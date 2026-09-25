/* fullscreen.c — 桌面层钉扎续期（每秒把组件钉到 Progman 正上方） */
#include "app.h"
#include "fullscreen.h"
#include "widget.h"
#include <string.h>

/* 桌面层模式下不需要全屏检测/隐藏/恢复：
 * 组件位于所有应用窗口之下，全屏应用自然覆盖它，无需 SW_HIDE。 */

void fullscreen_tick(void)
{
    if (IsWindowVisible(g.hwndMain))
        widget_pin_desktop();
}
