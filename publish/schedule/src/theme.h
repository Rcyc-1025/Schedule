/* theme.h — 系统主题配色：跟随 Windows 浅/深色 + 系统强调色（可由壁纸取色） */
#ifndef THEME_H
#define THEME_H

#include "app.h"

/* 全局配色变量（由 theme_reload() 计算，替代原 #define） */
extern DWORD g_colBg;
extern DWORD g_colCard;
extern DWORD g_colTxt;
extern DWORD g_colSub;
extern DWORD g_colAccent;
extern DWORD g_colLine;
extern DWORD g_colInput;

/* 是否深色模式（供编辑器/设置对话框判断） */
extern int g_themeDark;

/* 读取系统主题并重新计算所有配色；force=1 表示强制应用 DWM 暗色属性 */
void theme_reload(int force);

/* 给指定窗口应用 DWM 暗色标题栏（Win10 1809+/Win11） */
void theme_apply_dark_frame(HWND hwnd);

/* 进程级深色模式：必须在 WinMain 最早、任何控件/主题绘制前调用一次 */
void theme_dark_mode_init(void);

/* 让经典控件（如 ComboBox 箭头按钮/下拉列表）跟随深色（Win10 1903+） */
void theme_enable_dark_controls(HWND hwnd);

#endif
