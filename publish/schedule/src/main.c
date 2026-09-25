/* main.c — 入口：单实例、COM/通用控件、GDI+、初始化链与消息循环 */
#include "app.h"
#include "render.h"
#include "storage.h"
#include "theme.h"
#include "widget.h"
#include "notify.h"
#include <stdlib.h>

App g;

static const wchar_t *MUTEX_NAME = L"Local\\ScheduleWidget_Mutex";

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    (void)hPrev; (void)cmdLine; (void)nShow;
    g.hInst = hInst;

    /* 深色模式必须在任何通用控件/主题绘制之前生效，ComboBox 等才会整体深色 */
    theme_reload(1);
    theme_dark_mode_init();

    /* 单实例：已运行则唤起既有窗口 */
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(L"SW_MAIN_WIN", NULL);
        if (old) {
            ShowWindow(old, SW_SHOW);
            SetForegroundWindow(old);
        }
        CloseHandle(mutex);
        return 0;
    }

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
        return 1;
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_DATE_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    g.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON),
                                GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);

    if (!gfx_init()) {
        CoUninitialize();
        return 1;
    }

    schedule_init();
    storage_init();     /* g.dir / g.st / 事件 */
    schedule_purge_expired();   /* 启动即清理已过期的非循环日程 */
    widget_create();    /* 主窗 + 定时器 */
    tray_init();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    settings_save();
    gfx_shutdown();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    CoUninitialize();
    return 0;
}
