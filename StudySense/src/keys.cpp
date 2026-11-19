#include <Arduino.h>

#include "config.h"
#include "keys.h"

// ======================================================
// 按键状态（按下-弹起 边沿检测）
// ======================================================

// 当前按住的按键（KEY_NONE_EVENT 表示没有按键按下）
static int heldKey = KEY_NONE_EVENT;

// 本次按下的起始时刻（计算按压时长，滤除抖动）
static unsigned long pressStart = 0;

// 上次完整按键（按下并弹起）的结束时刻（限制连按频率）
static unsigned long lastRelease = 0;

// 上次长按重复触发时刻（连续触发用）
static unsigned long lastRepeat = 0;

// 本次按住是否已进入长按重复阶段
// 进入后弹起不再产生按键事件，避免多触发一次
static bool repeatFired = false;

// 本次按住是否已触发长按事件（K4 长按进入专注模式用）
// 触发后弹起不再产生短按事件，避免多触发一次
static bool holdFired = false;

// ======================================================
// 初始化
// ======================================================

void keysInit()
{
    pinMode(KEY_UP, INPUT_PULLUP);
    pinMode(KEY_DOWN, INPUT_PULLUP);
    pinMode(KEY_OK, INPUT_PULLUP);
    pinMode(KEY_BACK, INPUT_PULLUP);
}

// ======================================================
// 读取当前按下的物理按键（无按下返回 KEY_NONE_EVENT）
// ======================================================

static int readRawKey()
{
    if (digitalRead(KEY_UP) == LOW)
    {
        return KEY_UP_EVENT;
    }

    if (digitalRead(KEY_DOWN) == LOW)
    {
        return KEY_DOWN_EVENT;
    }

    if (digitalRead(KEY_OK) == LOW)
    {
        return KEY_OK_EVENT;
    }

    if (digitalRead(KEY_BACK) == LOW)
    {
        return KEY_BACK_EVENT;
    }

    return KEY_NONE_EVENT;
}

// ======================================================
// 获取按键
// 一次完整按键 = 按下 + 弹起：
// - 短按：弹起时产生一次事件
// - 长按事件（K4，仅 allowRepeat=false 时）：按住超过
//   KEY_LONG_PRESS_MS 立即返回一次 KEY_OK_HOLD_EVENT，
//   弹起时不再产生短按事件
// - 长按重复（仅 allowRepeat=true 时）：按住超过 KEY_REPEAT_DELAY_MS 后，
//   每 KEY_REPEAT_INTERVAL_MS 重复触发一次（连续调整用），
//   弹起时不再产生一次事件（避免多触发一次）
// - allowRepeat=false 时除 K4 长按事件外按住不放不会触发（弹起时才产生事件）
// - 按压时长不足 KEY_MIN_PRESS_MS 视为抖动丢弃
// - 两次完整按键之间需间隔 KEY_DEBOUNCE_MS
// ======================================================

int getKey(bool allowRepeat)
{
    // ==================================================
    // 没有按键按住：检测新按下
    // ==================================================

    if (heldKey == KEY_NONE_EVENT)
    {
        int raw = readRawKey();

        // 距上次完整按键需超过消抖间隔，才接受新按下
        if (raw != KEY_NONE_EVENT && millis() - lastRelease >= KEY_DEBOUNCE_MS)
        {
            heldKey = raw;

            pressStart = millis();

            lastRepeat = millis();

            repeatFired = false;

            holdFired = false;
        }

        return KEY_NONE_EVENT;
    }

    // ==================================================
    // 已有按键按住：检测该键是否已弹起
    // ==================================================

    bool released = false;

    switch (heldKey)
    {
    case KEY_UP_EVENT:
        released = (digitalRead(KEY_UP) == HIGH);
        break;

    case KEY_DOWN_EVENT:
        released = (digitalRead(KEY_DOWN) == HIGH);
        break;

    case KEY_OK_EVENT:
        released = (digitalRead(KEY_OK) == HIGH);
        break;

    case KEY_BACK_EVENT:
        released = (digitalRead(KEY_BACK) == HIGH);
        break;
    }

    unsigned long now = millis();

    // ==================================================
    // 长按重复（仅 allowRepeat 时启用）：
    // 未弹起且按住超过阈值，按间隔连续触发
    // ==================================================

    if (allowRepeat && !released && now - pressStart >= KEY_REPEAT_DELAY_MS)
    {
        if (now - lastRepeat >= KEY_REPEAT_INTERVAL_MS)
        {
            lastRepeat = now;

            repeatFired = true;

            return heldKey;
        }
    }

    // ==================================================
    // 长按事件（仅非重复模式）：K4 按住超过阈值触发一次
    // 触发后弹起不再产生短按事件
    // ==================================================

    if (!allowRepeat && !released && !holdFired &&
        heldKey == KEY_OK_EVENT && now - pressStart >= KEY_LONG_PRESS_MS)
    {
        holdFired = true;

        return KEY_OK_HOLD_EVENT;
    }

    // ==================================================
    // 弹起处理
    // ==================================================

    if (released)
    {
        int event = heldKey;

        heldKey = KEY_NONE_EVENT;

        // 按压时长达到要求 且 未进入长按重复/长按事件阶段：才算一次完整按键
        if (now - pressStart >= KEY_MIN_PRESS_MS && !repeatFired && !holdFired)
        {
            lastRelease = now;

            return event;
        }

        // 已进入重复/长按阶段：弹起只更新消抖计时，不再返回事件
        lastRelease = now;
    }

    return KEY_NONE_EVENT;
}
