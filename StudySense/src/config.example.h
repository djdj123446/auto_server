#ifndef CONFIG_H
#define CONFIG_H

// ======================================================
// 配置模板：复制本文件为同目录下的 config.h 后再修改
//   Windows: copy src\config.example.h src\config.h
//   macOS/Linux: cp src/config.example.h src/config.h
// config.h 含本地 WiFi 密码与电脑 IP，已在 .gitignore 中忽略，不会提交
// ======================================================

// ======================================================
// 系统版本
// ======================================================

#define SYSTEM_VERSION "V1.2"

// ======================================================
// 引脚定义
// ======================================================

// DHT11 温湿度传感器
#define DHT_PIN 4
#define DHT_TYPE DHT11

// HC-SR501 人体红外感应模块
#define PIR_PIN 13

// 4位独立键盘模块
#define KEY_UP 25
#define KEY_DOWN 26
#define KEY_OK 32
#define KEY_BACK 33

// I2C 总线（OLED 和 BH1750 共用）
#define I2C_SDA 21
#define I2C_SCL 22

// ======================================================
// 执行器引脚
// ======================================================

// 3W LED 灯珠模块（S: 控制信号，高电平亮、低电平灭）
#define LED_PIN 14

// L9110S 电机驱动板（风扇），A/INA、B/INB 两个输入
// 正转：A 低、B 高；反转：A 高、B 低；停止：A、B 均低
#define FAN_IN1 18
#define FAN_IN2 19

// 有源蜂鸣器（高电平响、低电平停）
// 若为低电平触发模块，请对调蜂鸣器响/停电平
#define BUZZER_PIN 27

// ======================================================
// WiFi 配置（改成自己的WiFi名称和密码）
// ======================================================

#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"

// WiFi 连接超时（毫秒），超时后离线运行
#define WIFI_TIMEOUT_MS 8000

// ======================================================
// 自建云平台 MQTT 配置（EMQX Broker，详见对接规范文档）
// ======================================================

// Broker 地址：后端电脑的局域网 IP（须与 ESP32 同一 WiFi）
#define MQTT_HOST "192.168.1.100"

// MQTT 端口（TCP，不用 TLS，EMQX 允许匿名接入）
#define MQTT_PORT 1883

// MQTT ClientId：固定为 esp32_01
#define MQTT_CLIENT_ID "esp32_01"

// MQTT 心跳间隔（秒）
#define MQTT_KEEPALIVE_S 15

// MQTT 连接超时（毫秒），超时后离线运行
#define MQTT_CONNECT_TIMEOUT_MS 10000

// 状态上报周期（毫秒）
#define MQTT_REPORT_INTERVAL_MS 10000

// ======================================================
// 时间常量
// ======================================================

// 人体感应保持窗口（毫秒）：
// PIR 检测到人后，即使人静止不动（PIR 输出变低），
// 只要没有超时，仍视为"有人在场"，灯光保持开启。
// 人稍有动作 PIR 会再次触发并刷新该窗口。
#define LIGHT_PEOPLE_HOLD_MS 120000

// 传感器读取间隔（毫秒）
#define SENSOR_INTERVAL_MS 2000

// 专注模式休息提醒间隔默认值（毫秒）：满该周期弹框提醒注意休息，
// 可选择结束专注或继续（继续后每再满该周期提醒一次）。
// 网页端可下发 rest_min（分钟）覆盖默认值，设备重启后回到此默认。
#define FOCUS_REST_REMIND_MS (1UL * 60UL * 1000UL)

// 休息提醒间隔可调范围（分钟）：设备端菜单与网页端滑条使用同一范围
#define REST_REMIND_MIN_MIN 1
#define REST_REMIND_MAX_MIN 120

// 休息提醒到期后整分钟计时定格展示时长（毫秒）：
// 满一个提醒周期时先让屏上计时显示满分钟数（如 01:00）片刻，
// 再弹休息提醒框，避免 01:00 一闪而过看不清
#define FOCUS_REST_HOLD_MS (1UL * 1000UL)

// 休息提醒弹框无人操作自动继续时长（毫秒）：
// 休息提醒弹框打开后超过该时长无人按键，自动继续专注。
// 与提醒蜂鸣时长（五声约 5 秒）一致，响完弹框即自动关闭
#define FOCUS_REST_AUTO_CONTINUE_MS (5UL * 1000UL)

// 按键消抖：两次完整按键之间的最小间隔（毫秒）
#define KEY_DEBOUNCE_MS 180

// 按键最短按压时长（毫秒），按下并弹起才算一次完整按键
// 短于该时长的按压视为抖动丢弃
#define KEY_MIN_PRESS_MS 50

// 长按重复：按住超过该时长后开始连续触发按键事件
#define KEY_REPEAT_DELAY_MS 500

// 长按事件：按住超过该时长触发一次长按事件（用于 K4 长按进入专注模式）
#define KEY_LONG_PRESS_MS 800

// 长按重复：进入连续触发后的触发间隔（毫秒）
// 用于阈值调整长按连续增大/减小
#define KEY_REPEAT_INTERVAL_MS 100

#endif
