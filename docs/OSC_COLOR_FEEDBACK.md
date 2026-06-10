# OSC 实时颜色反馈（MA3 → 控制器 LED）

MA3 的 MIDI 反馈只能传开关/电平，无法传颜色。本功能通过 **OSC over USB** 实现：
MA3 上 executor 的外观颜色（Appearance）是什么，控制器对应按键/推子的指示灯就实时变成什么颜色。
整条链路只用现有的 USB 线，不需要 WiFi 和路由器。

## 架构

```
grandMA3 onPC ──(Lua 插件轮询 executor 颜色)──> SendOSC ──UDP 到 127.0.0.1:8000──>
PC 桥接程序 (tools/osc_usb_bridge.py) ──SLIP 封帧的 OSC──> USB CDC 串口 ──> ESP32-S3 ──> NeoPixel LED
```

- MIDI 链路（按键/推子/编码器控制 + Note/CC 反馈）保持不变。
- OSC 仅用于颜色反馈，是 LED 的最高优先级覆盖（物理按下白色除外）。
- MA3 的 `SendOSC` 只能走网络，所以由 PC 上的桥接程序把发到本机回环地址的
  OSC 数据报按 OSC 1.1 串口传输标准（SLIP，RFC 1055）转发给 ESP32-S3 的原生 USB 串口。

## OSC 消息协议

| 地址 | 类型标签 | 参数 | 作用 |
|------|----------|------|------|
| `/btn/<note>` | `,iii` 或 `,fff` | r g b（int 0–255 或 float 0–1） | 矩阵按钮 LED 颜色（note 70–89） |
| `/fader/<cc>` | `,iii` 或 `,fff` | r g b | 推子 LED 颜色（cc 46–55），亮度跟随推子电平 |

RGB 全 0 = 清除颜色覆盖，LED 恢复默认行为（蓝色/绿色激活态/电平亮度）。

## 配置步骤

### 1. ESP32 固件

`firmware/esp32_s3/esp32_s3.ino` 顶部确认：

```cpp
#define ENABLE_OSC_COLOR_FEEDBACK 1
#define OSC_USB_BAUD 115200
```

Arduino IDE 编译时选择 **Tools → USB CDC On Boot → Enabled**，
让 `Serial` 走 ESP32-S3 原生 USB 口。烧录后 ESP32-S3 在电脑上会枚举为一个串口设备
（Windows 为 `COMx`，macOS/Linux 为 `/dev/ttyACM*` 或 `/dev/cu.usbmodem*`）。

### 2. 运行 PC 桥接程序

1. 安装依赖：`pip install pyserial`。
2. 查找 ESP32-S3 的串口号：`python -m serial.tools.list_ports -v`。
3. 启动桥接（随 MA3 一起常驻运行，可做成开机自启/批处理）：

```
python tools/osc_usb_bridge.py --serial-port COM5
```

默认监听 `127.0.0.1:8000`，可用 `--listen-port` 修改。串口断开后自动重连。

### 3. MA3 onPC 的 OSC 输出

1. Menu → **In & Out → OSC**。
2. 新建一行：`Destination IP` = `127.0.0.1`，`Port` = 8000，`Mode` = UDP，勾选启用。
3. 记下该行编号（默认第 1 行）。

### 4. 导入并运行 Lua 插件

1. 将 `ma3_plugin/ColorFeedback.lua` 内容复制进新插件：
   - Menu → **Show Creator → Plugins**（或直接编辑 Plugin Pool）。
   - 新建 Plugin → 新建 Lua Component → 粘贴文件内容 → 保存。
2. 按需修改插件顶部 `CONFIG`：
   - `oscConfigSlot`：第 3 步记下的 OSC 行编号。
   - `buttonExecutors` / `faderExecutors`：Note/CC 与 executor 编号的映射（默认与 `docs/MIDI_MAPPING.md` 一致：按钮 70–89 → Exec 101–120，推子 46–55 → Exec 201–210）。
   - `pollInterval`：轮询间隔（默认 0.1 秒，颜色变化近乎实时；仅在颜色变化时才发送 OSC）。
3. 运行插件（点击 Plugin 池中的插件）开始推送颜色；再次运行可停止。

### 5. 给 executor 上色

在 MA3 中给 sequence 指定 Appearance（如 `Assign Appearance 1 At Sequence 5`），
executor 显示什么背景色，控制器对应的按键灯就同步显示该颜色。改变 Appearance 颜色，
LED 在一个轮询周期内（约 0.1 秒）更新。

## 验证

1. 桥接程序输出 `serial connected` 确认已连上 ESP32-S3。
2. 在电脑上用 OSC 工具（如 Protokol / TouchOSC）向 `127.0.0.1:8000` 手动发 `/btn/70 ,iii 255 0 0`，对应按钮应变红。
3. 运行 MA3 插件，修改某个 executor 的 Appearance 颜色，确认 LED 实时跟随。
4. 删除 executor 上的对象或清除 Appearance，确认 LED 恢复默认行为。

## 故障排查

| 现象 | 检查 |
|------|------|
| 桥接连不上串口 | 串口号是否正确；固件是否以 "USB CDC On Boot: Enabled" 编译；串口是否被 Arduino 串口监视器等其他程序占用 |
| LED 不变色 | 桥接程序是否在运行；MA3 OSC 行的 IP 是否为 127.0.0.1、端口 8000；防火墙是否放行本机 UDP 8000 |
| 部分按键不变色 | 插件 `CONFIG` 中 Note/CC → executor 映射是否与实际 show 一致 |
| 颜色不对 | MA3 版本的 Appearance 属性名差异；插件已尝试 BackR/BACKR 等多种写法，必要时按版本调整 `readAppearanceColor` |
| 插件报错 | MA3 System Monitor 中查看 `ColorFeedback:` 日志输出 |
