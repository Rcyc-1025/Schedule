/* picker.h — 自绘日期/时间选择器（替代 SysDateTimePick32）
 * 背景：深色模式下 DTP 控件本身无法被未文档化 API 染深，且仅在窗口中
 * 创建 DTP 子控件就会导致该顶层窗口处于前台时 DWM 深色标题栏失效（24H2
 * 实测，各种扩展样式与 uxtheme 133/135/132 组合均无效）。故完全自绘，杜绝
 * 该副作用。 */
#ifndef PICKER_H
#define PICKER_H

#include <windows.h>

void picker_register(void);

#define PICK_CLASS L"W_DPK"   /* 收起态控件窗口类；窗口名 "date" / "time" */

/* PKM_SET: lParam = const SYSTEMTIME*，写入当前值（控件只取自身字段） */
#define PKM_SET (WM_APP + 50)
/* PKM_GET: lParam = SYSTEMTIME*，读出完整值；返回 TRUE */
#define PKM_GET (WM_APP + 51)

#endif
