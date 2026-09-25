/* notify.h — 系统托盘 与 事件提醒扫描 */
#ifndef NOTIFY_H
#define NOTIFY_H

#include <windows.h>

void tray_init(void);            /* 注册 TaskbarCreated 并添加图标 */
void tray_add(void);             /* （重建）托盘图标 */
void tray_remove(void);
void tray_on_msg(LPARAM lp);     /* WM_APP_TRAY 回调处理 */
UINT  tray_taskbar_msg(void);    /* TaskbarCreated 消息号 */
void notify_scan(void);          /* TIMER_REMIND：扫描到期事件弹气泡 */

#endif
