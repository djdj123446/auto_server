#include <Arduino.h>

#include "config.h"
#include "control.h"
#include "sensors.h"

// ======================================================
// 环境阈值
// ======================================================

// 温度控制
float temperatureThreshold = 28.0;

// 光照控制
float lightThreshold = 300.0;

// 专注模式休息提醒间隔（分钟），默认与 config.h 的 FOCUS_REST_REMIND_MS 一致
int restRemindMin = 1;

// 下次休息提醒的时间点（毫秒，每满一个提醒周期提醒一次）
static unsigned long focusNextRestMs = 0;

// ======================================================
// 设置休息提醒间隔（分钟，云端下发/本地共用）
// 专注计时进行中时按新旧周期的比例顺延下次提醒时间点，
// 避免"刚设置完立刻弹出提醒"或"长时间不提醒"
// ======================================================

void setRestRemindMin(int minutes)
{
    if (minutes < REST_REMIND_MIN_MIN)
    {
        minutes = REST_REMIND_MIN_MIN;
    }

    if (minutes > REST_REMIND_MAX_MIN)
    {
        minutes = REST_REMIND_MAX_MIN;
    }

    if (currentMode == MODE_FOCUS && focusNextRestMs != 0)
    {
        unsigned long oldCycle = (unsigned long)restRemindMin * 60UL * 1000UL;

        unsigned long remainMs =
            (long)(focusNextRestMs - millis()) > 0
                ? (unsigned long)(focusNextRestMs - millis())
                : 0UL;

        // 剩余时间按比例缩放后顺延，保持提醒节奏平滑
        focusNextRestMs = millis() + remainMs * (unsigned long)minutes / oldCycle;
    }

    restRemindMin = minutes;
}

// ======================================================
// 执行器状态
// ======================================================

bool fanState = false;
bool lightState = false;

// 风扇转速（百分比 0~100）
int fanSpeed = 60;

// 风扇档位（0=关闭 1=一档 2=二档 3=三档）
int fanLevel = 0;

// 自动控制是否允许接管执行器（默认允许）
bool fanAutoAllowed = true;
bool lightAutoAllowed = true;

// 最后一次检测到人的时刻（millis），
// 用于 PIR 静止时的人在场保持
static unsigned long lastPeopleDetectedMs = 0;

// 档位对应的显示文字
static const char *const FAN_LEVEL_NAMES[] =
    {"关闭", "一档风速", "二档风速", "三档风速"};

// ======================================================
// 返回档位的显示文字
// ======================================================

const char *fanLevelName(int level)
{
    if (level < 0)
    {
        level = 0;
    }

    if (level > 3)
    {
        level = 3;
    }

    return FAN_LEVEL_NAMES[level];
}

// ======================================================
// 工作模式
// ======================================================

WorkMode currentMode = MODE_MANUAL;

// ======================================================
// 专注模式计时状态
// ======================================================

// 专注计时起点（毫秒）
static unsigned long focusStartMs = 0;

// 是否正在计时（结束专注后置 false，时长冻结不再增长）
static bool focusRunning = false;

// 冻结的结束时刻（毫秒），focusElapsedMs() 据此返回总时长
static unsigned long focusFrozenEndMs = 0;

// 暂停状态（弹框打开期间暂停计时）
static bool focusPaused = false;

// 暂停开始时刻（毫秒）
static unsigned long focusPauseStartMs = 0;

// 开始专注计时
void focusStart()
{
    focusStartMs = millis();

    focusFrozenEndMs = 0;

    focusRunning = true;

    focusPaused = false;

    focusNextRestMs = focusStartMs + (unsigned long)restRemindMin * 60UL * 1000UL;
}

// 结束专注：冻结本次总时长（结束页展示）
void focusStop()
{
    if (focusRunning)
    {
        // 暂停中结束时，时长冻结在暂停那一刻
        focusFrozenEndMs = focusPaused ? focusPauseStartMs : millis();
    }

    focusRunning = false;

    focusPaused = false;
}

// 暂停专注计时（"按任意键"弹框打开时调用）
void focusPause()
{
    if (focusRunning && !focusPaused)
    {
        focusPaused = true;

        focusPauseStartMs = millis();
    }
}

// 恢复专注计时（弹框选择"继续"时调用）：
// 把计时起点与下次休息提醒点整体后移暂停的时长，
// 等效于暂停期间时间静止
void focusResume()
{
    if (focusPaused)
    {
        unsigned long pausedMs = millis() - focusPauseStartMs;

        focusStartMs += pausedMs;

        focusNextRestMs += pausedMs;

        focusPaused = false;
    }
}

// 当前已专注时长（毫秒）：计时中实时计算，结束后返回冻结值；
// 暂停期间冻结在暂停那一刻
unsigned long focusElapsedMs()
{
    if (focusRunning)
    {
        unsigned long now = focusPaused ? focusPauseStartMs : millis();

        return now - focusStartMs;
    }

    if (focusFrozenEndMs != 0)
    {
        return focusFrozenEndMs - focusStartMs;
    }

    return 0;
}

// 是否到达休息提醒时间点
bool focusRestDue()
{
    return (currentMode == MODE_FOCUS) &&
           ((long)(millis() - focusNextRestMs) >= 0);
}

// 专注计时是否处于暂停状态：
// 只在专注模式下有意义（切走模式后 focusRunning 已复位，避免网页弹框卡住）
bool focusIsPaused()
{
    return (currentMode == MODE_FOCUS) && focusRunning && focusPaused;
}

// 选择"继续"后，把下次提醒时间点顺延一个周期
void focusRestPostpone()
{
    focusNextRestMs += (unsigned long)restRemindMin * 60UL * 1000UL;
}

// ======================================================
// 自动控制逻辑
// ======================================================

void automaticControl()
{
    // 只有自动模式才进行自动控制
    if (currentMode != MODE_AUTO)
    {
        return;
    }

    // ==================================================
    // 风扇控制（云端/手动下发过风扇后不再自动覆盖）
    // ==================================================

    if (fanAutoAllowed)
    {
        // 温度达到上限 -> 开风扇（一档）
        if (temperature >= temperatureThreshold)
        {
            if (!fanState)
            {
                setFanLevel(1);

                Serial.println("[自动控制] 温度过高");
                Serial.println("[自动控制] 风扇：开启");
            }
        }

        // 温度降到上限以下 -> 关风扇
        else if (temperature < temperatureThreshold)
        {
            if (fanState)
            {
                setFanLevel(0);

                Serial.println("[自动控制] 温度恢复");
                Serial.println("[自动控制] 风扇：关闭");
            }
        }
    }

    // ==================================================
    // 灯光控制（云端/手动下发过灯光后不再自动覆盖）
    // ==================================================

    if (lightAutoAllowed)
    {
        // PIR 检测到人：刷新"人在场"时间戳
        if (peopleDetected)
        {
            lastPeopleDetectedMs = millis();
        }

        // 在保持窗口内都视为"有人"（人静止不动 PIR 无输出也不关灯）
        bool personPresent =
            (millis() - lastPeopleDetectedMs) < LIGHT_PEOPLE_HOLD_MS;

        // 有人 且 光照不足 -> 开灯
        if (personPresent && lux <= lightThreshold)
        {
            if (!lightState)
            {
                lightState = true;

                Serial.println("[自动控制] 有人且光照不足");
                Serial.println("[自动控制] 灯光：开启");
            }
        }

        // 保持窗口内无人 或 光照充足 -> 关灯
        else if (!personPresent || lux > lightThreshold)
        {
            if (lightState)
            {
                lightState = false;

                Serial.println("[自动控制] 无人或光照充足");
                Serial.println("[自动控制] 灯光：关闭");
            }
        }
    }
}

// ======================================================
// 统一设置风扇档位（本地按键与云端下发共用）
// 0=关闭 1=一档 2=二档 3=三档，自动换算转速与开关状态
// ======================================================

void setFanLevel(int level)
{
    if (level < 0)
    {
        level = 0;
    }

    if (level > 3)
    {
        level = 3;
    }

    static const int FAN_LEVEL_SPEEDS[] = {0, 60, 80, 100};

    fanLevel = level;

    fanSpeed = FAN_LEVEL_SPEEDS[level];
    fanState = (level > 0);
}

// ======================================================
// 统一切换工作模式（本地按键与云端下发共用）
// 切到手动模式时关闭风扇与灯光；切到专注模式时保持当前
// 灯光/风扇状态（不打断学习环境），并恢复自动接管权
// ======================================================

void setWorkMode(WorkMode mode)
{
    currentMode = mode;

    if (mode == MODE_MANUAL)
    {
        fanState = false;
        lightState = false;
        fanLevel = 0;
        fanSpeed = 0;
    }

    // 模式切换后恢复自动接管权（下次下发优先依然生效）
    fanAutoAllowed = true;
    lightAutoAllowed = true;
}

// ======================================================
// 执行器硬件控制（LED + 风扇 L9110S）
// ======================================================

// 风扇 PWM 参数
static const int FAN_PWM_FREQ = 1000;    // 1 kHz
static const int FAN_PWM_RESOLUTION = 8; // 8 位，占空比 0~255

// LEDC 通道（IN1/IN2 各自一路，便于方向切换）
static const int FAN_IN1_CHANNEL = 0;
static const int FAN_IN2_CHANNEL = 1;

// 风扇停止：A、B 均为低电平
static void fanStop()
{
    ledcWrite(FAN_IN1_CHANNEL, 0);
    ledcWrite(FAN_IN2_CHANNEL, 0);
}

// 风扇正转：A(IN1) 低、B(IN2) 高/PWM
// （反转则为 A 高/PWM、B 低）
static void fanForward(int duty)
{
    ledcWrite(FAN_IN1_CHANNEL, 0);
    ledcWrite(FAN_IN2_CHANNEL, duty);
}

// ======================================================
// 初始化执行器
// ======================================================

void actuatorsInit()
{
    // ==================================================
    // 3W LED 灯珠：S 端高电平亮、低电平灭
    // ==================================================

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // ==================================================
    // L9110S 风扇驱动：IN1/IN2 用两路 LEDC PWM 驱动
    // ==================================================

    ledcSetup(FAN_IN1_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
    ledcSetup(FAN_IN2_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);

    ledcAttachPin(FAN_IN1, FAN_IN1_CHANNEL);
    ledcAttachPin(FAN_IN2, FAN_IN2_CHANNEL);

    // 上电停止
    fanStop();

    // ==================================================
    // 有源蜂鸣器：高电平响、低电平停
    // ==================================================

    pinMode(BUZZER_PIN, OUTPUT);

    digitalWrite(BUZZER_PIN, LOW);

    Serial.println("执行器初始化完成（LED + 风扇 + 蜂鸣器）");
}

// ======================================================
// 将当前状态应用到实际硬件
// ======================================================

void applyActuators()
{
    // LED
    digitalWrite(LED_PIN, lightState ? HIGH : LOW);

    // 风扇
    if (fanState)
    {
        // 正转，占空比按 fanSpeed（0~100%）换算为 PWM（0~255）
        int duty = constrain(fanSpeed, 0, 100) * 255 / 100;

        fanForward(duty);
    }
    else
    {
        fanStop();
    }
}

// ======================================================
// 蜂鸣器（有源，高电平响；低电平触发模块需对调响/停电平）
// 非阻塞状态机：buzzerLoop() 在主循环中驱动响/停切换
// ======================================================

// 单次响音时长（毫秒）
static const int BUZZER_ON_MS = 150;

// 连响之间的静音间隔（毫秒），与响音时长合计约 1 秒一声
static const int BUZZER_GAP_MS = 850;

// 状态：0=空闲 1=响音阶段 2=静音阶段
static int buzzerState = 0;

// 当前阶段计时起点
static unsigned long buzzerTimer = 0;

// 剩余响音次数
static int buzzerRemaining = 0;

// 立即开始一次响音
static void buzzerSoundOn()
{
    digitalWrite(BUZZER_PIN, HIGH);

    buzzerState = 1;

    buzzerTimer = millis();
}

// 短响 times 次（进入/结束专注、休息提醒共用，约 1 秒一声）
void buzzerBeep(int times)
{
    buzzerRemaining = times;

    buzzerSoundOn();
}

// 强制静音：立即停止未响完的提示音序列并拉低蜂鸣器，
// 供按键退出结束弹框、云端切模式等场景打断提示音
void buzzerSilence()
{
    buzzerRemaining = 0;

    buzzerState = 0;

    buzzerTimer = 0;

    digitalWrite(BUZZER_PIN, LOW);
}

// 提示音序列是否仍在进行：响音阶段或有剩余次数即为 true，
// 最后一次响音结束（进入末尾静音）即变 false
bool buzzerBusy()
{
    return buzzerState == 1 || buzzerRemaining > 0;
}

// 蜂鸣器状态机（主循环调用）
void buzzerLoop()
{
    if (buzzerState == 0)
    {
        return;
    }

    unsigned long now = millis();

    // 响音阶段结束 -> 静音
    if (buzzerState == 1)
    {
        if (now - buzzerTimer >= BUZZER_ON_MS)
        {
            digitalWrite(BUZZER_PIN, LOW);

            buzzerRemaining--;

            buzzerState = 2;

            buzzerTimer = now;
        }
    }

    // 静音阶段结束 -> 还有剩余次数则继续响，否则空闲
    else if (now - buzzerTimer >= BUZZER_GAP_MS)
    {
        if (buzzerRemaining > 0)
        {
            buzzerSoundOn();
        }
        else
        {
            buzzerState = 0;
        }
    }
}
