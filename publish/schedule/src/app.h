/* app.h — 全局应用上下文与跨模块约定 */
#ifndef APP_H
#define APP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
/* 注：SDK 的 gdiplus 头依赖 C++ 语义（enum 类型名），C 模式不可用；
 * 本项目在 render.h 内自带最小 GDI+ flat API 声明 */

#include "util.h"
#include "schedule.h"

/* ---- 应用内消息 ---- */
#define WM_APP_TRAY      (WM_APP + 1)  /* 托盘回调, lParam 为 NIN_* */

/* ---- 定时器 ---- */
#define TIMER_FULLSCREEN 1   /* 1s: 全屏检测 + 桌面层钉扎续期 */
#define TIMER_REMIND     2   /* 30s: 提醒扫描 */
#define TIMER_CLOCK      3   /* 60s: 月历/倒计时刷新 */
#define TIMER_PILL       4   /* 收起态 pill 多日程自动轮播 */

/* ---- 调色板（ARGB）---- */
#define PAL_COUNT 8
static const DWORD PAL_ARGB[PAL_COUNT] = {
    0xFF4F86F7, 0xFF34A853, 0xFFF4B400, 0xFFEA4335,
    0xFF9C27B0, 0xFF00BCD4, 0xFFFF7043, 0xFF7E57C2
};
#define PAL2REF(i) RGB((PAL_ARGB[i]>>16)&0xFF, (PAL_ARGB[i]>>8)&0xFF, PAL_ARGB[i]&0xFF)

/* ---- 界面配色（ARGB，运行时跟随系统主题，定义见 theme.c）---- */
extern DWORD g_colBg, g_colCard, g_colTxt, g_colSub,
             g_colAccent, g_colLine, g_colInput;
#define COL_BG      g_colBg
#define COL_CARD    g_colCard
#define COL_TXT     g_colTxt
#define COL_SUB     g_colSub
#define COL_ACCENT  g_colAccent
#define COL_LINE    g_colLine
#define COL_INPUT   g_colInput

typedef struct Settings {
    int     autostart;       /* 开机自启 */
    int     defRemind;       /* 新事件默认提醒分钟, <0 不提醒 */
    int     posX, posY;      /* 自定义窗口位置, -1 = 未设置 */
    int     collapsed;       /* 启动时收起为 pill */
} Settings;

typedef struct App {
    HINSTANCE hInst;
    HWND      hwndMain;
    HICON     hIcon;
    wchar_t   dir[MAX_PATH];      /* %APPDATA%\ScheduleWidget */
    wchar_t   exePath[MAX_PATH];
    Settings  st;
} App;

extern App g;

#endif
