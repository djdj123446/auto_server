#ifndef KEYS_H
#define KEYS_H

// ======================================================
// 按键事件
// ======================================================

enum Key
{
    KEY_NONE_EVENT = 0,  // 无按键
    KEY_UP_EVENT = 1,    // 上键
    KEY_DOWN_EVENT = 2,  // 下键
    KEY_OK_EVENT = 3,    // 确定键
    KEY_BACK_EVENT = 4,  // 返回键
    KEY_OK_HOLD_EVENT = 5 // 确定键（K4）长按：按住超过 KEY_LONG_PRESS_MS 触发一次
};

// ======================================================
// 函数
// ======================================================

// 初始化按键引脚
void keysInit();

// 扫描按键（带消抖），返回 Key 枚举值
// allowRepeat：是否启用长按重复触发（按住后连续返回按键事件）
// 仅阈值调整页面（PAGE_TEMP / PAGE_LIGHT）传入 true，其余页面传 false
int getKey(bool allowRepeat);

#endif
