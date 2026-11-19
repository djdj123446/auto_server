# StudySense ESP32 固件对接规范

> 把固件从 ThingsCloud 迁移到自建平台，只需要改 **Broker 地址、Topic、JSON 格式** 三处。
> 本文按 Arduino (PubSubClient) 写法说明，其他框架同理。

## 1. 连接参数

| 项目 | 值 |
|---|---|
| Broker 地址 | 你电脑的局域网 IP（`ipconfig` 查看"IPv4 地址"，如 `192.168.1.100`） |
| 端口 | `1883`（TCP，不用 TLS） |
| ClientID | `esp32_01`（固定，别改） |
| 用户名/密码 | 无（EMQX 当前允许匿名接入） |
| KeepAlive | 15 秒 |

⚠️ ESP32 必须和电脑连**同一个 WiFi**。

## 2. Topic 规划

| 方向 | Topic | QoS | Retain | 用途 |
|---|---|---|---|---|
| 设备→云 | `studysense/esp32_01/state/up` | 1 | 否 | 每 10 秒上报传感器数据 |
| 设备→云 | `studysense/esp32_01/threshold/up` | 1 | 否 | 设备端改了阈值后上报 |
| 设备→云 | `studysense/esp32_01/cmd/ack` | 1 | 否 | 执行云端指令后的回执 |
| 设备→云 | `studysense/esp32_01/status` | 1 | **是** | 在线状态（上线发 `online`，遗嘱发 `offline`） |
| 云→设备 | `studysense/esp32_01/cmd/down` | 1 | 否 | 控制指令（LED/风扇/模式） |
| 云→设备 | `studysense/esp32_01/threshold/down` | 1 | **是** | 阈值下发（保留消息） |

## 3. JSON 字段定义

### state/up（每 10 秒）

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
  "focus_paused": 0,
  "focus_rest": 0
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| temperature | 浮点 | 温度（°C） |
| humidity | 浮点/整数 | 湿度（%） |
| light | 整数 | 光照（lx） |
| people | 布尔 | true=有人 / false=无人 |
| led | 整数 | 0=关 / 1=开 |
| fan | 枚举 | 0=关闭，1/2/3=一/二/三档风速 |
| mode | 整数 | 1=自动 / 0=手动 / 2=专注 |
| focus | 整数 | 专注模式已计时秒数（非专注模式为 0） |
| focus_paused | 整数 | 1=设备端"是否结束专注"弹框打开中（计时已暂停） |
| focus_rest | 整数 | 1=设备端休息提醒弹框打开中（计时继续，超时自动关闭） |

> `focus_paused` / `focus_rest` 变化时必须**立即**上报一次（不能等 10 秒周期上报），
> 否则网页来不及弹出/收起对应弹框（休息提醒弹框仅存在约 5 秒）。

### cmd/down（云端下发，收到后回 ack）

```json
{"cmd": "led", "value": 1}
{"cmd": "fan", "value": 2}
{"cmd": "mode", "value": 0}
{"cmd": "focus_pause"}
{"cmd": "focus_continue"}
{"cmd": "focus_end", "value": 0}
```

| 指令 | 说明 | 对应设备端操作 |
|---|---|---|
| mode | 0=手动 / 1=自动 / 2=专注（进专注即开始计时） | K4 短按 / 长按 |
| focus_pause | 专注中打开"是否结束专注"弹框并**暂停计时** | 专注中按 K3/K4 |
| focus_continue | 弹框中选择"继续"：恢复计时并关框 | 弹框内按 K3 |
| focus_end | 弹框中选择"结束计时"：冻结本次时长，切到 `value` 指定模式（0=手动 1=自动），并展示结束页 | 弹框内按 K4 |

> 三条 focus_* 指令只做"与设备端按键等价"的动作，**弹框状态仍以设备为准**：
> 设备执行后立即上报 `focus_paused` / `focus_rest`，网页据此显示同一个弹框，
> 因此网页与 OLED 不会出现一端弹框、另一端没弹的情况。

### ack（设备执行后回复）

```json
{"echo": {"cmd": "fan", "value": 2}, "ok": true}
```

### threshold/down（网页保存阈值后下发；retain，重启也能收到最新值）/ threshold/up（设备端改完上报，网页同步刷新）

```json
{"temp_max": 28.0, "light_min": 150, "rest_min": 30}
```

| 字段 | 说明 |
|---|---|
| temp_max | 温度上限（°C） |
| light_min | 光照下限（lx） |
| rest_min | 专注休息提醒间隔（分钟，1~120），设备端系统菜单"休息提醒"可同步调整 |

字段名**以你固件里已有的阈值变量为准**，告诉我后我在文档里补全。收到 `threshold/down` 后请存入全局变量并同步到 OLED 显示。

## 4. 关键代码片段（PubSubClient）

### 连接（含遗嘱消息 LWT）

```cpp
#include <PubSubClient.h>

const char* MQTT_HOST = "192.168.1.100";  // 改成你电脑的 IP
const int   MQTT_PORT = 1883;
const char* CLIENT_ID = "esp32_01";

// 遗嘱：异常掉线时 broker 自动替你发 offline（retain）
bool connectMqtt(PubSubClient& mqtt) {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  if (mqtt.connect(CLIENT_ID,
                    NULL, NULL,                              // 用户名/密码
                    "studysense/esp32_01/status", 1, true,    // willTopic, QoS, retain
                    "offline")) {
    mqtt.publish("studysense/esp32_01/status", "online", true);  // 上线通知
    mqtt.subscribe("studysense/esp32_01/cmd/down", 1);
    mqtt.subscribe("studysense/esp32_01/threshold/down", 1);
    return true;
  }
  return false;
}
```

### 上报

```cpp
void publishState(PubSubClient& mqtt) {
  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"temperature\":%.1f,\"humidity\":%.0f,\"light\":%d,"
    "\"people\":%s,\"led\":%d,\"fan\":%d,\"mode\":%d}",
    temperature, humidity, light,
    people ? "true" : "false", led, fan, mode);
  mqtt.publish("studysense/esp32_01/state/up", buf);
}
```

### 收指令回调

```cpp
void onMessage(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, len)) return;

  String t = topic;
  if (t == "studysense/esp32_01/cmd/down") {
    String cmd = doc["cmd"];
    if (cmd == "led")  setLed(doc["value"]);
    if (cmd == "fan")  setFan(doc["value"]);
    if (cmd == "mode") setMode(doc["value"]);
    // 回执
    String ack;
    serializeJson(doc, ack);
    mqtt.publish("studysense/esp32_01/cmd/ack",
      ("{\"echo\":" + ack + ",\"ok\":true}").c_str());
  }
  else if (t == "studysense/esp32_01/threshold/down") {
    // 遍历 doc 里的字段更新对应全局阈值变量，并刷新 OLED
    loadThreshold(doc.as<JsonObject>());
  }
}
```

## 5. 联调步骤

1. 电脑上启动：`docker compose up -d`（EMQX + MySQL）
2. 启动后端：`backend\.venv\Scripts\python.exe backend\run.py`
3. 启动前端：`frontend` 目录 `npm run dev`，浏览器开 http://localhost:5173
4. 固件烧录后，网页右上角看"设备在线"，仪表盘出数据即通
5. 浏览器 EMQX 管理界面 http://localhost:18083（admin/public）可看报文调试

## 6. 行为约定

- **数据上报周期**：10 秒（网页 30 秒收不到上报会判定离线）
- **状态变化立即上报**：设备端按键改变 led / fan / mode 后必须**立即**上报一次 state/up（不等 10 秒周期），云端指令执行后同理；这样物理按键操作在网页上毫秒级可见
- **模式语义**：自动(1)下设备按阈值自调；手动(0)下接受 cmd 指令。网页下发指令不受模式限制，但自动模式下设备可在下一轮自检覆盖
- **专注模式**：`mode=2` 时设备开始计时，满 `rest_min` 提醒休息；「是否结束专注」弹框期间计时暂停，「休息提醒」弹框期间计时继续且约 5 秒无人操作自动继续。两种弹框都由设备端产生并上报 `focus_paused` / `focus_rest`，网页只做镜像显示
- **阈值双端同步**：任何一端修改（OLED 按键 / 网页）都要发消息，对端收到后更新自身显示
- **离线判定**：遗嘱消息 + 30 秒超时双保险
