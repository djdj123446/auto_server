# StudySense — 基于 ESP32 的智能书房环境调节系统（固件端）

毕业设计项目 **StudySense** 的 ESP32 固件：以 ESP32 为主控，采集书房温湿度、光照与人体状态，在 OLED 上本地显示并通过四键键盘交互，同时通过 MQTT 接入**自建云平台**（EMQX + FastAPI），实现数据上报、远程控制、阈值同步与专注学习计时。

项目已从 ThingsCloud 迁移到自建服务端，本文档与当前固件代码保持一致。

## 系统组成

| 部分 | 目录 | 说明 |
| --- | --- | --- |
| 固件端（本文档） | `StudySense/` | ESP32 Arduino 工程，传感器采集、执行器控制、OLED 交互、MQTT 通信 |
| 服务端 | `StudySense-Server/` | FastAPI + MySQL + EMQX，另有 Vue 网页控制台 |

```
ESP32 ──MQTT(state/up、threshold/up、cmd/ack、status)──> EMQX ──> FastAPI ──> MySQL
  ↑                                                                  │
  └────────────── MQTT(cmd/down、threshold/down) ────────────────────┘
                                                HTTP / WebSocket ──> 网页
```

## 功能特性

### 环境感知
- **温湿度采集**：DHT11，每 2 秒读取一次
- **光照采集**：BH1750（I2C，单位 lx）
- **人体感应**：HC-SR501 红外检测，判断有人/无人；检测到人后保持 120 秒"在场"状态，避免人静止时灯光反复开关

### 执行控制
- **LED 照明**：3W LED 模块，GPIO 高电平点亮
- **风扇调速**：L9110S 驱动 + LEDC PWM（1 kHz / 8 位），四档：关闭 / 一档 / 二档 / 三档（占空比 0 / 60% / 80% / 100%）
- **自动模式**：
  - 温度 ≥ 温度上限（默认 28.0 °C）自动开风扇（一档），低于上限自动关闭
  - 有人且光照 ≤ 光照下限（默认 300 lx）自动开灯；无人或光照充足自动关灯
  - 当前实现为单阈值直接控制，不带回差；阈值可在设备菜单或网页修改
- **手动优先**：按键或云端直接操作过 LED / 风扇后，自动控制不再覆盖该执行器，直到下一次切换工作模式
- **手动模式**：按键或云端直接控制执行器；进入手动模式时关闭灯光与风扇

### 专注模式（学习计时）
- 主界面 **K4 长按**（或云端下发 `mode=2`）进入专注，设备端弹框确认后开始计时
- 专注中 K1 / K2 仍可正常控制风扇与灯光，不打断学习环境
- 每满 **休息提醒间隔**（默认 1 分钟，可调 1~120 分钟）先定格显示整分钟，再弹框提醒休息
- 专注中按 K3 / K4 打开"是否结束专注"弹框并**暂停计时**：K3 继续、K4 结束
- 休息提醒弹框期间计时继续，约 5 秒无人操作自动继续专注
- 进入专注、结束专注、休息提醒均有约 1 秒一声、共五声的蜂鸣提示
- 专注状态（`focus`、`focus_ms`、`focus_paused`、`focus_rest`）实时上报，网页显示与 OLED 一致的弹框和计时

### 云平台（自建 EMQX + FastAPI）
- MQTT 匿名接入自建 EMQX，ClientID 固定为 `esp32_01`
- 每 10 秒周期上报属性，状态变化（按键、云端指令执行）后立即补报一次
- 遗嘱消息（LWT）+ 上线 `online` 通知，服务端据此判定在线/离线
- 接收云端控制指令（LED / 风扇 / 模式 / 专注交互）并回复 ack
- 阈值双向同步：设备端改阈值后上报，网页保存阈值后通过 retain 消息下发，设备重启也能收到最新值
- MQTT 断开后每 5 秒重试连接，WiFi 断开由 ESP32 SDK 自动重连；离线期间本地显示与控制不受影响

### 人机交互
- 0.96" OLED（SSD1306 128×64，u8g2 驱动，I2C 400 kHz）：
  - **开机动画**：ESP32 ↔ 云平台数据流对接动画，展示 WiFi / MQTT 连接过程
  - **主界面**：温度、湿度、光照（带小图标）及人体 / LED / 风扇状态，标题栏显示工作模式
  - **专注界面**：大号计时 + 操作提示
  - **系统设置**：温度阈值、光照阈值、休息提醒间隔三页菜单
  - **弹框**：开始专注、是否结束专注、休息提醒、专注结束展示
- 四键键盘（按下并弹起计一次，带消抖；阈值页支持长按连续调整）：
  - 主界面：K1 风扇调档、K2 LED 开关、K3 进入设置、K4 短按切换手动/自动、K4 长按进入专注
  - 设置页：K2/K1 上下选择与加减、K3 进入、K4 返回
  - 专注弹框：K3 左侧操作（开始/继续）、K4 右侧操作（取消/结束）

## 硬件组成

| 模块 | 型号/说明 | 接线（ESP32 引脚） |
| --- | --- | --- |
| 主控 | ESP32 DevKit | — |
| 温湿度传感器 | DHT11 | DATA = GPIO4 |
| 光照传感器 | BH1750（I2C 地址 0x23） | SDA = GPIO21，SCL = GPIO22 |
| 人体红外 | HC-SR501 | OUT = GPIO13 |
| OLED 显示屏 | 0.96" SSD1306 128×64（I2C 地址 0x3C） | I2C（与 BH1750 共用） |
| LED 灯珠 | 3W LED 模块 | 信号 = GPIO14（高电平亮） |
| 风扇驱动 | L9110S | A/IN1 = GPIO18，B/IN2 = GPIO19 |
| 有源蜂鸣器 | 高电平触发 | GPIO27 |
| 独立键盘 | 4 键模块（低电平按下，内部上拉） | K1 = GPIO26，K2 = GPIO25，K3 = GPIO33，K4 = GPIO32 |

按键名称与功能对应：

| 按键 | 引脚 | 主界面功能 | 设置页功能 |
| --- | --- | --- | --- |
| K1 | GPIO26 | 风扇档位循环切换 | 上一项 / 增大阈值 |
| K2 | GPIO25 | LED 开关 | 下一项 / 减小阈值 |
| K3 | GPIO33 | 进入系统设置 | 进入选中项 |
| K4 | GPIO32 | 短按手动/自动切换，长按进入专注 | 返回 |

> 风扇建议使用独立 5V 电源供电并与 ESP32 共地；L9110S 的 A/B 两路输入分别接 GPIO18 / GPIO19，固件通过两路 PWM 控制正转与占空比。

## 软件结构

```
src/
├── main.cpp        # 主流程：初始化、开机动画、按键分发、主循环调度
├── config.h        # 引脚、WiFi、MQTT、时间常量等全部配置
├── sensors.*       # 传感器采集（DHT11 / BH1750 / PIR）
├── control.*       # 执行器、自动控制、工作模式、专注计时、蜂鸣器
├── display.*       # OLED 界面（开机动画 / 主界面 / 菜单 / 专注 / 弹框）
├── keys.*          # 四键扫描、消抖、长按与重复触发
├── wifi_conn.*     # WiFi 连接管理
└── mqtt_conn.*     # MQTT 连接、上报、指令与阈值下发处理
```

`platformio.ini` 已声明依赖：`olikraus/U8g2`、`adafruit/DHT sensor library`、`claws/BH1750`、`knolleary/PubSubClient`，并设置 `MQTT_MAX_PACKET_SIZE=512`、`huge_app.csv` 分区表。

## 快速开始

### 1. 环境准备

- 安装 [VS Code](https://code.visualstudio.com/) 与 [PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) 插件
- 使用 VS Code 打开本目录（`StudySense/`），等待 PlatformIO 自动安装依赖

### 2. 修改配置

先把配置模板复制为本地配置文件（`config.h` 已被 `.gitignore` 忽略，不会提交到仓库）：

```bash
# Windows
copy src\config.example.h src\config.h
# macOS / Linux
cp src/config.example.h src/config.h
```

然后编辑 `src/config.h`：

```c
// WiFi（ESP32 必须与运行服务端的电脑在同一局域网）
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"

// 自建 MQTT 服务（EMQX）：填电脑的局域网 IPv4 地址
#define MQTT_HOST "192.168.1.100"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "esp32_01"   // 与服务端 DEVICE_ID 一致
```

说明：

- `MQTT_HOST` 用 `ipconfig` 查看电脑"IPv4 地址"，不要填 `127.0.0.1`
- 固件使用固定主题 `studysense/esp32_01/...`，修改 ClientID 时需同步修改 `mqtt_conn.cpp` 中的主题与服务端 `DEVICE_ID`
- `config.h` 含有本地 WiFi 密码与电脑 IP，已加入 `.gitignore`，仓库只保留 `config.example.h` 模板

### 3. 编译烧录

PlatformIO 工具栏点击 **Upload**，或终端执行：

```bash
pio run -t upload
```

串口监视器波特率 **115200**。首次启动会依次初始化传感器、按键、执行器、WiFi 与 MQTT，OLED 显示对接动画，成功后进入主界面。

### 4. 启动顺序（联调推荐）

1. 电脑启动服务端依赖：MySQL 与 EMQX（见 `StudySense-Server/README.md`）
2. 启动 FastAPI 后端与网页控制台
3. 给 ESP32 上电，OLED 显示"云平台对接成功"及 IP
4. 网页右上角显示"设备在线"，仪表盘出现实时数据即接通

没有硬件时可用服务端自带的 `backend/sim_device.py` 模拟设备上报。

## 云端通信说明

主题与 JSON 契约详见 [`StudySense-Server/ESP32固件对接规范.md`](../StudySense-Server/ESP32固件对接规范.md)。

### 主题规划

| 方向 | Topic | QoS | Retain | 用途 |
| --- | --- | --- | --- | --- |
| 设备→云 | `studysense/esp32_01/state/up` | 1 | 否 | 每 10 秒上报传感器与设备状态 |
| 设备→云 | `studysense/esp32_01/threshold/up` | 1 | 否 | 设备端修改阈值后上报 |
| 设备→云 | `studysense/esp32_01/cmd/ack` | 1 | 否 | 云端指令执行回执 |
| 设备→云 | `studysense/esp32_01/status` | 1 | 是 | 在线状态（上线 `online`，遗嘱 `offline`） |
| 云→设备 | `studysense/esp32_01/cmd/down` | 1 | 否 | 控制指令 |
| 云→设备 | `studysense/esp32_01/threshold/down` | 1 | 是 | 阈值下发（保留消息） |

### 状态上报示例

```json
{
  "temperature": 26.5,
  "humidity": 58,
  "light": 312,
  "people": true,
  "led": 1,
  "fan": 2,
  "mode": 1,
  "focus": 0,
  "focus_ms": 0,
  "focus_paused": 0,
  "focus_rest": 0
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| temperature | 浮点 | 温度（°C） |
| humidity | 浮点/整数 | 湿度（%） |
| light | 整数 | 光照（lx） |
| people | 布尔 | true=有人 / false=无人 |
| led | 整数 | 0=关 / 1=开 |
| fan | 整数 | 0=关闭，1/2/3=一/二/三档 |
| mode | 整数 | 0=手动 / 1=自动 / 2=专注 |
| focus | 整数 | 专注已计时秒数（非专注为 0） |
| focus_ms | 整数 | 专注已计时毫秒数（精确值，供前端插值） |
| focus_paused | 整数 | 1=设备端"是否结束专注"弹框打开中（计时暂停） |
| focus_rest | 整数 | 1=设备端休息提醒弹框打开中（计时继续） |

### 控制指令示例

```json
{"cmd": "led", "value": 1}
{"cmd": "fan", "value": 2}
{"cmd": "mode", "value": 0}
{"cmd": "focus_pause"}
{"cmd": "focus_continue"}
{"cmd": "focus_end", "value": 0}
```

设备执行后回复 `{"echo": <原指令>, "ok": true}`，并立即补报一次状态。

### 阈值同步示例

```json
{"temp_max": 28.0, "light_min": 150, "rest_min": 30}
```

- `temp_max`：温度上限（°C）
- `light_min`：光照下限（lx）
- `rest_min`：专注休息提醒间隔（分钟，1~120）

## 自动控制与手动优先

| 场景 | 行为 |
| --- | --- |
| 自动模式，温度 ≥ 上限 | 打开风扇一档（若风扇未被手动/云端接管） |
| 自动模式，温度 < 上限 | 关闭风扇（同上） |
| 自动模式，有人且光照 ≤ 下限 | 打开 LED（若 LED 未被手动/云端接管） |
| 自动模式，无人或光照 > 下限 | 关闭 LED（同上） |
| 按键 / 云端操作过风扇或灯光 | 对应执行器标记为"手动优先"，自动控制不再覆盖 |
| 切换工作模式 | 恢复自动接管权；切到手动模式时关闭灯光与风扇 |

## 常见问题

- **MQTT 连不上**：确认 ESP32 与电脑同一 WiFi、`MQTT_HOST` 是电脑局域网 IP、EMQX 已启动且 1883 端口未被防火墙拦截；串口会打印连接返回码
- **WiFi 连不上**：8 秒超时后进入离线模式，本地显示与控制仍然可用，恢复网络后自动重连
- **网页一直显示设备离线**：检查 ClientID 是否为 `esp32_01`、`status` 主题是否收到 retain 的 `online`、服务端 `DEVICE_ID` 是否一致
- **OLED 不亮**：检查 I2C 接线（SDA=21、SCL=22）、供电与地址 0x3C；BH1750 地址为 0x23
- **DHT11 读数异常**：DHT11 两次读取间隔需 ≥ 1 秒，固件已按 2 秒读取；检查上拉电阻与线长
- **风扇不转或只响不转**：检查 L9110S 的 A/B 接线、风扇独立供电与共地；三档占空比分别为 60% / 80% / 100%
- **阈值改了但网页不同步**：设备需在线；网页保存的阈值以 retain 消息下发，设备重启后仍会收到
- **休息提醒太频繁**：默认间隔 1 分钟，可在设备"系统设置 → 休息提醒"或网页设置页调到 1~120 分钟
- **编译提示空间不足**：项目已使用 `huge_app.csv` 分区表，若自行添加大体积库需重新评估 Flash 分区

## 相关项目

- 服务端与网页控制台：`../StudySense-Server/`
- MQTT 对接规范：`../StudySense-Server/ESP32固件对接规范.md`
