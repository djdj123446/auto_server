#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "sensors.h"
#include "control.h"
#include "display.h"
#include "keys.h"
#include "wifi_conn.h"
#include "mqtt_conn.h"

// ======================================================
// 函数声明
// ======================================================

void handleKey(int key);
void handleDialogKey(int key);
void printStatus();
void waitForCloud();

// ======================================================
// 传感器读取计时
// ======================================================

unsigned long lastSensorRead = 0;

// MQTT 上报计时（与传感器读取分开，避免触发平台流控）
unsigned long lastMqttReport = 0;

// ======================================================
// 初始化
// ======================================================

void setup()
{
    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println("=============================");
    Serial.println(" StudySense 智能书房环境系统 ");
    Serial.println("=============================");

    // ==================================================
    // I2C 总线（OLED 和 BH1750 共用）
    // ==================================================

    Wire.begin(I2C_SDA, I2C_SCL);

    // ==================================================
    // 开机动态对接动画（MCU <-> 自建云平台）
    // ==================================================

    displayInit();

    int bootFrame = 0;

    // 初始化传感器 + 数据上传动画
    for (int i = 0; i < 15; i++)
    {
        drawCloudConnect(bootFrame++, "读取传感器");
        delay(50);
    }
    sensorsInit();

    // 初始化按键
    for (int i = 0; i < 15; i++)
    {
        drawCloudConnect(bootFrame++, "初始化按键");
        delay(50);
    }
    keysInit();

    // ==================================================
    // 初始化执行器（LED + 风扇）
    // ==================================================

    actuatorsInit();

    // 启动 WiFi
    for (int i = 0; i < 15; i++)
    {
        drawCloudConnect(bootFrame++, "启动WiFi");
        delay(50);
    }
    wifiBegin();

    // 初始化 MQTT 客户端（动画在 waitForCloud 网络判定后显示）
    mqttBegin();

    // ==================================================
    // 对接云平台（WiFi + 云平台连接成功后进入主界面）
    // ==================================================

    waitForCloud();

    // ==================================================
    // 进入主界面
    // ==================================================

    readSensors();

    currentPage = PAGE_MAIN;

    updateDisplay();

    Serial.println("系统启动完成！");
    Serial.println("当前模式：手动模式");
}

// ======================================================
// 对接云平台：等待 WiFi 连接后，连接自建 EMQX MQTT
// ======================================================

void waitForCloud()
{
    unsigned long startTime = millis();

    int frame = 0;

    bool wifiOk = false;

    // ==================================================
    // 阶段1：等待 WiFi 连接（正在接入网络）
    // ==================================================

    while (true)
    {
        // 连接成功
        if (wifiConnected())
        {
            Serial.println("WiFi连接成功!");
            Serial.print("IP地址: ");
            Serial.println(wifiGetIP());

            wifiOk = true;

            break;
        }

        // 连接超时
        if (millis() - startTime >= WIFI_TIMEOUT_MS)
        {
            Serial.println("WiFi连接超时! 离线运行!");

            break;
        }

        // 刷新对接动画
        drawCloudConnect(frame++, "正在接入网络");

        delay(50);
    }

    // ==================================================
    // 阶段2：MQTT 初始化动画
    // 放到网络判定（成功或失败）之后显示（约 2 秒）
    // ==================================================

    for (int i = 0; i < 40; i++)
    {
        drawCloudConnect(frame++, "MQTT初始化");
        delay(50);
    }

    // WiFi 失败：直接离线运行（无网络，MQTT 无法连接）
    if (!wifiOk)
    {
        drawCloudFailed();

        delay(1500);

        return;
    }

    // ==================================================
    // 阶段3：连接自建云平台（MQTT）
    // ==================================================

    startTime = millis();

    // 非阻塞地尝试建立 MQTT 连接
    while (!mqttConnected() && (millis() - startTime < MQTT_CONNECT_TIMEOUT_MS))
    {
        mqttLoop();

        drawCloudConnect(frame++, "云平台对接中");

        delay(50);
    }

    if (mqttConnected())
    {
        Serial.println("自建云平台连接成功！");

        drawCloudConnected(wifiGetIP().c_str());
    }
    else
    {
        Serial.println("自建云平台连接超时，离线运行");

        drawCloudFailed();
    }

    delay(1500);
}

// ======================================================
// 处理按键（页面导航）
// ======================================================

void handleKey(int key)
{
    if (key == KEY_NONE_EVENT)
    {
        return;
    }

    // ==================================================
    // 弹框页面：专注模式交互（K3/K4 按弹框提示操作）
    // ==================================================

    if (currentPage == PAGE_DIALOG)
    {
        handleDialogKey(key);

        return;
    }

    // ==================================================
    // K4 长按：任意工作模式下进入专注模式
    // （弹框确认是否开始计时，K4 短按不受影响）
    // ==================================================

    if (key == KEY_OK_HOLD_EVENT)
    {
        if (currentPage == PAGE_MAIN && currentMode != MODE_FOCUS)
        {
            currentDialog = DIALOG_FOCUS_START;

            currentPage = PAGE_DIALOG;

            updateDisplay();
        }

        return;
    }

    // ==================================================
    // 主界面
    // ==================================================

    if (currentPage == PAGE_MAIN)
    {
        // ==============================================
        // 专注模式：K3/K4 短按弹框询问是否结束专注
        // （K1/K2 不拦截，继续正常控制风扇与灯光；
        //  弹框打开期间计时暂停，选择"继续"后恢复）
        // ==============================================

        if (currentMode == MODE_FOCUS &&
            (key == KEY_OK_EVENT || key == KEY_BACK_EVENT))
        {
            currentDialog = DIALOG_FOCUS_END;

            currentPage = PAGE_DIALOG;

            focusPause();

            updateDisplay();

            // 弹框打开立即上报 focus_paused=1，服务器/网页同步暂停计时
            lastMqttReport = millis();

            mqttReportAttributes();

            return;
        }

        // ==============================================
        // 手动控制执行器（任意模式均生效）
        // K2(上键/KEY_UP) 切换 LED，K1(下键/KEY_DOWN) 切换风扇
        // 手动/云端下发优先：自动模式不再覆盖已调过的执行器
        // ==============================================

        if (key == KEY_UP_EVENT) // K2
        {
            lightState = !lightState;
            lightAutoAllowed = false; // 手动/云端下发优先：自动控制不再覆盖灯光

            Serial.print("按 K2 LED：");
            Serial.println(lightState ? "开" : "关");

            updateDisplay();

            // 切换后立即上报一次，并重置周期上报计时
            lastMqttReport = millis();

            mqttReportAttributes();

            return;
        }

        else if (key == KEY_DOWN_EVENT) // K1
        {
            // 循环切换：关闭->一档->二档->三档->关闭
            setFanLevel((fanLevel + 1) % 4);

            fanAutoAllowed = false; // 手动/云端下发优先：自动控制不再覆盖风扇

            Serial.print("按 K1 风扇：");
            Serial.println(fanLevelName(fanLevel));

            updateDisplay();

            // 切换后立即上报一次，并重置周期上报计时
            lastMqttReport = millis();

            mqttReportAttributes();

            return;
        }

        // K3 (GPIO33/返回键) 进入系统设置
        if (key == KEY_BACK_EVENT)
        {
            currentPage = PAGE_MENU;
            menuIndex = 0;
        }

        // K4 (确定键) 短按：手动 <-> 自动切换
        // 进入专注改为 K4 长按（见函数开头 KEY_OK_HOLD_EVENT 处理）
        else if (key == KEY_OK_EVENT)
        {
            if (currentMode == MODE_MANUAL)
            {
                setWorkMode(MODE_AUTO);

                Serial.println("按 K4 切换到：自动模式");
            }
            else
            {
                setWorkMode(MODE_MANUAL);

                Serial.println("按 K4 切换到：手动模式");
            }

            updateDisplay();

            // 切换后立即上报一次，并重置周期上报计时
            lastMqttReport = millis();

            mqttReportAttributes();

            return;
        }
    }

    // ==================================================
    // 系统菜单
    // ==================================================

    else if (currentPage == PAGE_MENU)
    {
        // 上键：下一项（按用户要求与原方向对调）
        if (key == KEY_UP_EVENT)
        {
            menuIndex++;

            if (menuIndex >= MENU_COUNT)
            {
                menuIndex = 0;
            }
        }

        // 下键：上一项
        else if (key == KEY_DOWN_EVENT)
        {
            menuIndex--;

            if (menuIndex < 0)
            {
                menuIndex = MENU_COUNT - 1;
            }
        }

        // K3 (返回键) 进入选中项
        else if (key == KEY_BACK_EVENT)
        {
            switch (menuIndex)
            {
            case 0:
                currentPage = PAGE_TEMP;
                break;

            case 1:
                currentPage = PAGE_LIGHT;
                break;

            case 2:
                currentPage = PAGE_REST;
                break;
            }
        }

        // K4 (确定键) 返回主页面
        else if (key == KEY_OK_EVENT)
        {
            currentPage = PAGE_MAIN;
        }
    }

    // ==================================================
    // 温度页面
    // ==================================================

    else if (currentPage == PAGE_TEMP)
    {
        if (key == KEY_UP_EVENT)
        {
            temperatureThreshold -= 0.5;

            if (temperatureThreshold < 10)
            {
                temperatureThreshold = 10;
            }

            mqttReportThresholds();
        }

        else if (key == KEY_DOWN_EVENT)
        {
            temperatureThreshold += 0.5;

            if (temperatureThreshold > 40)
            {
                temperatureThreshold = 40;
            }

            mqttReportThresholds();
        }

        else if (key == KEY_OK_EVENT)
        {
            currentPage = PAGE_MENU;
        }
    }

    // ==================================================
    // 光照页面
    // ==================================================

    else if (currentPage == PAGE_LIGHT)
    {
        if (key == KEY_UP_EVENT)
        {
            lightThreshold -= 10;

            if (lightThreshold < 0)
            {
                lightThreshold = 0;
            }

            mqttReportThresholds();
        }

        else if (key == KEY_DOWN_EVENT)
        {
            lightThreshold += 10;

            if (lightThreshold > 2000)
            {
                lightThreshold = 2000;
            }

            mqttReportThresholds();
        }

        else if (key == KEY_OK_EVENT)
        {
            currentPage = PAGE_MENU;
        }
    }

    // ==================================================
    // 休息提醒间隔页面（专注模式每满该间隔提醒休息）
    // ==================================================

    else if (currentPage == PAGE_REST)
    {
        if (key == KEY_UP_EVENT)
        {
            // 复用 setRestRemindMin：专注计时中按比例顺延下次提醒时间点
            setRestRemindMin(restRemindMin - 1);

            mqttReportThresholds();
        }

        else if (key == KEY_DOWN_EVENT)
        {
            setRestRemindMin(restRemindMin + 1);

            mqttReportThresholds();
        }

        else if (key == KEY_OK_EVENT)
        {
            currentPage = PAGE_MENU;
        }
    }

    updateDisplay();
}

// ======================================================
// 弹框按键处理（专注模式交互）
// 开始弹框：K3 = 开始，K4 = 取消
// 专注/休息弹框：K3 = 继续（左侧），K4 = 结束（右侧）
// 结束展示页：K3 / K4 均可返回，提示音响完最后一声也自动返回
// ======================================================

void handleDialogKey(int key)
{
    // 弹框只响应 K3（取消键）与 K4（确定键）
    if (key != KEY_OK_EVENT && key != KEY_BACK_EVENT)
    {
        return;
    }

    bool k3 = (key == KEY_BACK_EVENT);

    bool k4 = (key == KEY_OK_EVENT);

    switch (currentDialog)
    {
    // 进入专注：询问是否开始计时
    case DIALOG_FOCUS_START:
        if (k3)
        {
            // K3 确认：进入专注并开始计时
            setWorkMode(MODE_FOCUS);

            focusStart();

            // 进入专注提示音：响五声
            buzzerBeep(5);

            Serial.println("[专注] 开始专注计时");

            // 模式变化立即上报，网页同步显示"专注"
            lastMqttReport = millis();

            mqttReportAttributes();
        }
        else
        {
            // K4 取消：回到手动模式（不进入专注）
            setWorkMode(MODE_MANUAL);

            Serial.println("[专注] 已取消，回到手动模式");

            lastMqttReport = millis();

            mqttReportAttributes();
        }

        currentDialog = DIALOG_NONE;

        currentPage = PAGE_MAIN;

        break;

    // 专注中询问结束 / 满提醒周期提醒休息
    case DIALOG_FOCUS_END:
    case DIALOG_FOCUS_REST:
        if (k4)
        {
            // 结束提示音：响五声（buzzerBeep 会重置未响完的提醒音序列）
            buzzerBeep(5);

            // K4 结束专注：先冻结时长，再记录并展示（冻结后不再增长）
            focusStop();

            unsigned long s = focusElapsedMs() / 1000;

            Serial.printf("[专注] 结束，本次学习时长 %02lu:%02lu:%02lu\n",
                          s / 3600, (s / 60) % 60, s % 60);

            // 退出后回到手动模式（关闭灯光与风扇）
            setWorkMode(MODE_MANUAL);

            // 展示本次时长，按 K3 返回主界面
            currentDialog = DIALOG_FOCUS_DONE;

            lastMqttReport = millis();

            mqttReportAttributes();
        }
        else
        {
            // K3 继续专注：恢复"结束询问"弹框期间暂停的计时
            // （休息提醒弹框期间计时一直在走，此处恢复为空操作）
            focusResume();

            // 休息提醒后顺延下一个提醒周期，并立即停掉剩余提醒音
            if (currentDialog == DIALOG_FOCUS_REST)
            {
                buzzerSilence();

                focusRestPostpone();
            }

            currentDialog = DIALOG_NONE;

            currentPage = PAGE_MAIN;

            // 弹框关闭立即上报，网页同步收起对应弹框
            lastMqttReport = millis();

            mqttReportAttributes();
        }
        break;

    // 专注结束展示页：K3 / K4 均可返回主界面
    // （退出时若五声提示音还没响完，立即静音；
    //  无人按键时提示音响完最后一声由主循环自动返回）
    case DIALOG_FOCUS_DONE:
        if (k3 || k4)
        {
            buzzerSilence();

            currentDialog = DIALOG_NONE;

            currentPage = PAGE_MAIN;
        }
        break;

    default:
        currentDialog = DIALOG_NONE;

        currentPage = PAGE_MAIN;
        break;
    }

    updateDisplay();
}

// ======================================================
// 串口输出
// ======================================================

void printStatus()
{
    Serial.println();
    Serial.println("==============================");

    Serial.print("温度：");
    Serial.print(temperature);
    Serial.println(" C");

    Serial.print("湿度：");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("光照：");
    Serial.print(lux);
    Serial.println(" lx");

    Serial.print("人体：");

    if (peopleDetected)
    {
        Serial.println("有人");
    }
    else
    {
        Serial.println("无人");
    }

    Serial.print("模式：");

    if (currentMode == MODE_AUTO)
    {
        Serial.println("自动");
    }
    else if (currentMode == MODE_FOCUS)
    {
        Serial.println("专注");
    }
    else
    {
        Serial.println("手动");
    }

    Serial.print("风扇：");

    if (fanState)
    {
        Serial.print("开启（");
        Serial.print(fanLevelName(fanLevel));
        Serial.println("）");
    }
    else
    {
        Serial.println("关闭");
    }

    Serial.print("灯光：");

    if (lightState)
    {
        Serial.println("开启");
    }
    else
    {
        Serial.println("关闭");
    }

    Serial.println("==============================");
}

// ======================================================
// 主循环
// ======================================================

void loop()
{
    // ==================================================
    // 保持云平台连接，并处理平台下发的消息
    // ==================================================

    mqttLoop();

    // ==================================================
    // 专注界面计时按秒边界刷新：每进入新的一秒立即重绘，
    // 屏上计时与真实专注时长严格对齐（不再有随机相位滞后）
    // ==================================================

    static unsigned long lastPaintedSec = 0;

    // 休息提醒到期后整分钟计时的定格展示起始时刻，0 = 未在定格展示
    static unsigned long restHoldMs = 0;

    if (currentMode == MODE_FOCUS && currentPage == PAGE_MAIN)
    {
        unsigned long focusSec = focusElapsedMs() / 1000;

        // 定格展示期间不再刷新计时，避免 01:00 一闪而过变成 01:01
        if (focusSec != lastPaintedSec && restHoldMs == 0)
        {
            lastPaintedSec = focusSec;

            updateDisplay();
        }
    }

    // ==================================================
    // 专注模式：满一个提醒周期先定格显示整分钟（01:00）片刻，
    // 再弹框提醒注意休息（可选择结束专注或继续，
    // 继续后每再满一个周期提醒一次）
    // ==================================================

    // 休息提醒弹框打开时刻（用于无人操作自动继续）
    static unsigned long restDialogOpenMs = 0;

    if (currentMode == MODE_FOCUS && currentPage == PAGE_MAIN && focusRestDue())
    {
        if (restHoldMs == 0)
        {
            // 到期瞬间：上方秒边界刷新已把 01:00 画出，开始定格计时
            restHoldMs = millis();
        }
        else if (millis() - restHoldMs >= FOCUS_REST_HOLD_MS)
        {
            restHoldMs = 0;

            currentDialog = DIALOG_FOCUS_REST;

            currentPage = PAGE_DIALOG;

            restDialogOpenMs = millis();

            // 休息提醒期间专注计时继续走（不暂停），弹框只是提醒不冻结时长

            // 休息提醒蜂鸣：约 1 秒一声共五声（约 5 秒），响完自动停止
            buzzerBeep(5);

            updateDisplay();

            // 立即上报 focus_rest=1，网页同步弹出休息提醒弹框
            // （周期上报 10 秒一次，等不到下一轮弹框就已自动关闭）
            lastMqttReport = millis();

            mqttReportAttributes();
        }
    }
    else
    {
        // 未到期/不在专注主页/弹框已打开：复位定格状态
        restHoldMs = 0;
    }

    // ==================================================
    // 休息提醒弹框：超过 10 秒无人操作，自动继续专注
    // ==================================================

    if (currentPage == PAGE_DIALOG && currentDialog == DIALOG_FOCUS_REST &&
        millis() - restDialogOpenMs >= FOCUS_REST_AUTO_CONTINUE_MS)
    {
        // 计时期间一直在走，无需恢复；静音残留提醒音并顺延提醒周期
        buzzerSilence();

        focusRestPostpone();

        currentDialog = DIALOG_NONE;

        currentPage = PAGE_MAIN;

        Serial.println("[专注] 休息提醒无人操作，自动继续");

        updateDisplay();

        // 立即上报 focus_rest=0，网页同步收起休息提醒弹框并继续计时
        lastMqttReport = millis();

        mqttReportAttributes();
    }

    // ==================================================
    // 专注结束展示页：结束提示音响完最后一声（声音消失）
    // 自动返回主界面，无需再按键
    // （进入展示页与开始提示音在同一处同步执行，
    //  蜂鸣器必为"响音中"，不会出现一进页面就立即返回）
    // ==================================================

    if (currentPage == PAGE_DIALOG && currentDialog == DIALOG_FOCUS_DONE &&
        !buzzerBusy())
    {
        currentDialog = DIALOG_NONE;

        currentPage = PAGE_MAIN;

        Serial.println("[专注] 结束提示音响完，自动返回主界面");

        updateDisplay();
    }

    // ==================================================
    // 按键
    // 仅在阈值调整页面（温度/光照/休息提醒）启用长按重复连续调整
    // ==================================================

    bool allowKeyRepeat =
        (currentPage == PAGE_TEMP) || (currentPage == PAGE_LIGHT) ||
        (currentPage == PAGE_REST);

    int key = getKey(allowKeyRepeat);

    if (key != KEY_NONE_EVENT)
    {
        handleKey(key);
    }

    // ==================================================
    // 定时读取传感器
    // ==================================================

    if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS)
    {
        lastSensorRead = millis();

        readSensors();

        // ==============================================
        // 自动控制
        // ==============================================

        automaticControl();

        // ==============================================
        // 串口输出
        // ==============================================

        printStatus();

        // ==============================================
        // 更新 OLED
        // 专注计时页由上方 1 秒定时独立刷新、弹框内容静态，
        // 这两类页面跳过，避免同页双重整帧重绘造成闪烁
        // ==============================================

        bool periodicUi = (currentPage != PAGE_DIALOG) &&
                          !(currentMode == MODE_FOCUS && currentPage == PAGE_MAIN);

        if (periodicUi)
        {
            updateDisplay();
        }

        // ==============================================
        // 上报属性到云平台（受流控限制，按独立周期执行）
        // ==============================================

        if (millis() - lastMqttReport >= MQTT_REPORT_INTERVAL_MS)
        {
            lastMqttReport = millis();

            mqttReportAttributes();
        }
    }

    // ==================================================
    // 将状态同步到实际硬件（LED + 风扇）
    // ==================================================

    applyActuators();

    // ==================================================
    // 蜂鸣器状态机驱动（非阻塞响/停切换）
    // ==================================================

    buzzerLoop();

    delay(20);
}
