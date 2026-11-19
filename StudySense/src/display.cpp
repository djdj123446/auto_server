#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "config.h"
#include "display.h"
#include "sensors.h"
#include "control.h"

// ======================================================
// 显示对象
// ======================================================

// SSD1306 128x64 I2C OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE);

// ======================================================
// 页面状态
// ======================================================

Page currentPage = PAGE_MAIN;

// 当前弹框类型（无弹框时为 DIALOG_NONE）
DialogType currentDialog = DIALOG_NONE;

int menuIndex = 0;

// 菜单项数量
const int MENU_COUNT = 3;

// ======================================================
// 初始化
// ======================================================

void displayInit()
{
    oled.begin();

    oled.enableUTF8Print();

    // I2C 提速到 400kHz：整帧 sendBuffer 从约 120ms 缩到约 30ms，
    // 专注计时页每秒整帧重绘、弹框切换不再出现明显闪烁
    Wire.setClock(400000);
}

// ======================================================
// 主界面
// ======================================================

// ======================================================
// 主界面小图标（单色点线绘制，约 9x10 像素）
// ======================================================

// 温度计
static void iconThermometer(uint8_t x, uint8_t y)
{
    oled.drawRFrame(x + 1, y, 5, 8, 2); // 管壁
    oled.drawDisc(x + 3, y + 8, 2);     // 底部感温泡
    oled.drawVLine(x + 3, y + 2, 6);    // 水银柱
}

// 水滴
static void iconDroplet(uint8_t x, uint8_t y)
{
    oled.drawDisc(x + 3, y + 6, 3);
    oled.drawBox(x + 2, y + 3, 3, 3);
    oled.drawBox(x + 3, y + 1, 1, 2);
}

// 太阳
static void iconSun(uint8_t x, uint8_t y)
{
    oled.drawDisc(x + 3, y + 4, 2); // 中心
    oled.drawPixel(x + 3, y);       // 上光芒
    oled.drawPixel(x + 3, y + 8);   // 下光芒
    oled.drawPixel(x - 1, y + 4);   // 左光芒
    oled.drawPixel(x + 7, y + 4);   // 右光芒
    oled.drawPixel(x, y + 1);       // 左上
    oled.drawPixel(x + 6, y + 1);   // 右上
    oled.drawPixel(x, y + 7);       // 左下
    oled.drawPixel(x + 6, y + 7);   // 右下
}

// 人形（有人实心 / 无人空心）
static void iconPerson(uint8_t x, uint8_t y, bool present)
{
    if (present)
    {
        oled.drawDisc(x + 3, y + 2, 2);
        oled.drawBox(x + 1, y + 5, 5, 4);
    }
    else
    {
        oled.drawCircle(x + 3, y + 2, 2);
        oled.drawFrame(x + 1, y + 5, 5, 4);
    }
}

// 灯泡（亮实心 / 灭空心）
static void iconBulb(uint8_t x, uint8_t y, bool on)
{
    if (on)
    {
        oled.drawDisc(x + 3, y + 3, 3);
    }
    else
    {
        oled.drawCircle(x + 3, y + 3, 3);
    }

    oled.drawBox(x + 2, y + 6, 3, 3); // 灯座
}

// 风扇（开叶片呈 X 形 / 关呈十字形）
static void iconFan(uint8_t x, uint8_t y, bool on)
{
    oled.drawCircle(x + 4, y + 4, 4); // 外框
    oled.drawDisc(x + 4, y + 4, 1);   // 中心

    if (on)
    {
        oled.drawLine(x + 4, y + 4, x + 7, y + 1);
        oled.drawLine(x + 4, y + 4, x + 1, y + 1);
        oled.drawLine(x + 4, y + 4, x + 1, y + 7);
        oled.drawLine(x + 4, y + 4, x + 7, y + 7);
    }
    else
    {
        oled.drawHLine(x + 2, y + 4, 5);
        oled.drawVLine(x + 4, y + 2, 5);
    }
}

// 书本（主界面标题图标）
static void iconBook(uint8_t x, uint8_t y)
{
    oled.drawFrame(x, y + 1, 10, 9); // 外框
    oled.drawVLine(x + 5, y + 1, 9); // 书脊
    oled.drawHLine(x + 2, y + 4, 2); // 左页文字线
    oled.drawHLine(x + 2, y + 7, 2);
    oled.drawHLine(x + 7, y + 4, 2); // 右页文字线
    oled.drawHLine(x + 7, y + 7, 2);
}

// ======================================================
// 主界面
// ======================================================

void drawMainPage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // ==================================================
    // 标题（靠左）
    // ==================================================

    iconBook(0, 3);

    oled.drawUTF8(14, 11, "智能书房");

    // ==================================================
    // 当前模式（靠右，括号内显示）
    // ==================================================

    const char *modeText = (currentMode == MODE_AUTO)    ? "（自动）"
                           : (currentMode == MODE_FOCUS) ? "（专注）"
                                                         : "（手动）";

    int modeWidth = oled.getUTF8Width(modeText);

    oled.drawUTF8(127 - modeWidth, 11, modeText);

    oled.drawHLine(0, 16, 128);

    char buffer[24];

    // ==================================================
    // 温度（图标 + 数值，单位紧贴数据）
    // ==================================================

    iconThermometer(1, 21);

    snprintf(buffer,
             sizeof(buffer),
             "温度: %.1fC",
             temperature);

    oled.drawUTF8(13, 31, buffer);

    // ==================================================
    // 湿度
    // ==================================================

    iconDroplet(1, 36);

    snprintf(buffer,
             sizeof(buffer),
             "湿度: %.0f%%",
             humidity);

    oled.drawUTF8(13, 46, buffer);

    // ==================================================
    // 光照
    // ==================================================

    iconSun(1, 51);

    snprintf(buffer,
             sizeof(buffer),
             "光照: %.0flx",
             lux);

    oled.drawUTF8(13, 61, buffer);

    // ==================================================
    // 人体状态（有人实心 / 无人空心）
    // ==================================================

    iconPerson(80, 21, peopleDetected);

    oled.drawUTF8(92, 31, peopleDetected ? "有人" : "无人");

    // ==================================================
    // LED 开关状态（亮实心 / 灭空心）
    // ==================================================

    iconBulb(80, 36, lightState);

    oled.drawUTF8(92, 46, lightState ? "LED开" : "LED关");

    // ==================================================
    // 风扇状态（开 X 形叶片 / 关十字形）
    // ==================================================

    iconFan(80, 51, fanState);

    oled.drawUTF8(92, 61, fanState ? "风扇开" : "风扇关");

    oled.sendBuffer();
}

// ======================================================
// 菜单界面
// ======================================================

void drawMenuPage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // 标题靠左，与主页面标题同一行
    oled.drawUTF8(2, 11, "系统设置");

    // 横线与主页面一致
    oled.drawHLine(0, 16, 128);

    const char *menuItems[] =
        {
            "温度阈值",
            "光照阈值",
            "休息提醒"};

    for (int i = 0; i < MENU_COUNT; i++)
    {
        // 与主页面内容行位置一致（31 / 46），间距宽松
        int y = 31 + i * 15;

        if (i == menuIndex)
        {
            // 高亮框稍微收小，上下都留出空隙，不碰到横线和下方文字
            oled.drawBox(0, y - 12, 128, 14);

            oled.setDrawColor(0);

            oled.drawUTF8(4, y, menuItems[i]);

            oled.setDrawColor(1);
        }
        else
        {
            oled.drawUTF8(4, y, menuItems[i]);
        }
    }

    oled.sendBuffer();
}

// ======================================================
// 温度设置
// ======================================================

void drawTemperaturePage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    oled.drawUTF8(0, 12, "温度设置");

    oled.drawHLine(0, 15, 128);

    char buffer[32];

    snprintf(buffer,
             sizeof(buffer),
             "当前：%.1f C",
             temperature);

    oled.drawUTF8(0, 31, buffer);

    snprintf(buffer,
             sizeof(buffer),
             "阈值：%.1f C",
             temperatureThreshold);

    oled.drawUTF8(0, 47, buffer);

    oled.drawUTF8(0, 62, "↑↓调整  返回");

    oled.sendBuffer();
}

// ======================================================
// 光照设置
// ======================================================

void drawLightPage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    oled.drawUTF8(0, 12, "光照设置");

    oled.drawHLine(0, 15, 128);

    char buffer[32];

    snprintf(buffer,
             sizeof(buffer),
             "当前：%.0f lx",
             lux);

    oled.drawUTF8(0, 31, buffer);

    snprintf(buffer,
             sizeof(buffer),
             "阈值：%.0f lx",
             lightThreshold);

    oled.drawUTF8(0, 47, buffer);

    oled.drawUTF8(0, 62, "↑↓调整  返回");

    oled.sendBuffer();
}

// ======================================================
// 休息提醒间隔设置
// ======================================================

void drawRestPage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    oled.drawUTF8(0, 12, "休息提醒设置");

    oled.drawHLine(0, 15, 128);

    char buffer[32];

    snprintf(buffer,
             sizeof(buffer),
             "间隔：%d 分钟",
             restRemindMin);

    oled.drawUTF8(0, 31, buffer);

    snprintf(buffer,
             sizeof(buffer),
             "范围：1~%d 分钟",
             REST_REMIND_MAX_MIN);

    oled.drawUTF8(0, 47, buffer);

    oled.drawUTF8(0, 62, "↑↓调整  返回");

    oled.sendBuffer();
}

// ======================================================
// 专注模式主界面（计时页面）
// ======================================================

// 时长格式化：毫秒 -> "hh:mm:ss"
static void formatDuration(unsigned long ms, char *out, size_t len)
{
    unsigned long s = ms / 1000;

    snprintf(out, len, "%02lu:%02lu:%02lu",
             s / 3600, (s / 60) % 60, s % 60);
}

// 居中绘制一行文字
static void drawCenteredUTF8(int y, const char *text)
{
    int w = oled.getUTF8Width(text);

    oled.drawUTF8((128 - w) / 2, y, text);
}

void drawFocusPage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // ==================================================
    // 外框（与弹框风格统一：圆角矩形贴屏幕最外层）
    // ==================================================

    oled.drawRFrame(0, 0, 128, 64, 4);

    // ==================================================
    // 标签（小字居中）
    // ==================================================

    drawCenteredUTF8(15, "已专注");

    // ==================================================
    // 计时时长（24 号数字字体居中，页面主体）
    // ==================================================

    char duration[16];

    formatDuration(focusElapsedMs(), duration, sizeof(duration));

    oled.setFont(u8g2_font_logisoso24_tf);

    int durWidth = oled.getUTF8Width(duration);

    oled.drawUTF8((128 - durWidth) / 2, 44, duration);

    // ==================================================
    // 底部操作提示（小字居中）：K1/K2 控制灯光风扇，K3/K4 结束专注
    // ==================================================

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    const char *tip = "K3/K4键 结束专注";

    int tipWidth = oled.getUTF8Width(tip);

    oled.drawUTF8((128 - tipWidth) / 2, 58, tip);

    oled.sendBuffer();
}

// ======================================================
// 弹框界面（专注模式交互）
// ======================================================

void drawDialogPage()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // ==================================================
    // 弹框边框（圆角矩形，扩大至屏幕最外层）
    // ==================================================

    oled.drawRFrame(0, 0, 128, 64, 4);

    char duration[16];

    formatDuration(focusElapsedMs(), duration, sizeof(duration));

    char line[32];

    // ==================================================
    // 弹框内容（标题 + 一至两行正文 + 底部按键提示）
    // ==================================================

    // 行坐标：3 行弹框用 20/36/52（等距 16），
    // 4 行弹框（休息提醒）用 17/30/43/56（等距 13），
    // 边框已扩大到屏幕最外层（0,0,128,64），
    // 文字块整体在框内上下居中、左右居中，四边空位对齐
    switch (currentDialog)
    {
    case DIALOG_FOCUS_START:
        drawCenteredUTF8(20, "专注模式");
        drawCenteredUTF8(36, "是否开始计时?");
        drawCenteredUTF8(52, "K3开始  K4取消");
        break;

    case DIALOG_FOCUS_END:
        snprintf(line, sizeof(line), "已专注 %s", duration);
        drawCenteredUTF8(20, "专注中");
        drawCenteredUTF8(36, line);
        drawCenteredUTF8(52, "K3继续  K4结束");
        break;

    case DIALOG_FOCUS_REST:
    {
        // 显示实际已专注分钟数（与网页/小程序 round(focus/60) 一致），
        // 而不是休息提醒间隔 restRemindMin（第 2 次及以后提醒会偏小）
        unsigned long restMin = (focusElapsedMs() + 30000UL) / 60000UL;
        if (restMin < 1)
        {
            restMin = 1;
        }

        drawCenteredUTF8(17, "休息提醒");
        snprintf(line, sizeof(line), "已专注 %lu 分钟", restMin);
        drawCenteredUTF8(30, line);
        drawCenteredUTF8(43, "注意休息!");
        drawCenteredUTF8(56, "K3继续  K4结束");
        break;
    }

    case DIALOG_FOCUS_DONE:
    {
        // 两行布局：标题小字 + 本次时长 24 号大数字，
        // 上下空位对称，无按键提示行（K3/K4 均可返回）
        drawCenteredUTF8(19, "专注结束");

        oled.setFont(u8g2_font_logisoso24_tf);

        int doneWidth = oled.getUTF8Width(duration);

        oled.drawUTF8((128 - doneWidth) / 2, 52, duration);

        oled.setFont(u8g2_font_wqy12_t_gb2312);
    }
    break;

    default:
        break;
    }

    oled.sendBuffer();
}

// ======================================================
// 显示页面
// ======================================================

void updateDisplay()
{
    switch (currentPage)
    {
    case PAGE_MAIN:
        // 专注模式主界面显示计时，其余模式显示常规数据
        if (currentMode == MODE_FOCUS)
        {
            drawFocusPage();
        }
        else
        {
            drawMainPage();
        }
        break;

    case PAGE_MENU:
        drawMenuPage();
        break;

    case PAGE_TEMP:
        drawTemperaturePage();
        break;

    case PAGE_LIGHT:
        drawLightPage();
        break;

    case PAGE_REST:
        drawRestPage();
        break;

    case PAGE_DIALOG:
        drawDialogPage();
        break;

    default:
        break;
    }
}

// ======================================================
// MCU（ESP32）图标
// ======================================================

static void drawMcuIcon(int frame)
{
    // 芯片主体
    oled.drawFrame(1, 24, 32, 18);

    // 上下引脚
    for (int i = 0; i < 4; i++)
    {
        int x = 6 + i * 7;

        oled.drawVLine(x, 20, 4);
        oled.drawVLine(x, 42, 4);
    }

    // ESP32 标识（在芯片框内水平居中）
    oled.setFont(u8g2_font_5x7_tr);

    int espWidth = oled.getStrWidth("ESP32");

    oled.drawStr(1 + (32 - espWidth) / 2, 36, "ESP32");

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // 天线（波纹逐帧向外扩散）
    oled.drawVLine(30, 21, 3);

    oled.drawDisc(30, 20, 1, U8G2_DRAW_ALL);

    int waveR = 2 + frame % 4;

    oled.drawCircle(30, 19, waveR, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
}

// ======================================================
// 云平台（ThingsCloud）图标
// ======================================================

static void drawCloudIcon(int frame)
{
    // 云朵上下轻微浮动
    int dy = (frame / 8) % 2;

    // 云朵轮廓（描边）
    oled.drawHLine(99, 38 + dy, 24);

    oled.drawCircle(105, 38 + dy, 6, U8G2_DRAW_UPPER_LEFT);

    oled.drawCircle(111, 32 + dy, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);

    oled.drawCircle(117, 38 + dy, 6, U8G2_DRAW_UPPER_RIGHT);
}

// ======================================================
// 开机动态对接界面（MCU <-> 云平台）
// ======================================================
// animFrame：动画帧号（每帧递增），用于数据流动画
// ======================================================

void drawCloudConnect(int animFrame, const char *statusText)
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // 标题（顶部居中）
    int titleWidth = oled.getUTF8Width("智能书房");

    oled.drawUTF8((128 - titleWidth) / 2, 11, "智能书房");

    // 云平台图标（右侧，带浮动动画）
    drawCloudIcon(animFrame);

    // MCU 图标（左侧，带天线波纹动画）
    drawMcuIcon(animFrame);

    // 连接虚线（MCU 与云平台之间）
    for (int x = 36; x < 99; x += 6)
    {
        oled.drawHLine(x, 33, 3);
    }

    // ==================================================
    // 数据包流动画（上车道：MCU -> 云，下车道：云 -> MCU）
    // ==================================================

    int phase = animFrame % 30;

    // 上行数据包（含拖尾）
    int upX = 36 + phase * 2;

    oled.drawDisc(upX, 30, 2, U8G2_DRAW_ALL);

    if (upX - 5 >= 36)
    {
        oled.drawDisc(upX - 5, 30, 1, U8G2_DRAW_ALL);
    }

    if (upX - 10 >= 36)
    {
        oled.drawDisc(upX - 10, 30, 1, U8G2_DRAW_ALL);
    }

    // 下行数据包（与上行交错半个周期）
    int downPhase = (phase + 15) % 30;

    int downX = 94 - downPhase * 2;

    oled.drawDisc(downX, 36, 2, U8G2_DRAW_ALL);

    if (downX + 5 <= 94)
    {
        oled.drawDisc(downX + 5, 36, 1, U8G2_DRAW_ALL);
    }

    if (downX + 10 <= 94)
    {
        oled.drawDisc(downX + 10, 36, 1, U8G2_DRAW_ALL);
    }

    // ==================================================
    // 状态文字（居中）+ 动态省略号
    // ==================================================

    // 仅按文字宽度居中（省略号不参与居中计算）
    int textWidth = oled.getUTF8Width(statusText);

    int x = (128 - textWidth) / 2;

    oled.drawUTF8(x, 56, statusText);

    // 省略号逐个点亮循环
    int dots = (animFrame / 8) % 4;

    for (int i = 0; i < dots; i++)
    {
        oled.drawDisc(x + textWidth + 3 + i * 5, 52, 1, U8G2_DRAW_ALL);
    }

    oled.sendBuffer();
}

// ======================================================
// 云平台对接成功界面
// ======================================================

void drawCloudConnected(const char *ip)
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // 对勾图标（圆环 + 勾）
    oled.drawCircle(64, 13, 8, U8G2_DRAW_ALL);

    oled.drawLine(60, 13, 63, 16);
    oled.drawLine(63, 16, 69, 10);

    // 标题（居中）
    int titleWidth = oled.getUTF8Width("云平台对接成功");

    oled.drawUTF8((128 - titleWidth) / 2, 38, "云平台对接成功");

    // IP 地址（居中）
    int ipWidth = oled.getUTF8Width(ip);

    oled.drawUTF8((128 - ipWidth) / 2, 56, ip);

    oled.sendBuffer();
}

// ======================================================
// 云平台对接失败界面
// ======================================================

void drawCloudFailed()
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_wqy12_t_gb2312);

    // 叉号图标（圆环 + 叉）
    oled.drawCircle(64, 13, 8, U8G2_DRAW_ALL);

    oled.drawLine(60, 9, 68, 17);
    oled.drawLine(68, 9, 60, 17);

    // 标题（居中）
    int titleWidth = oled.getUTF8Width("云平台对接失败");

    oled.drawUTF8((128 - titleWidth) / 2, 38, "云平台对接失败");

    // 提示（居中）
    int textWidth = oled.getUTF8Width("请检查WiFi设置");

    oled.drawUTF8((128 - textWidth) / 2, 56, "请检查WiFi设置");

    oled.sendBuffer();
}
