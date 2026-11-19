#ifndef DISPLAY_H
#define DISPLAY_H

// ======================================================
// 页面
// ======================================================

enum Page
{
    PAGE_MAIN,
    PAGE_MENU,
    PAGE_TEMP,
    PAGE_LIGHT,
    PAGE_REST,
    PAGE_MODE,
    PAGE_DIALOG
};

// ======================================================
// 弹框类型（专注模式交互）
// ======================================================

enum DialogType
{
    DIALOG_NONE,        // 无弹框
    DIALOG_FOCUS_START, // 进入专注：询问是否开始计时
    DIALOG_FOCUS_END,   // 专注中：询问是否结束
    DIALOG_FOCUS_REST,  // 满提醒周期：提醒注意休息（结束 / 继续）
    DIALOG_FOCUS_DONE   // 专注结束：展示本次学习时长
};

// 当前弹框类型
extern DialogType currentDialog;

// 当前页面
extern Page currentPage;

// 菜单选中项
extern int menuIndex;

// 菜单项数量
extern const int MENU_COUNT;

// ======================================================
// 函数
// ======================================================

// OLED 初始化
void displayInit();

// 开机动态对接界面（MCU <-> 云平台）
// animFrame：动画帧号，statusText：状态文字
void drawCloudConnect(int animFrame, const char *statusText);

// 云平台对接成功界面（显示 IP 地址）
void drawCloudConnected(const char *ip);

// 云平台对接失败界面
void drawCloudFailed();

// 根据当前页面刷新显示
void updateDisplay();

#endif
