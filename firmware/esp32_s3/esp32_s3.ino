#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ESP32-S3 reads controls and LEDs, then exchanges compact MIDI/LED packets with Pro Micro.
// Adapted for grandMA3 onPC:
//   - Faders send CC only (no duplicate Note messages).
//   - Encoders default to absolute CC mode (MA3 MIDI Remote "Fader" type friendly).
//   - MA3 MIDI output feedback (Note/CC via Pro Micro downlink) drives button/fader LEDs.
//   - OSC over WiFi: real-time executor appearance colors from a grandMA3 Lua plugin
//     drive button/fader LED colors (see ma3_plugin/ColorFeedback.lua).
// Wire ESP32 TX -> Pro Micro RX1, Pro Micro TX1 -> ESP32 RX, GND -> GND.

#define ESP32_TO_PROMICRO_BAUD 31250
#define ESP32_TX_PIN 43
#define ESP32_RX_PIN 44

// --- OSC color feedback over WiFi ---
// Set ENABLE_OSC_COLOR_FEEDBACK to 0 to fall back to MIDI-only feedback.
// The MA3 Lua plugin sends:
//   /btn/<note>   ,iii  r g b   (0-255, executor appearance color for Note 70-89)
//   /fader/<cc>   ,iii  r g b   (0-255, executor appearance color for CC 46-55)
// All-zero RGB clears the color override (LED returns to default behavior).
#define ENABLE_OSC_COLOR_FEEDBACK 1
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define OSC_LISTEN_PORT 8000
#define WIFI_RECONNECT_INTERVAL_MS 5000

#define MIDI_CHANNEL 1

#define LED_PIN_1 40
#define LED_PIN_2 41
#define LED_PIN_3 42
#define LED_COUNT_1 55
#define LED_COUNT_2 15
#define LED_COUNT_3 30
#define ENCODER_LED_GROUP_SIZE 11
#define ENCODER_BUTTON_LED_GROUP_SIZE 3
#define MATRIX_LED_ROW_SIZE 5

#define MUX_S0 15
#define MUX_S1 16
#define MUX_S2 17
#define MUX_S3 18
#define MUX1_SIG 4
#define MUX2_SIG 5

#define MUX_CHANNELS 16
#define ANALOG_CHANGE_THRESHOLD 4
#define FADER_SAMPLE_COUNT 8
#define FADER_LOW_CLAMP 40
#define FADER_HIGH_CLAMP 4050
#define FADER_EMA_SHIFT 3
#define FADER_MIN_INTERVAL_MS 25
#define MUX_SCAN_INTERVAL_MS 5
#define BUTTON_DEBOUNCE_MS 20
#define DEBUG_RGB_FEEDBACK 1

// EC11 is a relative mechanical encoder. Each detent updates the encoder value.
// ENCODER_ABSOLUTE_MODE 1: firmware accumulates an absolute 0-127 value and sends
//   it as an absolute CC (recommended for MA3 MIDI Remote, type Fader).
// ENCODER_ABSOLUTE_MODE 0: relative CC.
//   ENCODER_RELATIVE_MODE 0: 2's complement relative, CW=1, CCW=127.
//   ENCODER_RELATIVE_MODE 1: binary offset relative, CW=65, CCW=63.
#define EC11_STEPS_PER_DETENT 4
#define ENCODER_ABSOLUTE_MODE 1
#define ENCODER_ABSOLUTE_STEP 8
#define ENCODER_RELATIVE_MODE 0
#define ENCODER_REVERSE_DIRECTION 0

#define MSG_NOTE_OFF 0x08
#define MSG_NOTE_ON 0x09
#define MSG_BUTTON_RGB_BLUE 0x0A
#define MSG_CC 0x0B
#define MSG_BUTTON_RGB_GREEN 0x0C
#define MSG_BUTTON_RGB_RED 0x0D
#define MSG_BUTTON_RGB_NOTE 0x0E

struct Encoder {
  byte pinA;
  byte pinB;
  byte cc;
  byte lastState;
  int8_t stepAccumulator;
  byte absoluteValue;
};

Encoder encoders[] = {
  {6, 7, 20, 0, 0, 0},
  {8, 9, 21, 0, 0, 0},
  {10, 11, 22, 0, 0, 0},
  {12, 13, 23, 0, 0, 0},
  {14, 3, 24, 0, 0, 0},
};

const byte encoderCount = sizeof(encoders) / sizeof(encoders[0]);

struct Button {
  byte pin;
  byte cc;
  byte stableState;
  byte lastReading;
  unsigned long lastChangeTime;
};

Button encoderButtons[] = {
  {35, 25, HIGH, HIGH, 0},
  {36, 26, HIGH, HIGH, 0},
  {37, 27, HIGH, HIGH, 0},
  {38, 28, HIGH, HIGH, 0},
  {39, 29, HIGH, HIGH, 0},
};

const byte encoderButtonCount = sizeof(encoderButtons) / sizeof(encoderButtons[0]);

struct MuxFader {
  byte mux;
  byte channel;
  byte cc;
  byte faderIndex;
};

MuxFader muxFaders[] = {
  {1, 0, 46, 0},
  {1, 1, 47, 1},
  {1, 2, 48, 2},
  {1, 3, 49, 3},
  {1, 4, 50, 4},
  {1, 9, 51, 5},
  {1, 8, 52, 6},
  {1, 7, 53, 7},
  {1, 6, 54, 8},
  {1, 5, 55, 9},
};

const byte muxFaderCount = sizeof(muxFaders) / sizeof(muxFaders[0]);

struct MuxButton {
  byte mux;
  byte channel;
  byte cc;
  byte ledIndex;
  byte stableState;
  byte lastReading;
  unsigned long lastChangeTime;
};

MuxButton muxButtons[] = {
  {1, 11, 70, 0, HIGH, HIGH, 0},
  {1, 12, 71, 1, HIGH, HIGH, 0},
  {1, 13, 72, 2, HIGH, HIGH, 0},
  {1, 14, 73, 3, HIGH, HIGH, 0},
  {1, 15, 74, 4, HIGH, HIGH, 0},
  {2, 0, 75, 5, HIGH, HIGH, 0},
  {2, 1, 76, 6, HIGH, HIGH, 0},
  {2, 2, 77, 7, HIGH, HIGH, 0},
  {2, 3, 78, 8, HIGH, HIGH, 0},
  {2, 4, 79, 9, HIGH, HIGH, 0},
  {2, 9, 80, 20, HIGH, HIGH, 0},
  {2, 8, 81, 21, HIGH, HIGH, 0},
  {2, 7, 82, 22, HIGH, HIGH, 0},
  {2, 6, 83, 23, HIGH, HIGH, 0},
  {2, 5, 84, 24, HIGH, HIGH, 0},
  {2, 14, 85, 25, HIGH, HIGH, 0},
  {2, 13, 86, 26, HIGH, HIGH, 0},
  {2, 12, 87, 27, HIGH, HIGH, 0},
  {2, 11, 88, 28, HIGH, HIGH, 0},
  {2, 10, 89, 29, HIGH, HIGH, 0},
};

const byte muxButtonCount = sizeof(muxButtons) / sizeof(muxButtons[0]);

enum IncomingParserState {
  WAIT_HEADER,
  WAIT_DATA1,
  WAIT_DATA2,
  WAIT_CHECKSUM
};

byte encoderLedPosition[5] = {0, 0, 0, 0, 0};
bool muxButtonFeedbackActive[sizeof(muxButtons) / sizeof(muxButtons[0])];
byte muxButtonFeedbackRed[sizeof(muxButtons) / sizeof(muxButtons[0])];
byte muxButtonFeedbackGreen[sizeof(muxButtons) / sizeof(muxButtons[0])];
byte muxButtonFeedbackBlue[sizeof(muxButtons) / sizeof(muxButtons[0])];
bool muxButtonExecActive[sizeof(muxButtons) / sizeof(muxButtons[0])];
bool muxFaderFeedbackActive[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderFeedbackRed[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderFeedbackGreen[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderFeedbackBlue[sizeof(muxFaders) / sizeof(muxFaders[0])];
bool muxFaderMa3Active[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderMa3Level[sizeof(muxFaders) / sizeof(muxFaders[0])];
// OSC executor appearance colors (8-bit RGB, highest priority LED override).
bool muxButtonOscColorActive[sizeof(muxButtons) / sizeof(muxButtons[0])];
byte muxButtonOscRed[sizeof(muxButtons) / sizeof(muxButtons[0])];
byte muxButtonOscGreen[sizeof(muxButtons) / sizeof(muxButtons[0])];
byte muxButtonOscBlue[sizeof(muxButtons) / sizeof(muxButtons[0])];
bool muxFaderOscColorActive[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderOscRed[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderOscGreen[sizeof(muxFaders) / sizeof(muxFaders[0])];
byte muxFaderOscBlue[sizeof(muxFaders) / sizeof(muxFaders[0])];
int muxLastValue[2][MUX_CHANNELS];
uint32_t muxFaderFiltered[2][MUX_CHANNELS];
bool muxFaderFilterInited[2][MUX_CHANNELS];
unsigned long muxFaderLastSendTime[2][MUX_CHANNELS];
unsigned long lastMuxScanTime = 0;
IncomingParserState incomingParserState = WAIT_HEADER;
uint8_t incomingHeader = 0;
uint8_t incomingData1 = 0;
uint8_t incomingData2 = 0;

#if ENABLE_OSC_COLOR_FEEDBACK
WiFiUDP oscUdp;
unsigned long lastWifiAttemptTime = 0;
#endif

Adafruit_NeoPixel strip1(LED_COUNT_1, LED_PIN_1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT_2, LED_PIN_2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip3(LED_COUNT_3, LED_PIN_3, NEO_GRB + NEO_KHZ800);

uint8_t makeHeader(uint8_t type, uint8_t channel) {
  return 0x80 | ((type & 0x0F) << 4) | ((channel - 1) & 0x0F);
}

uint8_t calcChecksum(uint8_t h, uint8_t d1, uint8_t d2) {
  return (h ^ d1 ^ d2) & 0x7F;
}

void sendToProMicro(uint8_t type, uint8_t d1, uint8_t d2) {
  uint8_t header = makeHeader(type, MIDI_CHANNEL);
  d1 &= 0x7F;
  d2 &= 0x7F;
  uint8_t checksum = calcChecksum(header, d1, d2);
  Serial1.write(header);
  Serial1.write(d1);
  Serial1.write(d2);
  Serial1.write(checksum);
}

void sendControlChange(byte cc, byte value) {
  sendToProMicro(MSG_CC, cc, value);
}

void sendNoteOn(byte note, byte velocity) {
  sendToProMicro(MSG_NOTE_ON, note, velocity);
}

void sendNoteOff(byte note) {
  sendToProMicro(MSG_NOTE_OFF, note, 0);
}

void clearAll() {
  strip1.clear();
  strip2.clear();
  strip3.clear();
  strip1.show();
  strip2.show();
  strip3.show();
}

uint32_t bluePurpleColor(Adafruit_NeoPixel& strip, byte phase) {
  byte red = map(phase, 0, 255, 20, 180);
  return strip.Color(red, 0, 255);
}

void renderSelfTestStrip(Adafruit_NeoPixel& strip, int ledCount, byte frameOffset) {
  for (int i = 0; i < ledCount; i++) {
    byte phase = (i * 18 + frameOffset) & 0xFF;
    strip.setPixelColor(i, bluePurpleColor(strip, phase));
  }
}

void runLedSelfTest() {
  strip1.setBrightness(80);
  strip2.setBrightness(80);
  strip3.setBrightness(80);

  for (byte frame = 0; frame < 90; frame++) {
    byte offset = frame * 6;
    renderSelfTestStrip(strip1, LED_COUNT_1, offset);
    renderSelfTestStrip(strip2, LED_COUNT_2, offset + 60);
    renderSelfTestStrip(strip3, LED_COUNT_3, offset + 120);
    strip1.show();
    strip2.show();
    strip3.show();
    delay(18);
  }

  clearAll();
  strip1.setBrightness(80);
  strip2.setBrightness(80);
  strip3.setBrightness(80);
}

void renderEncoderLed(byte encoderIndex) {
  int startLed = encoderIndex * ENCODER_LED_GROUP_SIZE;
  for (byte i = 0; i < ENCODER_LED_GROUP_SIZE; i++) {
    uint32_t color = i <= encoderLedPosition[encoderIndex] ? strip1.Color(0, 0, 255) : 0;
    strip1.setPixelColor(startLed + i, color);
  }
}

void renderEncoderButtonLed(byte buttonIndex, bool pressed) {
  int startLed = buttonIndex * ENCODER_BUTTON_LED_GROUP_SIZE;
  uint32_t color = pressed ? strip2.Color(255, 255, 255) : strip2.Color(0, 0, 255);
  for (byte i = 0; i < ENCODER_BUTTON_LED_GROUP_SIZE; i++) {
    strip2.setPixelColor(startLed + i, color);
  }
}

byte rgb7To8(byte value) {
  return map(value, 0, 127, 0, 255);
}

byte scaleColorByLevel(byte color7, byte level) {
  byte brightnessPercent = map(level, 0, 127, 5, 95);
  return (uint16_t)rgb7To8(color7) * brightnessPercent / 100;
}

void renderMatrixButtonLed(byte ledIndex, bool pressed) {
  strip3.setPixelColor(ledIndex, pressed ? strip3.Color(255, 255, 255) : strip3.Color(0, 0, 255));
}

void renderMuxButtonLed(byte buttonIndex) {
  if (muxButtons[buttonIndex].stableState == LOW) {
    renderMatrixButtonLed(muxButtons[buttonIndex].ledIndex, true);
    return;
  }

  // OSC executor appearance color (real-time from MA3 Lua plugin) has top priority.
  if (muxButtonOscColorActive[buttonIndex]) {
    strip3.setPixelColor(
      muxButtons[buttonIndex].ledIndex,
      strip3.Color(
        muxButtonOscRed[buttonIndex],
        muxButtonOscGreen[buttonIndex],
        muxButtonOscBlue[buttonIndex]
      )
    );
    return;
  }

  if (muxButtonFeedbackActive[buttonIndex]) {
    strip3.setPixelColor(
      muxButtons[buttonIndex].ledIndex,
      strip3.Color(
        rgb7To8(muxButtonFeedbackRed[buttonIndex]),
        rgb7To8(muxButtonFeedbackGreen[buttonIndex]),
        rgb7To8(muxButtonFeedbackBlue[buttonIndex])
      )
    );
    return;
  }

  // MA3 executor state feedback: active = green, inactive = dim blue.
  if (muxButtonExecActive[buttonIndex]) {
    strip3.setPixelColor(muxButtons[buttonIndex].ledIndex, strip3.Color(0, 255, 0));
    return;
  }

  renderMatrixButtonLed(muxButtons[buttonIndex].ledIndex, false);
}

void renderFaderLed(byte faderIndex, byte value) {
  byte row = faderIndex < 5 ? 2 : 3;
  byte column = faderIndex % 5;
  byte ledIndex = row * MATRIX_LED_ROW_SIZE + column;

  // MA3 fader level feedback overrides the local value when present.
  byte displayValue = muxFaderMa3Active[faderIndex] ? muxFaderMa3Level[faderIndex] : value;

  // OSC executor appearance color: tint the fader LED, brightness follows the level.
  if (muxFaderOscColorActive[faderIndex]) {
    byte brightnessPercent = map(displayValue, 0, 127, 5, 95);
    strip3.setPixelColor(
      ledIndex,
      strip3.Color(
        (uint16_t)muxFaderOscRed[faderIndex] * brightnessPercent / 100,
        (uint16_t)muxFaderOscGreen[faderIndex] * brightnessPercent / 100,
        (uint16_t)muxFaderOscBlue[faderIndex] * brightnessPercent / 100
      )
    );
    return;
  }

  if (muxFaderFeedbackActive[faderIndex]) {
    strip3.setPixelColor(
      ledIndex,
      strip3.Color(
        scaleColorByLevel(muxFaderFeedbackRed[faderIndex], displayValue),
        scaleColorByLevel(muxFaderFeedbackGreen[faderIndex], displayValue),
        scaleColorByLevel(muxFaderFeedbackBlue[faderIndex], displayValue)
      )
    );
    return;
  }

  byte level = map(displayValue, 0, 127, 8, 255);
  strip3.setPixelColor(ledIndex, strip3.Color(level, level, level));
}

void renderAllIndicators() {
  strip1.clear();
  strip2.clear();
  strip3.clear();

  for (byte i = 0; i < encoderCount; i++) {
    renderEncoderLed(i);
  }
  for (byte i = 0; i < encoderButtonCount; i++) {
    renderEncoderButtonLed(i, encoderButtons[i].stableState == LOW);
  }
  for (byte i = 0; i < muxButtonCount; i++) {
    renderMuxButtonLed(i);
  }
  for (byte i = 0; i < muxFaderCount; i++) {
    byte muxIndex = muxFaders[i].mux - 1;
    byte channel = muxFaders[i].channel;
    renderFaderLed(muxFaders[i].faderIndex, muxLastValue[muxIndex][channel] >= 0 ? muxLastValue[muxIndex][channel] : 0);
  }

  strip1.show();
  strip2.show();
  strip3.show();
}

void initLeds() {
  strip1.begin();
  strip2.begin();
  strip3.begin();
  strip1.setBrightness(80);
  strip2.setBrightness(80);
  strip3.setBrightness(80);
  clearAll();
}

void initButtonFeedback() {
  for (byte i = 0; i < muxButtonCount; i++) {
    muxButtonFeedbackActive[i] = false;
    muxButtonFeedbackRed[i] = 0;
    muxButtonFeedbackGreen[i] = 0;
    muxButtonFeedbackBlue[i] = 0;
    muxButtonExecActive[i] = false;
    muxButtonOscColorActive[i] = false;
    muxButtonOscRed[i] = 0;
    muxButtonOscGreen[i] = 0;
    muxButtonOscBlue[i] = 0;
  }
  for (byte i = 0; i < muxFaderCount; i++) {
    muxFaderFeedbackActive[i] = false;
    muxFaderFeedbackRed[i] = 0;
    muxFaderFeedbackGreen[i] = 0;
    muxFaderFeedbackBlue[i] = 0;
    muxFaderMa3Active[i] = false;
    muxFaderMa3Level[i] = 0;
    muxFaderOscColorActive[i] = false;
    muxFaderOscRed[i] = 0;
    muxFaderOscGreen[i] = 0;
    muxFaderOscBlue[i] = 0;
  }
}

void initEncoders() {
  for (byte i = 0; i < encoderCount; i++) {
    pinMode(encoders[i].pinA, INPUT_PULLUP);
    pinMode(encoders[i].pinB, INPUT_PULLUP);
    encoders[i].lastState = (digitalRead(encoders[i].pinA) << 1) | digitalRead(encoders[i].pinB);
    encoders[i].absoluteValue = 0;
  }
}

byte encoderRelativeValue(int8_t direction) {
#if ENCODER_RELATIVE_MODE == 1
  return direction > 0 ? 65 : 63;
#else
  return direction > 0 ? 1 : 127;
#endif
}

void emitEncoderStep(byte encoderIndex, int8_t direction) {
#if ENCODER_REVERSE_DIRECTION
  direction = -direction;
#endif

#if ENCODER_ABSOLUTE_MODE
  int next = (int)encoders[encoderIndex].absoluteValue + direction * ENCODER_ABSOLUTE_STEP;
  if (next < 0) {
    next = 0;
  } else if (next > 127) {
    next = 127;
  }
  if (next == encoders[encoderIndex].absoluteValue) {
    return;
  }
  encoders[encoderIndex].absoluteValue = next;
  sendControlChange(encoders[encoderIndex].cc, encoders[encoderIndex].absoluteValue);
  encoderLedPosition[encoderIndex] = map(encoders[encoderIndex].absoluteValue, 0, 127, 0, ENCODER_LED_GROUP_SIZE - 1);
#else
  sendControlChange(encoders[encoderIndex].cc, encoderRelativeValue(direction));
  if (direction > 0) {
    if (encoderLedPosition[encoderIndex] < ENCODER_LED_GROUP_SIZE - 1) {
      encoderLedPosition[encoderIndex]++;
    }
  } else {
    if (encoderLedPosition[encoderIndex] > 0) {
      encoderLedPosition[encoderIndex]--;
    }
  }
#endif

  renderEncoderLed(encoderIndex);
  strip1.show();
}

void scanEncoders() {
  const int8_t directionTable[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
  };

  for (byte i = 0; i < encoderCount; i++) {
    byte currentState = (digitalRead(encoders[i].pinA) << 1) | digitalRead(encoders[i].pinB);
    byte transition = (encoders[i].lastState << 2) | currentState;
    int8_t direction = directionTable[transition];

    if (currentState != encoders[i].lastState) {
      encoders[i].stepAccumulator += direction;
    }

    if (encoders[i].stepAccumulator >= EC11_STEPS_PER_DETENT) {
      encoders[i].stepAccumulator = 0;
      emitEncoderStep(i, 1);
    } else if (encoders[i].stepAccumulator <= -EC11_STEPS_PER_DETENT) {
      encoders[i].stepAccumulator = 0;
      emitEncoderStep(i, -1);
    }

    encoders[i].lastState = currentState;
  }
}

void initEncoderButtons() {
  for (byte i = 0; i < encoderButtonCount; i++) {
    pinMode(encoderButtons[i].pin, INPUT_PULLUP);
    byte reading = digitalRead(encoderButtons[i].pin);
    encoderButtons[i].stableState = reading;
    encoderButtons[i].lastReading = reading;
    encoderButtons[i].lastChangeTime = millis();
  }
}

void scanEncoderButtons() {
  unsigned long now = millis();
  for (byte i = 0; i < encoderButtonCount; i++) {
    byte reading = digitalRead(encoderButtons[i].pin);
    if (reading != encoderButtons[i].lastReading) {
      encoderButtons[i].lastReading = reading;
      encoderButtons[i].lastChangeTime = now;
    }
    if ((now - encoderButtons[i].lastChangeTime) >= BUTTON_DEBOUNCE_MS && reading != encoderButtons[i].stableState) {
      encoderButtons[i].stableState = reading;
      sendControlChange(encoderButtons[i].cc, reading == LOW ? 127 : 0);
      renderEncoderButtonLed(i, reading == LOW);
      strip2.show();
    }
  }
}

void selectMuxChannel(byte channel) {
  digitalWrite(MUX_S0, bitRead(channel, 0));
  digitalWrite(MUX_S1, bitRead(channel, 1));
  digitalWrite(MUX_S2, bitRead(channel, 2));
  digitalWrite(MUX_S3, bitRead(channel, 3));
  delayMicroseconds(10);
}

void initMuxes() {
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX1_SIG, INPUT);
  pinMode(MUX2_SIG, INPUT);

  for (byte muxIndex = 0; muxIndex < 2; muxIndex++) {
    for (byte i = 0; i < MUX_CHANNELS; i++) {
      muxLastValue[muxIndex][i] = -1;
      muxFaderFiltered[muxIndex][i] = 0;
      muxFaderFilterInited[muxIndex][i] = false;
      muxFaderLastSendTime[muxIndex][i] = 0;
    }
  }

  for (byte i = 0; i < muxButtonCount; i++) {
    selectMuxChannel(muxButtons[i].channel);
    byte sigPin = muxButtons[i].mux == 1 ? MUX1_SIG : MUX2_SIG;
    pinMode(sigPin, INPUT_PULLUP);
    byte reading = digitalRead(sigPin);
    muxButtons[i].stableState = reading;
    muxButtons[i].lastReading = reading;
    muxButtons[i].lastChangeTime = millis();
  }
}

byte readMuxFaderCcValue(byte muxIndex, byte channel, byte sigPin) {
  long total = 0;
  for (byte i = 0; i < FADER_SAMPLE_COUNT; i++) {
    total += analogRead(sigPin);
    delayMicroseconds(80);
  }

  int raw = total / FADER_SAMPLE_COUNT;
  if (raw <= FADER_LOW_CLAMP) {
    raw = 0;
  } else if (raw >= FADER_HIGH_CLAMP) {
    raw = 4095;
  }

  if (!muxFaderFilterInited[muxIndex][channel]) {
    muxFaderFiltered[muxIndex][channel] = ((uint32_t)raw) << FADER_EMA_SHIFT;
    muxFaderFilterInited[muxIndex][channel] = true;
  } else {
    muxFaderFiltered[muxIndex][channel] =
      muxFaderFiltered[muxIndex][channel] -
      (muxFaderFiltered[muxIndex][channel] >> FADER_EMA_SHIFT) +
      raw;
  }

  int smoothRaw = muxFaderFiltered[muxIndex][channel] >> FADER_EMA_SHIFT;
  return map(smoothRaw, 0, 4095, 127, 0);
}

int findMuxButtonIndex(byte mux, byte channel) {
  for (byte i = 0; i < muxButtonCount; i++) {
    if (muxButtons[i].mux == mux && muxButtons[i].channel == channel) {
      return i;
    }
  }
  return -1;
}

int findMuxFaderIndex(byte mux, byte channel) {
  for (byte i = 0; i < muxFaderCount; i++) {
    if (muxFaders[i].mux == mux && muxFaders[i].channel == channel) {
      return i;
    }
  }
  return -1;
}

byte getMuxSigPin(byte mux) {
  return mux == 1 ? MUX1_SIG : MUX2_SIG;
}

void scanMuxButton(byte buttonIndex) {
  unsigned long now = millis();
  byte sigPin = getMuxSigPin(muxButtons[buttonIndex].mux);
  pinMode(sigPin, INPUT_PULLUP);
  byte reading = digitalRead(sigPin);

  if (reading != muxButtons[buttonIndex].lastReading) {
    muxButtons[buttonIndex].lastReading = reading;
    muxButtons[buttonIndex].lastChangeTime = now;
  }

  if ((now - muxButtons[buttonIndex].lastChangeTime) >= BUTTON_DEBOUNCE_MS && reading != muxButtons[buttonIndex].stableState) {
    muxButtons[buttonIndex].stableState = reading;
    if (reading == LOW) {
      sendNoteOn(muxButtons[buttonIndex].cc, 127);
    } else {
      sendNoteOff(muxButtons[buttonIndex].cc);
    }
    renderMuxButtonLed(buttonIndex);
    strip3.show();
  }
}

void scanMuxes() {
  if (millis() - lastMuxScanTime < MUX_SCAN_INTERVAL_MS) {
    return;
  }

  lastMuxScanTime = millis();

  for (byte channel = 0; channel < MUX_CHANNELS; channel++) {
    selectMuxChannel(channel);
    for (byte mux = 1; mux <= 2; mux++) {
      int buttonIndex = findMuxButtonIndex(mux, channel);
      int faderIndex = findMuxFaderIndex(mux, channel);

      if (buttonIndex >= 0) {
        scanMuxButton(buttonIndex);
      }

      if (faderIndex >= 0) {
        byte sigPin = getMuxSigPin(mux);
        byte muxIndex = mux - 1;
        pinMode(sigPin, INPUT);
        byte value = readMuxFaderCcValue(muxIndex, channel, sigPin);

        if (muxLastValue[muxIndex][channel] < 0) {
          muxLastValue[muxIndex][channel] = value;
          renderFaderLed(muxFaders[faderIndex].faderIndex, value);
          strip3.show();
        } else {
          unsigned long now = millis();
          byte threshold = (value == 0 || value == 127) ? 0 : ANALOG_CHANGE_THRESHOLD;
          int delta = abs(value - muxLastValue[muxIndex][channel]);
          if (delta > threshold && now - muxFaderLastSendTime[muxIndex][channel] >= FADER_MIN_INTERVAL_MS) {
            muxFaderLastSendTime[muxIndex][channel] = now;
            muxLastValue[muxIndex][channel] = value;
            sendControlChange(muxFaders[faderIndex].cc, value);
            // Moving the physical fader takes over from MA3 feedback.
            muxFaderMa3Active[muxFaders[faderIndex].faderIndex] = false;
            renderFaderLed(muxFaders[faderIndex].faderIndex, value);
            strip3.show();
          }
        }
      }
    }
  }
}

void resetIncomingParser() {
  incomingParserState = WAIT_HEADER;
  incomingHeader = 0;
  incomingData1 = 0;
  incomingData2 = 0;
}

int findMuxButtonByMidiNote(byte note) {
  for (byte i = 0; i < muxButtonCount; i++) {
    if (muxButtons[i].cc == note) {
      return i;
    }
  }
  return -1;
}

int findMuxFaderByMidiNote(byte note) {
  for (byte i = 0; i < muxFaderCount; i++) {
    if (muxFaders[i].cc == note) {
      return i;
    }
  }
  return -1;
}

void applyButtonRgb(byte note) {
  int buttonIndex = findMuxButtonByMidiNote(note);
  if (buttonIndex < 0) {
    return;
  }

  muxButtonFeedbackActive[buttonIndex] = true;
  renderMuxButtonLed(buttonIndex);
  strip3.show();

#if DEBUG_RGB_FEEDBACK
  Serial.print("RGB feedback note=");
  Serial.print(note);
  Serial.print(" led=");
  Serial.print(muxButtons[buttonIndex].ledIndex);
  Serial.print(" rgb7=(");
  Serial.print(muxButtonFeedbackRed[buttonIndex]);
  Serial.print(",");
  Serial.print(muxButtonFeedbackGreen[buttonIndex]);
  Serial.print(",");
  Serial.print(muxButtonFeedbackBlue[buttonIndex]);
  Serial.println(")");
#endif
}

void applyFaderRgb(byte note) {
  int faderIndex = findMuxFaderByMidiNote(note);
  if (faderIndex < 0) {
    return;
  }

  muxFaderFeedbackActive[faderIndex] = true;
  renderFaderLed(faderIndex, muxLastValue[muxFaders[faderIndex].mux - 1][muxFaders[faderIndex].channel] >= 0 ? muxLastValue[muxFaders[faderIndex].mux - 1][muxFaders[faderIndex].channel] : 0);
  strip3.show();
}

void handleRgbPacket(byte messageType, byte note, byte value) {
  int buttonIndex = findMuxButtonByMidiNote(note);
  int faderIndex = findMuxFaderByMidiNote(note);
  if (buttonIndex < 0 && faderIndex < 0) {
    return;
  }

  if (messageType == MSG_BUTTON_RGB_NOTE) {
    if (buttonIndex >= 0) {
      muxButtonFeedbackRed[buttonIndex] = 0;
      muxButtonFeedbackGreen[buttonIndex] = 0;
      muxButtonFeedbackBlue[buttonIndex] = 0;
    }
    if (faderIndex >= 0) {
      muxFaderFeedbackRed[faderIndex] = 0;
      muxFaderFeedbackGreen[faderIndex] = 0;
      muxFaderFeedbackBlue[faderIndex] = 0;
    }
  } else if (messageType == MSG_BUTTON_RGB_RED) {
    if (buttonIndex >= 0) {
      muxButtonFeedbackRed[buttonIndex] = value & 0x7F;
    }
    if (faderIndex >= 0) {
      muxFaderFeedbackRed[faderIndex] = value & 0x7F;
    }
  } else if (messageType == MSG_BUTTON_RGB_GREEN) {
    if (buttonIndex >= 0) {
      muxButtonFeedbackGreen[buttonIndex] = value & 0x7F;
    }
    if (faderIndex >= 0) {
      muxFaderFeedbackGreen[faderIndex] = value & 0x7F;
    }
  } else if (messageType == MSG_BUTTON_RGB_BLUE) {
    if (buttonIndex >= 0) {
      muxButtonFeedbackBlue[buttonIndex] = value & 0x7F;
      applyButtonRgb(note);
    }
    if (faderIndex >= 0) {
      muxFaderFeedbackBlue[faderIndex] = value & 0x7F;
      applyFaderRgb(note);
    }
  }
}

// MA3 executor state feedback: Note On = executor active, Note Off = inactive.
void handleMa3NoteFeedback(byte note, byte velocity, bool isNoteOn) {
  int buttonIndex = findMuxButtonByMidiNote(note);
  if (buttonIndex < 0) {
    return;
  }

  muxButtonExecActive[buttonIndex] = isNoteOn && velocity > 0;
  renderMuxButtonLed(buttonIndex);
  strip3.show();
}

// MA3 fader level feedback: CC value drives fader LED brightness.
void handleMa3CcFeedback(byte cc, byte value) {
  int faderIndex = findMuxFaderByMidiNote(cc);
  if (faderIndex < 0) {
    return;
  }

  byte slot = muxFaders[faderIndex].faderIndex;
  muxFaderMa3Active[slot] = true;
  muxFaderMa3Level[slot] = value & 0x7F;
  renderFaderLed(slot, muxFaderMa3Level[slot]);
  strip3.show();
}

void handleIncomingPacket(uint8_t header, uint8_t data1, uint8_t data2) {
  uint8_t messageType = (header >> 4) & 0x0F;
  uint8_t channel = (header & 0x0F) + 1;

  if (channel != MIDI_CHANNEL) {
    return;
  }

  if (messageType == MSG_NOTE_ON) {
    handleMa3NoteFeedback(data1, data2, true);
  } else if (messageType == MSG_NOTE_OFF) {
    handleMa3NoteFeedback(data1, data2, false);
  } else if (messageType == MSG_CC) {
    handleMa3CcFeedback(data1, data2);
  } else if (
    messageType == MSG_BUTTON_RGB_NOTE ||
    messageType == MSG_BUTTON_RGB_RED ||
    messageType == MSG_BUTTON_RGB_GREEN ||
    messageType == MSG_BUTTON_RGB_BLUE
  ) {
    handleRgbPacket(messageType, data1, data2);
  }
}

void parseIncomingByte(uint8_t incoming) {
  switch (incomingParserState) {
    case WAIT_HEADER:
      if (incoming & 0x80) {
        incomingHeader = incoming;
        incomingParserState = WAIT_DATA1;
      }
      break;

    case WAIT_DATA1:
      if (incoming & 0x80) {
        incomingHeader = incoming;
      } else {
        incomingData1 = incoming;
        incomingParserState = WAIT_DATA2;
      }
      break;

    case WAIT_DATA2:
      if (incoming & 0x80) {
        incomingHeader = incoming;
        incomingParserState = WAIT_DATA1;
      } else {
        incomingData2 = incoming;
        incomingParserState = WAIT_CHECKSUM;
      }
      break;

    case WAIT_CHECKSUM:
      if ((incoming & 0x80) || incoming != calcChecksum(incomingHeader, incomingData1, incomingData2)) {
        resetIncomingParser();
        if (incoming & 0x80) {
          incomingHeader = incoming;
          incomingParserState = WAIT_DATA1;
        }
      } else {
        handleIncomingPacket(incomingHeader, incomingData1, incomingData2);
        resetIncomingParser();
      }
      break;
  }
}

void readProMicroFeedback() {
  while (Serial1.available() > 0) {
    parseIncomingByte(Serial1.read());
  }
}

#if ENABLE_OSC_COLOR_FEEDBACK

// --- Minimal OSC receiver for MA3 executor appearance colors ---
// Accepts /btn/<note> and /fader/<cc> with three int32 (",iii") or
// three float32 (",fff", 0.0-1.0) arguments: r g b.

void applyOscButtonColor(byte note, byte r, byte g, byte b) {
  int buttonIndex = findMuxButtonByMidiNote(note);
  if (buttonIndex < 0) {
    return;
  }

  if (r == 0 && g == 0 && b == 0) {
    muxButtonOscColorActive[buttonIndex] = false;
  } else {
    muxButtonOscColorActive[buttonIndex] = true;
    muxButtonOscRed[buttonIndex] = r;
    muxButtonOscGreen[buttonIndex] = g;
    muxButtonOscBlue[buttonIndex] = b;
  }
  renderMuxButtonLed(buttonIndex);
  strip3.show();
}

void applyOscFaderColor(byte cc, byte r, byte g, byte b) {
  int faderIndex = findMuxFaderByMidiNote(cc);
  if (faderIndex < 0) {
    return;
  }

  byte slot = muxFaders[faderIndex].faderIndex;
  if (r == 0 && g == 0 && b == 0) {
    muxFaderOscColorActive[slot] = false;
  } else {
    muxFaderOscColorActive[slot] = true;
    muxFaderOscRed[slot] = r;
    muxFaderOscGreen[slot] = g;
    muxFaderOscBlue[slot] = b;
  }

  byte muxIndex = muxFaders[faderIndex].mux - 1;
  byte channel = muxFaders[faderIndex].channel;
  renderFaderLed(slot, muxLastValue[muxIndex][channel] >= 0 ? muxLastValue[muxIndex][channel] : 0);
  strip3.show();
}

int oscPaddedLength(int length) {
  return (length + 4) & ~3;
}

int32_t oscReadInt32(const uint8_t* data) {
  return ((int32_t)data[0] << 24) | ((int32_t)data[1] << 16) | ((int32_t)data[2] << 8) | (int32_t)data[3];
}

float oscReadFloat32(const uint8_t* data) {
  uint32_t bits = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

byte oscClampColor(long value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return (byte)value;
}

void handleOscMessage(const uint8_t* packet, int packetSize) {
  // Address pattern (null-terminated, padded to 4 bytes).
  int addressLength = strnlen((const char*)packet, packetSize);
  if (addressLength == 0 || addressLength >= packetSize) {
    return;
  }
  const char* address = (const char*)packet;

  int typeTagOffset = oscPaddedLength(addressLength);
  if (typeTagOffset >= packetSize || packet[typeTagOffset] != ',') {
    return;
  }
  const char* typeTags = (const char*)(packet + typeTagOffset);
  int typeTagLength = strnlen(typeTags, packetSize - typeTagOffset);
  int argOffset = typeTagOffset + oscPaddedLength(typeTagLength);

  bool isInts = strcmp(typeTags, ",iii") == 0;
  bool isFloats = strcmp(typeTags, ",fff") == 0;
  if ((!isInts && !isFloats) || argOffset + 12 > packetSize) {
    return;
  }

  byte rgb[3];
  for (byte i = 0; i < 3; i++) {
    const uint8_t* argData = packet + argOffset + i * 4;
    if (isInts) {
      rgb[i] = oscClampColor(oscReadInt32(argData));
    } else {
      rgb[i] = oscClampColor((long)(oscReadFloat32(argData) * 255.0f + 0.5f));
    }
  }

  if (strncmp(address, "/btn/", 5) == 0) {
    int note = atoi(address + 5);
    if (note >= 0 && note <= 127) {
      applyOscButtonColor((byte)note, rgb[0], rgb[1], rgb[2]);
    }
  } else if (strncmp(address, "/fader/", 7) == 0) {
    int cc = atoi(address + 7);
    if (cc >= 0 && cc <= 127) {
      applyOscFaderColor((byte)cc, rgb[0], rgb[1], rgb[2]);
    }
  }
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (lastWifiAttemptTime != 0 && now - lastWifiAttemptTime < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastWifiAttemptTime = now;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void initOsc() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptTime = millis();
  oscUdp.begin(OSC_LISTEN_PORT);
}

void readOscColorFeedback() {
  maintainWifi();

  int packetSize = oscUdp.parsePacket();
  while (packetSize > 0) {
    static uint8_t buffer[256];
    int length = oscUdp.read(buffer, sizeof(buffer));
    if (length > 0 && buffer[0] == '/') {
      handleOscMessage(buffer, length);
    }
    packetSize = oscUdp.parsePacket();
  }
}

#endif  // ENABLE_OSC_COLOR_FEEDBACK

void initControllerHardware() {
  initButtonFeedback();
  initEncoders();
  initEncoderButtons();
  initMuxes();
  renderAllIndicators();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(ESP32_TO_PROMICRO_BAUD, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN);

#if ENABLE_OSC_COLOR_FEEDBACK
  initOsc();
#endif

  initLeds();
  runLedSelfTest();
  initControllerHardware();
}

void loop() {
  readProMicroFeedback();
#if ENABLE_OSC_COLOR_FEEDBACK
  readOscColorFeedback();
#endif
  scanEncoders();
  scanEncoderButtons();
  scanMuxes();
}
