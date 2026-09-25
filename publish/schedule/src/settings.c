/* settings.c — 设置对话框：开机自启、默认提醒 */
#include "app.h"
#include "settings.h"
#include "editor.h"
#include "storage.h"
#include "widget.h"
#include "theme.h"
#include <windowsx.h>

enum {
    IDS_CHK_AUTO = 101, IDS_REMIND = 102, IDS_SAVE = 103
};

static const int REMIND_V[7] = { -1, 0, 5, 15, 30, 60, 1440 };
static const wchar_t *REMIND_T[7] = {
    L"不提醒", L"准时提醒", L"提前 5 分钟", L"提前 15 分钟",
    L"提前 30 分钟", L"提前 1 小时", L"提前 1 天"
};

static HWND s_hSet;
static HWND s_hChkAuto, s_hRemind;
static HBRUSH s_setDlgBrush = NULL;

static int remind_index(int minutes)
{
    for (int i = 0; i < 7; i++)
        if (REMIND_V[i] == minutes) return i;
    if (minutes < 0) return 0;
    if (minutes == 0) return 1;
    if (minutes <= 5) return 2;
    if (minutes <= 15) return 3;
    if (minutes <= 30) return 4;
    if (minutes <= 60) return 5;
    return 6;
}

static void on_save(void)
{
    g.st.autostart = dlg_check_get(s_hChkAuto);
    int ri = ComboBox_GetCurSel(s_hRemind);
    if (ri >= 0 && ri < 7) g.st.defRemind = REMIND_V[ri];
    autostart_set(g.st.autostart);
    settings_save();
    widget_refresh();
    DestroyWindow(s_hSet);
}

static LRESULT CALLBACK set_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDS_SAVE:
            if (HIWORD(wp) == BN_CLICKED) { on_save(); return 0; }
            break;
        case IDCANCEL:
            DestroyWindow(dlg);
            return 0;
        }
        break;
    case WM_CTLCOLORDLG:
        return (LRESULT)s_setDlgBrush;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB((COL_TXT >> 16) & 0xFF,
                                  (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        return (LRESULT)s_setDlgBrush;
    case WM_CTLCOLORBTN:   /* 复选框：透明背景，文字随主题 */
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB((COL_TXT >> 16) & 0xFF,
                                  (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        return (LRESULT)s_setDlgBrush;
    case WM_DESTROY:
        s_hSet = NULL;
        if (s_setDlgBrush) { DeleteObject(s_setDlgBrush); s_setDlgBrush = NULL; }
        RemoveWindowSubclass(dlg, set_proc, 1);
        break;
    }
    return DefSubclassProc(dlg, msg, wp, lp);
}

void settings_open(HWND owner)
{
    if (s_hSet) {
        SetForegroundWindow(s_hSet);
        return;
    }
    /* 高度含标题栏（约 30 基准 px），控件自客户区起排，故补偿标题栏高度 */
    HWND dlg = dlg_base_create(owner, L"设置", 400, 210);
    if (!dlg) return;
    s_hSet = dlg;
    SetWindowSubclass(dlg, set_proc, 1, 0);
    theme_reload(0);
    theme_apply_dark_frame(dlg);
    if (s_setDlgBrush) DeleteObject(s_setDlgBrush);
    s_setDlgBrush = CreateSolidBrush(RGB((COL_CARD >> 16) & 0xFF,
                                         (COL_CARD >> 8) & 0xFF, COL_CARD & 0xFF));

    int y = 26;
    s_hChkAuto = dlg_ctrl(dlg, L"BUTTON", L"开机自启",
                          WS_TABSTOP | BS_AUTOCHECKBOX, 0, IDS_CHK_AUTO, 24, y, 200, 24);
    dlg_check_set(s_hChkAuto, g.st.autostart);

    y += 44;
    dlg_ctrl(dlg, L"STATIC", L"默认提醒", 0, 0, 0, 24, y + 4, 64, 20);
    s_hRemind = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                         0, IDS_REMIND, 96, y, 200, 200);
    for (int i = 0; i < 7; i++) ComboBox_AddString(s_hRemind, REMIND_T[i]);
    ComboBox_SetCurSel(s_hRemind, remind_index(g.st.defRemind));

    y += 48;
    dlg_ctrl(dlg, L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON,
             0, IDS_SAVE, 296, y, 84, 30);

    dlg_run(dlg, owner);
}
