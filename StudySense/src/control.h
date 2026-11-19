#ifndef CONTROL_H
#define CONTROL_H

// ======================================================
// 工作模式
// ======================================================

enum WorkMode
{
    MODE_AUTO,
    MODE_MANUAL,
    MODE_FOCUS
};

// ======================================================
// 环境阈值（可在设置页面调整）
// ======================================================

extern float temperatureThreshold; // 温度上限
extern float lightThreshold;       // 光照下限

// 专注模式休息提醒间隔（分钟，网页端可下发 rest_min 修改）
extern int restRemindMin;

// 设置休息提醒间隔（分钟），并按比例顺延当前周期的下次提醒时间点
void setRestRemindMin(int minutes);

// ======================================================
// 执行器状态
// ======================================================

extern bool fanState;   // 风扇开关
extern bool lightState; // 灯光开关
extern int fanSpeed;    // 风扇转速（0~100%，用于 PWM 调速）
extern int fanLevel;    // 风扇档位：0=关闭 1=一档 2=二档 3=三档

// 自动控制是否允许接管执行器（默认 true）
// 云端/手动下发风扇或灯光后置为 false，自动控制不再覆盖对应执行器，
// 模式切换回自动时重新允许（保证"下发优先且保持"）
extern bool fanAutoAllowed;
extern bool lightAutoAllowed;

// 当前工作模式
extern WorkMode currentMode;

// ======================================================
// 专注模式计时
// ======================================================

// 开始专注计时（进入专注模式时调用）
void focusStart();

// 结束专注：冻结本次总时长（结束页展示，之后不再增长）
void focusStop();

// 暂停专注计时（"按任意键"弹框打开时调用）
void focusPause();

// 恢复专注计时（弹框选择"继续"时调用，暂停时长不计入）
void focusResume();

// 当前已专注时长（毫秒）
unsigned long focusElapsedMs();

// 专注计时是否处于暂停状态（"是否结束专注"弹框打开期间为 true），
// 供云端上报 focus_paused，让网页弹框与设备保持同步
bool focusIsPaused();

// 是否到达休息提醒时间点（满 1 小时提醒一次）
bool focusRestDue();

// 选择"继续"后，把下次提醒时间点顺延一个周期
void focusRestPostpone();

// ======================================================
// 函数
// ======================================================

// 返回档位的显示文字（0=关闭 / 一档风速 / 二档风速 / 三档风速）
const char *fanLevelName(int level);

// 统一设置风扇档位（0=关闭 1=一档 2=二档 3=三档），自动换算转速与开关状态
void setFanLevel(int level);

// 统一切换工作模式（切到手动时关闭风扇与灯光）
void setWorkMode(WorkMode mode);

// 自动控制逻辑（仅自动模式生效）
void automaticControl();

// 初始化执行器（LED 与风扇 GPIO/PWM）
void actuatorsInit();

// 将当前状态（lightState / fanState / fanSpeed）应用到实际硬件
void applyActuators();

// ======================================================
// 蜂鸣器（有源，高电平响）
// ======================================================

// 短响 times 次（约 1 秒一声，进入/结束专注、休息提醒共用）
void buzzerBeep(int times);

// 强制静音：立即停止未响完的提示音序列，
// 用于按键退出结束弹框等场景打断仍在进行的提示音
void buzzerSilence();

// 提示音序列是否仍在进行：最后一次响音结束（声音消失）后变为 false，
// 供专注结束展示页判断"响完最后一声自动返回主界面"
bool buzzerBusy();

// 蜂鸣器状态机驱动（主循环调用，非阻塞）
void buzzerLoop();

#endif
