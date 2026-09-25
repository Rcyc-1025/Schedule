/* widget.h — 主窗口（无边框桌面层组件 / 月历议程 / 收起 pill） */
#ifndef WIDGET_H
#define WIDGET_H

#include <windows.h>

void widget_create(void);             /* 创建主窗并按设置定位显示 */
void widget_refresh(void);            /* 数据变化后整体重绘（含 AI 窗口滚到底） */
void widget_apply_pos(void);          /* 按设置或默认规则（右下角）摆放 */
void widget_pin_desktop(void);        /* 桌面层钉扎：钉到桌面之上、一切普通窗口之下 */
void widget_toggle_visible(void);     /* 托盘左键：显示/隐藏 */
void widget_set_collapsed(int c);     /* 1=收起为 pill 0=展开（保持右下角锚点） */
void widget_open_menu(void);          /* ⋯ / 托盘右键菜单 */

#endif
