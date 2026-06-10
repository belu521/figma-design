# MIDI 映射表（grandMA3 onPC）

所有消息均在 **MIDI 通道 1** 上发送/接收。

## 控制器 → MA3 onPC（输入）

### 推子（10 个，仅发 CC，不再附带 Note）

| 推子 | Mux/通道 | 消息类型 | CC 编号 | 值范围 | MA3 MIDI Remote 建议 |
|------|----------|----------|---------|--------|----------------------|
| Fader 1 | 1/0 | CC | 46 | 0–127 | Fader → Exec 201 |
| Fader 2 | 1/1 | CC | 47 | 0–127 | Fader → Exec 202 |
| Fader 3 | 1/2 | CC | 48 | 0–127 | Fader → Exec 203 |
| Fader 4 | 1/3 | CC | 49 | 0–127 | Fader → Exec 204 |
| Fader 5 | 1/4 | CC | 50 | 0–127 | Fader → Exec 205 |
| Fader 6 | 1/9 | CC | 51 | 0–127 | Fader → Exec 206 |
| Fader 7 | 1/8 | CC | 52 | 0–127 | Fader → Exec 207 |
| Fader 8 | 1/7 | CC | 53 | 0–127 | Fader → Exec 208 |
| Fader 9 | 1/6 | CC | 54 | 0–127 | Fader → Exec 209 |
| Fader 10 | 1/5 | CC | 55 | 0–127 | Fader → Exec 210 |

### 矩阵按钮（20 个，Note On/Off）

| 按钮 | 消息类型 | Note 编号 | 按下 | 松开 | MA3 MIDI Remote 建议 |
|------|----------|-----------|------|------|----------------------|
| Btn 1–5（第 1 排） | Note | 70–74 | NoteOn vel 127 | NoteOff | Key → Exec 101–105 |
| Btn 6–10（第 2 排） | Note | 75–79 | NoteOn vel 127 | NoteOff | Key → Exec 106–110 |
| Btn 11–15（第 3 排） | Note | 80–84 | NoteOn vel 127 | NoteOff | Key → Exec 111–115 |
| Btn 16–20（第 4 排） | Note | 85–89 | NoteOn vel 127 | NoteOff | Key → Exec 116–120 |

### 编码器（5 个，默认绝对 CC 模式）

固件默认 `ENCODER_ABSOLUTE_MODE 1`：每个棘轮步进 ±8，固件内累积为 0–127 的绝对值后发送，可直接在 MA3 中按 Fader 类型映射。设为 0 可切回相对 CC（2's complement：CW=1, CCW=127）。

| 编码器 | 消息类型 | CC 编号 | 模式 | MA3 MIDI Remote 建议 |
|--------|----------|---------|------|----------------------|
| Enc 1 | CC | 20 | 绝对 0–127 | Fader → Exec 290（或自定义） |
| Enc 2 | CC | 21 | 绝对 0–127 | Fader → Exec 291 |
| Enc 3 | CC | 22 | 绝对 0–127 | Fader → Exec 292 |
| Enc 4 | CC | 23 | 绝对 0–127 | Fader → Exec 293 |
| Enc 5 | CC | 24 | 绝对 0–127 | Fader → Exec 294 |

### 编码器按钮（5 个，CC 开关）

| 按钮 | 消息类型 | CC 编号 | 按下/松开 | MA3 MIDI Remote 建议 |
|------|----------|---------|-----------|----------------------|
| EncBtn 1 | CC | 25 | 127 / 0 | Key → Please |
| EncBtn 2 | CC | 26 | 127 / 0 | Key → Clear |
| EncBtn 3 | CC | 27 | 127 / 0 | Key → Highlt |
| EncBtn 4 | CC | 28 | 127 / 0 | Key → Blind |
| EncBtn 5 | CC | 29 | 127 / 0 | Key → 自定义 |

## MA3 onPC → 控制器（反馈输出）

Pro Micro 接收 MA3 的 USB MIDI 输出并下发给 ESP32，驱动 LED：

| MA3 输出 | 消息 | 控制器行为 |
|----------|------|------------|
| Executor 激活 | Note On 70–89（vel > 0） | 对应矩阵按钮 LED 变绿 |
| Executor 关闭 | Note Off 70–89（或 vel 0） | 对应按钮 LED 恢复默认蓝色 |
| Fader 电平回传 | CC 46–55 | 对应推子 LED 按电平显示亮度（移动物理推子时本地值接管） |

RGB 自定义反馈协议（消息类型 0x0A/0x0C/0x0D/0x0E）保持不变，可用于自定义颜色标签。
