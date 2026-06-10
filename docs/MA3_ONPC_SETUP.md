# grandMA3 onPC 配置指南

本指南介绍如何在 grandMA3 onPC 中配置本控制器（Pro Micro USB MIDI 设备）。

## 1. 硬件接线

```
ESP32-S3 TX (GPIO43) ──> Pro Micro RX1
ESP32-S3 RX (GPIO44) <── Pro Micro TX1
ESP32-S3 GND ────────── Pro Micro GND
Pro Micro USB ────────> 运行 MA3 onPC 的电脑
```

波特率：31250（双向，固件已设定）。

## 2. 启用 MIDI 设备

1. 将 Pro Micro 通过 USB 连接到电脑，确认系统识别为 MIDI 设备（如 "Arduino Micro"）。
2. 打开 MA3 onPC → **Menu → In & Out → MIDI** 页签。
3. 在 **MIDI In Device** 与 **MIDI Out Device** 中选择 Pro Micro 对应的设备。
4. `MIDIShowControl` 等无关项保持默认即可。

## 3. 配置 MIDI Remotes（输入映射）

打开 **Menu → In & Out → MIDI Remotes**，为每个控件新建一行 Remote。
通用设置：`Enabled = Yes`，`MIDIChannel = 1`。

### 推子（CC 46–55）

| 列 | 值 |
|----|----|
| Trigger Type | CC |
| Index | 46 … 55 |
| Type | Fader |
| Target | Page 1 对应的 Fader Executor（如 Exec 201–210） |

### 矩阵按钮（Note 70–89）

| 列 | 值 |
|----|----|
| Trigger Type | Note |
| Index | 70 … 89 |
| Type | Key 或 Exec |
| Target | 对应 Executor 按钮（如 Exec 101–120）或命令 |

### 编码器（CC 20–24，绝对值）

固件默认输出绝对 0–127 CC，按推子方式映射：

| 列 | 值 |
|----|----|
| Trigger Type | CC |
| Index | 20 … 24 |
| Type | Fader |
| Target | 任意 Fader Executor（如 Master、Speed Master） |

若改用相对模式（固件 `ENCODER_ABSOLUTE_MODE 0`），需在 MA3 中用 CMD 类型映射自定义命令。

### 编码器按钮（CC 25–29）

| 列 | 值 |
|----|----|
| Trigger Type | CC |
| Index | 25 … 29 |
| Type | Key |
| Target | Please / Clear / Highlt / Blind / 自定义 |

## 4. 配置 MIDI 输出反馈

1. 在 MIDI Remotes 中为需要反馈的 Remote 设置 **OutputType**：
   - 按钮 Executor：`OutputType = Note`，MA3 会在 executor 激活/关闭时发送 Note On/Off（编号与输入 Index 相同 70–89）。
   - 推子 Executor：`OutputType = CC`，MA3 回传电平到 CC 46–55。
2. 控制器收到反馈后：
   - 按钮 LED：executor 激活 = 绿色，关闭 = 默认蓝色，物理按下 = 白色。
   - 推子 LED：按 MA3 回传电平显示亮度；移动物理推子时由本地值接管。

## 5. 验证步骤

1. **MIDI 监视**：用 MIDI-OX（Windows）或 Protokol 确认：
   - 每个推子只输出对应 CC（46–55），无 Note 伴随。
   - 按钮输出 Note On（vel 127）/ Note Off。
   - 编码器输出平滑递增/递减的绝对 CC 值。
2. **MA3 联调**：逐一验证推子跟手、按钮触发 executor、编码器调节、LED 反馈。
3. **压力测试**：同时推动多个推子，确认无丢消息、LED 不卡顿。

## 6. 已知限制与后续

- MA3 MIDI Remote 对相对编码器支持有限，因此固件默认绝对模式。
- 第二阶段可评估 ESP32-S3 直接通过 WiFi/OSC 连接 MA3 onPC（更丰富的反馈、无需 Pro Micro）。
