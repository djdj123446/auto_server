#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"
#include "mqtt_conn.h"
#include "sensors.h"
#include "control.h"
#include "display.h"

// main.cpp 中的周期上报计时（指令执行后重置，避免紧跟周期上报重复发送）
extern unsigned long lastMqttReport;

// ======================================================
// MQTT 客户端
// ======================================================

WiFiClient mqttWifiClient;

PubSubClient mqttClient(mqttWifiClient);

// ======================================================
// 订阅主题（详见《ESP32 固件对接规范》）
// ======================================================

// 设备 → 云：每 10 秒上报传感器数据
#define TOPIC_STATE_UP "studysense/esp32_01/state/up"

// 设备 → 云：设备端改了阈值后上报
#define TOPIC_THRESHOLD_UP "studysense/esp32_01/threshold/up"

// 设备 → 云：执行云端指令后的回执
#define TOPIC_CMD_ACK "studysense/esp32_01/cmd/ack"

// 设备 → 云：在线状态（上线发 online，遗嘱发 offline，retain）
#define TOPIC_STATUS "studysense/esp32_01/status"

// 云 → 设备：控制指令（LED/风扇/模式）
#define TOPIC_CMD_DOWN "studysense/esp32_01/cmd/down"

// 云 → 设备：阈值下发（保留消息，重启也能收到最新值）
#define TOPIC_THRESHOLD_DOWN "studysense/esp32_01/threshold/down"

// ======================================================
// 轻量 JSON 解析（获取数字值）
// ======================================================

static float jsonValueNumber(const char *json, const char *key, float def)
{
    char pattern[48];

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);

    if (!p)
    {
        return def;
    }

    p = strchr(p, ':');

    if (!p)
    {
        return def;
    }

    p++;

    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    return atof(p);
}

// ======================================================
// 轻量 JSON 解析（获取整型值）
// ======================================================

static int jsonValueInt(const char *json, const char *key, int def)
{
    return (int)jsonValueNumber(json, key, def);
}

// ======================================================
// 轻量 JSON 解析（获取字符串值，返回其长度，未找到返回 -1）
// ======================================================

static int jsonValueString(const char *json, const char *key, char *out, int outLen)
{
    char pattern[48];

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);

    if (!p)
    {
        return -1;
    }

    p = strchr(p, ':');

    if (!p)
    {
        return -1;
    }

    p = strchr(p, '"');

    if (!p)
    {
        return -1;
    }

    p++;

    const char *end = strchr(p, '"');

    if (!end)
    {
        return -1;
    }

    int len = end - p;

    if (len >= outLen)
    {
        len = outLen - 1;
    }

    memcpy(out, p, len);

    out[len] = '\0';

    return len;
}

// ======================================================
// 应用云端下发的阈值到本地
// temp_max=温度上限 / light_min=光照下限
// rest_min=专注休息提醒间隔（分钟）
// ======================================================

static bool applyThresholds(const char *json)
{
    bool changed = false;

    if (strstr(json, "\"temp_max\""))
    {
        temperatureThreshold = jsonValueNumber(json, "temp_max", temperatureThreshold);

        changed = true;

        Serial.print("[云端] 温度上限：");
        Serial.println(temperatureThreshold);
    }

    if (strstr(json, "\"light_min\""))
    {
        lightThreshold = jsonValueNumber(json, "light_min", lightThreshold);

        changed = true;

        Serial.print("[云端] 光照下限：");
        Serial.println(lightThreshold);
    }

    if (strstr(json, "\"rest_min\""))
    {
        int v = jsonValueInt(json, "rest_min", restRemindMin);

        if (v >= REST_REMIND_MIN_MIN && v <= REST_REMIND_MAX_MIN && v != restRemindMin)
        {
            setRestRemindMin(v);

            changed = true;

            Serial.print("[云端] 休息提醒间隔（分钟）：");
            Serial.println(restRemindMin);
        }
    }

    if (changed)
    {
        // 刷新 OLED 显示
        updateDisplay();

        // 自动模式下云端修改了阈值，重新执行一次自动控制
        automaticControl();
    }

    return changed;
}

// ======================================================
// MQTT 消息回调
// ======================================================

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String data;

    for (unsigned int i = 0; i < length; i++)
    {
        data += (char)payload[i];
    }

    const char *json = data.c_str();

    Serial.print("[MQTT] 收到消息，主题：");
    Serial.print(topic);
    Serial.print("，内容：");
    Serial.println(data);

    // ==================================================
    // 控制指令下发：studysense/esp32_01/cmd/down
    // 格式：{"cmd":"led"/"fan"/"mode","value":x}
    //       {"cmd":"focus_pause"} / {"cmd":"focus_continue"}
    //       {"cmd":"focus_end","value":0}  （0=结束后回手动 1=回自动）
    // ==================================================

    if (strcmp(topic, TOPIC_CMD_DOWN) == 0)
    {
        char cmd[16] = "";

        jsonValueString(json, "cmd", cmd, sizeof(cmd));

        if (strcmp(cmd, "fan") == 0)
        {
            setFanLevel(jsonValueInt(json, "value", 0));

            // 云端指令优先：自动控制不再覆盖风扇
            fanAutoAllowed = false;

            Serial.println("[云端指令] 风扇：" + String(fanLevelName(fanLevel)));
        }
        else if (strcmp(cmd, "led") == 0)
        {
            lightState = jsonValueNumber(json, "value", 0) != 0;

            // 云端指令优先：自动控制不再覆盖灯光
            lightAutoAllowed = false;

            Serial.println("[云端指令] 灯光：" + String(lightState ? "开" : "关"));
        }
        else if (strcmp(cmd, "mode") == 0)
        {
            int value = jsonValueInt(json, "value", 0);

            // 云端切模式时先静音本地未响完的提醒音/提示音，再按需发新提示
            buzzerSilence();

            if (value == 2)
            {
                // 云端进入专注模式：直接开始计时
                // （无人在设备旁按键，跳过"是否计时"弹框）
                setWorkMode(MODE_FOCUS);

                focusStart();

                // 进入专注提示音：响五声
                buzzerBeep(5);

                Serial.println("[云端指令] 模式：专注（计时开始）");
            }
            else
            {
                // 记录是否从专注模式切走（用于结束提示音）
                bool wasFocus = (currentMode == MODE_FOCUS);

                // 从专注切走：冻结本次时长并复位暂停态，
                // 否则 focusRunning 仍为 true，网页弹框状态会一直上报为"暂停中"
                if (wasFocus)
                {
                    focusStop();
                }

                setWorkMode(value != 0 ? MODE_AUTO : MODE_MANUAL);

                // 专注被云端结束时同样响五声提示
                if (wasFocus)
                {
                    buzzerBeep(5);
                }

                Serial.println("[云端指令] 模式：" + String(currentMode == MODE_AUTO ? "自动" : "手动"));
            }

            // 模式由云端切换时，关闭本地可能存在的弹框并回到主界面
            currentDialog = DIALOG_NONE;

            currentPage = PAGE_MAIN;
        }
        else if (strcmp(cmd, "focus_pause") == 0)
        {
            // 网页点击模式卡片：打开"是否结束专注"弹框并暂停计时
            // （与设备端专注中按 K3/K4 的行为完全一致）
            if (currentMode == MODE_FOCUS && currentDialog != DIALOG_FOCUS_REST)
            {
                currentDialog = DIALOG_FOCUS_END;

                currentPage = PAGE_DIALOG;

                focusPause();

                Serial.println("[云端指令] 专注：询问是否结束（计时暂停）");
            }
        }
        else if (strcmp(cmd, "focus_continue") == 0)
        {
            // 网页选择"继续"：恢复计时并关闭弹框（等价设备端 K3）
            if (currentDialog == DIALOG_FOCUS_END ||
                currentDialog == DIALOG_FOCUS_REST)
            {
                focusResume();

                // 休息提醒弹框：静音残留提醒音并顺延下一个提醒周期
                if (currentDialog == DIALOG_FOCUS_REST)
                {
                    buzzerSilence();

                    focusRestPostpone();
                }

                currentDialog = DIALOG_NONE;

                currentPage = PAGE_MAIN;

                Serial.println("[云端指令] 专注：继续计时");
            }
        }
        else if (strcmp(cmd, "focus_end") == 0)
        {
            // 网页选择"结束计时"：冻结时长后切到目标模式（等价设备端 K4，
            // 区别是网页按点击的卡片决定结束后进入手动(0)还是自动(1)）
            if (currentMode == MODE_FOCUS)
            {
                int target = jsonValueInt(json, "value", 0);

                // 结束提示音：响五声（会重置未响完的提醒音序列）
                buzzerBeep(5);

                focusStop();

                unsigned long s = focusElapsedMs() / 1000;

                Serial.printf("[云端指令] 专注结束，本次学习时长 %02lu:%02lu:%02lu\n",
                              s / 3600, (s / 60) % 60, s % 60);

                setWorkMode(target == 1 ? MODE_AUTO : MODE_MANUAL);

                // 与设备端本地结束一致：展示本次时长，
                // 按 K3/K4 返回或提示音响完最后一声自动返回主界面
                currentDialog = DIALOG_FOCUS_DONE;

                currentPage = PAGE_DIALOG;
            }
        }
        else
        {
            Serial.println("[云端指令] 未知命令：" + String(cmd));

            return;
        }

        updateDisplay();

        // 回执：{"echo":<原指令>,"ok":true}
        String ack = "{\"echo\":" + data + ",\"ok\":true}";

        mqttClient.publish(TOPIC_CMD_ACK, ack.c_str());

        // 指令执行后立即上报一次状态（与本地按键行为一致），
        // 让网页仪表盘马上看到 LED/风扇/模式的真实变化，
        // 而不是等下一个周期上报（最长 10 秒）
        lastMqttReport = millis();

        mqttReportAttributes();

        return;
    }

    // ==================================================
    // 阈值下发：studysense/esp32_01/threshold/down
    // 格式：{"temp_max":28.0,"light_min":150}
    // ==================================================

    if (strcmp(topic, TOPIC_THRESHOLD_DOWN) == 0)
    {
        applyThresholds(json);

        return;
    }
}

// ======================================================
// 是否已连接云平台
// ======================================================

bool mqttConnected()
{
    return mqttClient.connected();
}

// ======================================================
// 初始化 MQTT 客户端
// ======================================================

void mqttBegin()
{
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);

    mqttClient.setCallback(mqttCallback);

    mqttClient.setKeepAlive(MQTT_KEEPALIVE_S);
    // 注：限制"云平台不可达时的阻塞"在 mqttLoop 中通过
    // WiFiClient::connect(host, port, 1000) 的 TCP 预连接实现
    // （WiFiClient::setTimeout 的参数单位是秒，且只影响读写
    //   超时，不限制 connect 的阻塞时长，这里不使用）
}

// 最近一次尝试连接的时间（用于限制重连频率）
static unsigned long lastMqttAttempt = 0;

// ======================================================
// 保持连接并处理收发消息
// ======================================================

void mqttLoop()
{
    if (!mqttClient.connected())
    {
        // 未连接网络时不尝试，避免无意义的重连
        if (WiFi.status() != WL_CONNECTED)
        {
            return;
        }

        // 控制重连频率：间隔 5 秒，避免频繁尝试
        if (millis() - lastMqttAttempt < 5000)
        {
            mqttClient.loop();

            return;
        }

        lastMqttAttempt = millis();

        // ===============================================
        // TCP 预连接（最多等 1 秒）：
        // PubSubClient 内部用的底层 TCP 连接是无超时的，
        // 云平台不可达时 socket 会长时间阻塞，把按键扫描等
        // 主循环全部卡住。这里先手动建链并限时：
        //  - 连不上：直接跳过本次，下一轮再试
        //  - 连上：再走 MQTT 握手（此时底层已连接，
        //    PubSubClient 的 connect() 不会再发起 TCP 连接，
        //    不会再次阻塞）
        // ===============================================

        if (mqttWifiClient.connect(MQTT_HOST, MQTT_PORT, 1000) != 1)
        {
            return;
        }

        // 自建平台允许匿名接入（无用户名/密码）
        // 遗嘱消息（LWT）：ESP32 突然断电/断网等异常掉线时，
        // 由 MQTT 服务器自动代发 offline 到 status 主题（retain）
        if (mqttClient.connect(MQTT_CLIENT_ID,
                               NULL, NULL,
                               TOPIC_STATUS, 1, true, "offline"))
        {
            Serial.println("MQTT 连接自建云平台成功！");

            // 上线通知（retain，网页据此判定在线）
            mqttClient.publish(TOPIC_STATUS, "online", true);

            // 订阅下发主题（QoS 1）
            mqttClient.subscribe(TOPIC_CMD_DOWN, 1);
            mqttClient.subscribe(TOPIC_THRESHOLD_DOWN, 1);
        }
        else
        {
            Serial.print("MQTT 连接失败，返回码：");
            Serial.println(mqttClient.state());

            // 握手失败：主动断开 TCP，避免"TCP 已连但 MQTT 层
            // 未完成握手"的僵尸连接，导致 mqttClient.connected()
            // 恒为 true 而永不重连
            mqttWifiClient.stop();
        }
    }
    else
    {
        mqttClient.loop();
    }
}

// ======================================================
// 上报传感器数据到云平台（每 10 秒）
// ======================================================

void mqttReportAttributes()
{
    if (!mqttClient.connected())
    {
        return;
    }

    // mode：1=自动 0=手动 2=专注
    // fan：0=关闭 1=一档 2=二档 3=三档（即 fanLevel）
    // focus：专注模式已计时秒数（非专注模式为 0），网页端据此显示实时专注计时
    // focus_ms：专注模式已计时毫秒数（精确值，非专注为 0），
    //   供网页/小程序用毫秒精度插值，消除整数秒截断造成的三端计时相位差
    // focus_paused：1=设备端"是否结束专注"弹框打开中（计时暂停）
    // focus_rest：1=设备端休息提醒弹框打开中（计时继续）
    // 网页据此弹出与 OLED 完全相同的弹框，两端保持同步
    // people 用布尔 true/false，led 用 0/1
    char payload[320];

    int modeValue = (currentMode == MODE_AUTO)    ? 1
                    : (currentMode == MODE_FOCUS) ? 2
                                                  : 0;

    unsigned long focusMs = (currentMode == MODE_FOCUS)
                                ? focusElapsedMs()
                                : 0UL;

    unsigned long focusSec = focusMs / 1000UL;

    int focusPausedValue = focusIsPaused() ? 1 : 0;

    int focusRestValue =
        (currentMode == MODE_FOCUS && currentDialog == DIALOG_FOCUS_REST) ? 1 : 0;

    snprintf(payload,
             sizeof(payload),
             "{\"temperature\":%.1f,\"humidity\":%.0f,\"light\":%.0f,"
             "\"people\":%s,\"led\":%d,\"fan\":%d,\"mode\":%d,\"focus\":%lu,"
             "\"focus_ms\":%lu,\"focus_paused\":%d,\"focus_rest\":%d}",
             temperature,
             humidity,
             lux,
             peopleDetected ? "true" : "false",
             lightState ? 1 : 0,
             fanLevel,
             modeValue,
             focusSec,
             focusMs,
             focusPausedValue,
             focusRestValue);

    mqttClient.publish(TOPIC_STATE_UP, payload);
}

// ======================================================
// 上报阈值到云平台（设备端改了阈值后调用，网页同步刷新）
// ======================================================

void mqttReportThresholds()
{
    if (!mqttClient.connected())
    {
        return;
    }

    char payload[128];

    snprintf(payload,
             sizeof(payload),
             "{\"temp_max\":%.1f,\"light_min\":%.0f,\"rest_min\":%d}",
             temperatureThreshold,
             lightThreshold,
             restRemindMin);

    mqttClient.publish(TOPIC_THRESHOLD_UP, payload);
}
