#include <USB-MIDI.h>

// Pro Micro bridges the ESP32-S3 and grandMA3 onPC over USB MIDI.
// Uplink:   ESP32 TX -> Pro Micro RX1 (4-byte checksum packets -> USB MIDI out).
// Downlink: MA3 onPC USB MIDI in -> Pro Micro TX1 -> ESP32 RX (same 4-byte protocol).
// Wire ESP32 TX -> Pro Micro RX1, Pro Micro TX1 -> ESP32 RX, ESP32 GND -> Pro Micro GND.

USBMIDI_CREATE_DEFAULT_INSTANCE();

#define ESP32_BAUD 31250
#define RX_LED_PULSE_MS 40

#define MSG_NOTE_OFF 0x08
#define MSG_NOTE_ON 0x09
#define MSG_CC 0x0B

enum ParserState {
  WAIT_HEADER,
  WAIT_DATA1,
  WAIT_DATA2,
  WAIT_CHECKSUM
};

ParserState parserState = WAIT_HEADER;
uint8_t rxHeader = 0;
uint8_t rxData1 = 0;
uint8_t rxData2 = 0;
unsigned long rxLedOffTime = 0;

uint8_t calcChecksum(uint8_t h, uint8_t d1, uint8_t d2) {
  return (h ^ d1 ^ d2) & 0x7F;
}

uint8_t makeHeader(uint8_t type, uint8_t channel) {
  return 0x80 | ((type & 0x0F) << 4) | ((channel - 1) & 0x0F);
}

void sendToEsp32(uint8_t type, uint8_t channel, uint8_t d1, uint8_t d2) {
  uint8_t header = makeHeader(type, channel);
  d1 &= 0x7F;
  d2 &= 0x7F;
  uint8_t checksum = calcChecksum(header, d1, d2);
  Serial1.write(header);
  Serial1.write(d1);
  Serial1.write(d2);
  Serial1.write(checksum);
}

void sendMidiPacket(uint8_t msgType, uint8_t channel, uint8_t data1, uint8_t data2) {
  if (msgType == MSG_CC) {
    MIDI.sendControlChange(data1 & 0x7F, data2 & 0x7F, channel);
  } else if (msgType == MSG_NOTE_ON) {
    MIDI.sendNoteOn(data1 & 0x7F, data2 & 0x7F, channel);
  } else if (msgType == MSG_NOTE_OFF) {
    MIDI.sendNoteOff(data1 & 0x7F, data2 & 0x7F, channel);
  }
}

void handlePacket(uint8_t header, uint8_t data1, uint8_t data2) {
  uint8_t msgType = (header >> 4) & 0x0F;
  uint8_t channel = (header & 0x0F) + 1;

  if (msgType == MSG_CC || msgType == MSG_NOTE_ON || msgType == MSG_NOTE_OFF) {
    sendMidiPacket(msgType, channel, data1, data2);
    digitalWrite(LED_BUILTIN, HIGH);
    rxLedOffTime = millis() + RX_LED_PULSE_MS;
  }
}

void resetParser() {
  parserState = WAIT_HEADER;
  rxHeader = 0;
  rxData1 = 0;
  rxData2 = 0;
}

void parseByte(uint8_t incoming) {
  switch (parserState) {
    case WAIT_HEADER:
      if (incoming & 0x80) {
        rxHeader = incoming;
        parserState = WAIT_DATA1;
      }
      break;

    case WAIT_DATA1:
      if (incoming & 0x80) {
        rxHeader = incoming;
      } else {
        rxData1 = incoming;
        parserState = WAIT_DATA2;
      }
      break;

    case WAIT_DATA2:
      if (incoming & 0x80) {
        rxHeader = incoming;
        parserState = WAIT_DATA1;
      } else {
        rxData2 = incoming;
        parserState = WAIT_CHECKSUM;
      }
      break;

    case WAIT_CHECKSUM:
      if ((incoming & 0x80) || incoming != calcChecksum(rxHeader, rxData1, rxData2)) {
        resetParser();
        if (incoming & 0x80) {
          rxHeader = incoming;
          parserState = WAIT_DATA1;
        }
      } else {
        handlePacket(rxHeader, rxData1, rxData2);
        resetParser();
      }
      break;
  }
}

// --- MA3 onPC feedback (USB MIDI in) -> ESP32 downlink ---

void onUsbNoteOn(byte channel, byte note, byte velocity) {
  if (velocity == 0) {
    sendToEsp32(MSG_NOTE_OFF, channel, note, 0);
  } else {
    sendToEsp32(MSG_NOTE_ON, channel, note, velocity);
  }
}

void onUsbNoteOff(byte channel, byte note, byte velocity) {
  sendToEsp32(MSG_NOTE_OFF, channel, note, velocity);
}

void onUsbControlChange(byte channel, byte controller, byte value) {
  sendToEsp32(MSG_CC, channel, controller, value);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial1.begin(ESP32_BAUD);

  MIDI.setHandleNoteOn(onUsbNoteOn);
  MIDI.setHandleNoteOff(onUsbNoteOff);
  MIDI.setHandleControlChange(onUsbControlChange);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOff();
}

void loop() {
  MIDI.read();

  if (rxLedOffTime > 0 && millis() >= rxLedOffTime) {
    digitalWrite(LED_BUILTIN, LOW);
    rxLedOffTime = 0;
  }

  while (Serial1.available() > 0) {
    parseByte(Serial1.read());
  }
}
