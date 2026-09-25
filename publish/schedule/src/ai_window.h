/* ai_window.h — 日程文件导入窗口（普通顶层窗口，可移动/缩放，关闭即隐藏） */
#ifndef AI_WINDOW_H
#define AI_WINDOW_H

#include <windows.h>

void ai_window_toggle(void);   /* 可见则隐藏，否则显示（首次显示时定位） */
HWND ai_window_hwnd(void);     /* 窗口句柄（未创建返回 NULL） */
int  ai_window_visible(void);  /* 是否已创建且可见 */
void ai_window_refresh(void);  /* 状态变化：同步导入按钮可用性并重绘 */

#endif
