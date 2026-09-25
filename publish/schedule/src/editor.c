/* editor.c — 事件编辑对话框（标题/起止/循环/提醒/颜色/备注）+ 模态对话框公共基础 */
#include "app.h"
#include "editor.h"
#include "widget.h"
#include "storage.h"
#include "theme.h"
#include "picker.h"
#include <commctrl.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 模态对话框公共基础 ================= */

static const wchar_t *WC_DLG = L"W_DLG";
static BOOL s_dlgClassReg = FALSE;
static HFONT s_uiFont;
static UINT  s_uiFontDpi;

/* 公共对话框背景刷（COL_CARD，主题切换时重建）。
 * W_DLG 是 CreateWindowEx 创建的普通窗口，系统不会发送 WM_CTLCOLORDLG，
 * 背景由 WM_ERASEBKGND 决定，故在此统一按主题色擦除。 */
static HBRUSH s_dlgBgBrush = NULL;
static DWORD  s_dlgBgCol   = 0xFFFFFFFF;

static HBRUSH dlg_bg_brush(void)
{
    if (s_dlgBgCol != COL_CARD) {
        if (s_dlgBgBrush) DeleteObject(s_dlgBgBrush);
        s_dlgBgBrush = CreateSolidBrush(RGB((COL_CARD >> 16) & 0xFF,
                                            (COL_CARD >> 8) & 0xFF,
                                            COL_CARD & 0xFF));
        s_dlgBgCol = COL_CARD;
    }
    return s_dlgBgBrush;
}

/* ================= 深色 owner-draw 控件 =================
 * 经典 comctl32 控件（按钮/复选框/分组框/下拉框）在深色主题下仍是白底，
 * 统一改为 owner-draw，按调色板自绘，所有模态对话框共用。 */

#define OD_KIND_PROP L"sw_odkind"
#define OD_CHECK_PROP L"sw_odcheck"

/* 复选框状态（BS_OWNERDRAW 不自动切换，状态显式存窗口 PROP） */
void dlg_check_set(HWND h, int checked)
{
    if (!h) return;
    SetPropW(h, OD_CHECK_PROP, (HANDLE)(INT_PTR)(checked ? 1 : 0));
    InvalidateRect(h, NULL, FALSE);
}

int dlg_check_get(HWND h)
{
    return (h && GetPropW(h, OD_CHECK_PROP)) ? 1 : 0;
}

static void dlg_check_toggle(HWND h)
{
    dlg_check_set(h, dlg_check_get(h) ? 0 : 1);
}
#define OD_DEF_PROP  L"sw_oddef"
enum { OD_PUSH = 1, OD_CHECK = 2, OD_GROUP = 3 };

static COLORREF od_ref(DWORD argb)
{
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

static void od_fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void od_frame(HDC dc, const RECT *r, COLORREF border, COLORREF fill, int rad)
{
    HBRUSH bf = CreateSolidBrush(fill);
    HPEN pn = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, bf);
    HGDIOBJ op = SelectObject(dc, pn);
    RoundRect(dc, r->left, r->top, r->right + 1, r->bottom + 1, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(bf);
    DeleteObject(pn);
}

static void od_text(HDC dc, const wchar_t *s, const RECT *r,
                    COLORREF col, UINT fmt)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    DrawTextW(dc, s, -1, (RECT *)r, fmt | DT_NOPREFIX);
}

static void od_draw_push(HDC dc, DRAWITEMSTRUCT *d)
{
    int def = (int)(INT_PTR)GetPropW(d->hwndItem, OD_DEF_PROP);
    int down = (d->itemState & ODS_SELECTED) != 0;
    COLORREF bg, fg = RGB(0xFF, 0xFF, 0xFF), bc;
    if (def || down) {
        bg = od_ref(COL_ACCENT); bc = od_ref(COL_ACCENT);
    } else if (g_themeDark) {
        bg = RGB(0x35, 0x37, 0x3C); bc = od_ref(COL_LINE);
        fg = od_ref(COL_TXT);
    } else {
        bg = RGB(0xF0, 0xF1, 0xF3); bc = od_ref(COL_LINE);
        fg = od_ref(COL_TXT);
    }
    if (d->itemState & ODS_DISABLED) {
        bg = g_themeDark ? RGB(0x2A, 0x2C, 0x30) : RGB(0xF5, 0xF6, 0xF8);
        fg = od_ref(COL_SUB);
    }
    od_frame(dc, &d->rcItem, bc, bg, 12);
    wchar_t txt[128]; GetWindowTextW(d->hwndItem, txt, 128);
    RECT r = d->rcItem;
    od_text(dc, txt, &r, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void od_draw_check(HDC dc, DRAWITEMSTRUCT *d)
{
    int dpi = GetDpiForWindow(d->hwndItem);
    int box = MulDiv(16, dpi, 96);
    od_fill(dc, &d->rcItem, od_ref(COL_CARD));

    RECT b;
    b.left = d->rcItem.left + MulDiv(2, dpi, 96);
    b.top  = d->rcItem.top + (d->rcItem.bottom - d->rcItem.top - box) / 2;
    b.right = b.left + box; b.bottom = b.top + box;
    int checked = GetPropW(d->hwndItem, OD_CHECK_PROP) != NULL;
    COLORREF boxBg = checked ? od_ref(COL_ACCENT) : od_ref(COL_INPUT);
    od_frame(dc, &b, checked ? od_ref(COL_ACCENT) : od_ref(COL_LINE), boxBg, 4);
    if (checked) {
        HPEN pn = CreatePen(PS_SOLID, MulDiv(2, dpi, 96), RGB(0xFF, 0xFF, 0xFF));
        HGDIOBJ op = SelectObject(dc, pn);
        POINT pts[3] = {
            { b.left + box/6,      b.top + box/2 },
            { b.left + box*4/10,   b.bottom - box/4 },
            { b.right - box/6,     b.top + box/4 }
        };
        Polyline(dc, pts, 3);
        SelectObject(dc, op); DeleteObject(pn);
    }
    wchar_t txt[128]; GetWindowTextW(d->hwndItem, txt, 128);
    RECT r = d->rcItem;
    r.left = b.right + MulDiv(8, dpi, 96);
    od_text(dc, txt, &r,
            (d->itemState & ODS_DISABLED) ? od_ref(COL_SUB) : od_ref(COL_TXT),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void od_draw_group(HDC dc, DRAWITEMSTRUCT *d)
{
    int dpi = GetDpiForWindow(d->hwndItem);
    od_fill(dc, &d->rcItem, od_ref(COL_CARD));
    wchar_t txt[128]; GetWindowTextW(d->hwndItem, txt, 128);
    HFONT f = (HFONT)SendMessageW(d->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz; GetTextExtentPoint32W(dc, txt, (int)wcslen(txt), &sz);
    SelectObject(dc, of);
    int pad = MulDiv(8, dpi, 96);
    RECT line = d->rcItem;
    line.top += sz.cy / 2;
    HPEN pn = CreatePen(PS_SOLID, 1, od_ref(COL_LINE));
    HGDIOBJ op = SelectObject(dc, pn);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, line.left, line.top, line.right + 1, line.bottom + 1, 8, 8);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pn);
    RECT cap;
    cap.left = d->rcItem.left + pad;
    cap.top = d->rcItem.top;
    cap.right = cap.left + sz.cx + MulDiv(8, dpi, 96);
    cap.bottom = d->rcItem.top + sz.cy;
    od_fill(dc, &cap, od_ref(COL_CARD));
    RECT tr = cap; tr.left += MulDiv(4, dpi, 96);
    od_text(dc, txt, &tr, od_ref(COL_SUB), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* ---------- ComboBox 完全自绘 ----------
 * uxtheme 未文档化深色 API 在 Win11 24H2 对 ComboBox 不生效（实测），且本版
 * comctl32 对 CBS_DROPDOWNLIST 不发送 itemID=-1 的 WM_DRAWITEM（静态区无
 * 法靠 owner-draw 上色）。因此：列表项用 owner-draw（WM_DRAWITEM item>=0），
 * 收起态整体（含箭头按钮）由子类过程接管 WM_PAINT 自绘。 */
#define COMBO_SUBCLASS_ID 2
#define GROUP_SUBCLASS_ID 3

/* 分组框点击穿透：WM_NCHITTEST 返回 HTTRANSPARENT，鼠标事件落到对话框 */
static LRESULT CALLBACK group_sub_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, group_sub_proc, GROUP_SUBCLASS_ID);
    return DefSubclassProc(h, msg, wp, lp);
}

static void od_draw_combo_item(HDC dc, DRAWITEMSTRUCT *d)
{
    int dpi = GetDpiForWindow(d->hwndItem);
    int sel = (d->itemState & ODS_SELECTED) != 0;
    od_fill(dc, &d->rcItem, sel ? od_ref(COL_ACCENT) : od_ref(COL_CARD));
    /* 只能用 CB_GETLBTEXT 取文本：CBS_HASSTRINGS 下 itemData 指向 combo
     * 内部缓冲，CB_RESETCONTENT 等时机可能是已释放的悬空指针（实测在重置
     * 时触发重绘会直接在 DrawTextW 访问违例）。itemID==-1 是收起态重绘，
     * 文本由 combo_sub_proc 的 WM_PAINT 自绘，这里留空即可。 */
    wchar_t stackbuf[256] = {0};
    const wchar_t *txt = stackbuf;
    if (d->itemID != (UINT)-1)
        SendMessageW(d->hwndItem, CB_GETLBTEXT,
                     (WPARAM)d->itemID, (LPARAM)stackbuf);
    RECT r = d->rcItem;
    r.left += MulDiv(10, dpi, 96);
    r.right -= MulDiv(6, dpi, 96);
    od_text(dc, txt, &r, sel ? RGB(0xFF, 0xFF, 0xFF) : od_ref(COL_TXT),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* 收起态自绘：外框 + 当前选项文本 + 右侧箭头 */
static void combo_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    int dpi = GetDpiForWindow(h);
    RECT rc; GetClientRect(h, &rc);

    od_frame(dc, &rc, od_ref(COL_LINE), od_ref(COL_INPUT),
             MulDiv(4, dpi, 96));

    /* 右侧箭头：Win11 风格无边框分隔，灰色小三角 */
    int btnW = rc.bottom - rc.top;
    int cx = rc.right - btnW / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int tri = MulDiv(5, dpi, 96);
    POINT pts[3] = {
        { cx - tri,     cy - tri / 2 },
        { cx + tri,     cy - tri / 2 },
        { cx,           cy + tri / 2 }
    };
    HBRUSH tb = CreateSolidBrush(od_ref(COL_SUB));
    HGDIOBJ ob = SelectObject(dc, tb);
    Polygon(dc, pts, 3);
    SelectObject(dc, ob);
    DeleteObject(tb);

    /* 当前选项文本 */
    wchar_t buf[256] = {0};
    int idx = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (idx != CB_ERR)
        SendMessageW(h, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)buf);
    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ of = SelectObject(dc, f);
    RECT tr;
    tr.left   = rc.left + MulDiv(10, dpi, 96);
    tr.top    = rc.top;
    tr.right  = rc.right - btnW - MulDiv(4, dpi, 96);
    tr.bottom = rc.bottom;
    od_text(dc, buf, &tr, od_ref(COL_TXT),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK combo_sub_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        combo_paint(h);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_LBUTTONUP:
    case WM_KEYUP: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return r;
    }
    case CB_SETCURSEL: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return r;
    }
    case WM_DESTROY:
        RemoveWindowSubclass(h, combo_sub_proc, COMBO_SUBCLASS_ID);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK dlg_base_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) {
            /* OD_CHECK 复选框无自动切换（BS_AUTOCHECKBOX 被 BS_OWNERDRAW
             * 覆盖），点击时在此统一翻转状态 */
            HWND cb = GetDlgItem(h, LOWORD(wp));
            if (cb && (int)(INT_PTR)GetPropW(cb, OD_KIND_PROP) == OD_CHECK)
                dlg_check_toggle(cb);
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, dlg_bg_brush());
        return 1;
    }
    case WM_CTLCOLORLISTBOX:
        /* ComboBox 弹出列表底色兜底 */
        SetBkColor((HDC)wp, od_ref(COL_CARD));
        SetTextColor((HDC)wp, od_ref(COL_TXT));
        return (LRESULT)dlg_bg_brush();
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_COMBOBOX) {
            HWND cw = GetDlgItem(h, (int)mi->CtlID);
            int dpi = cw ? GetDpiForWindow(cw) : 96;
            mi->itemHeight = MulDiv(24, dpi, 96);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        if (!d) break;
        if (d->CtlType == ODT_COMBOBOX) {
            od_draw_combo_item(d->hDC, d);
            return TRUE;
        }
        HFONT f = (HFONT)SendMessageW(d->hwndItem, WM_GETFONT, 0, 0);
        HGDIOBJ of = SelectObject(d->hDC, f);
        int kind = (int)(INT_PTR)GetPropW(d->hwndItem, OD_KIND_PROP);
        if (kind == OD_CHECK)      od_draw_check(d->hDC, d);
        else if (kind == OD_GROUP) od_draw_group(d->hDC, d);
        else if (kind == OD_PUSH)  od_draw_push(d->hDC, d);
        else {
            /* 非本对话框自绘体系的 WM_DRAWITEM（如组合框内部 listbox 的
             * ODT_LISTBOX）：交回默认处理，切勿当按钮绘制（其 hwndItem 无
             * OD_KIND、字体/DC 不属于本窗口，会在 DrawTextW 处访问违例） */
            SelectObject(d->hDC, of);
            break;
        }
        SelectObject(d->hDC, of);
        return TRUE;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void dlg_register_class(void)
{
    if (s_dlgClassReg) return;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = dlg_base_proc;
    wc.hInstance = g.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;           /* 背景改由 WM_ERASEBKGND 按主题填充 */
    wc.lpszClassName = WC_DLG;
    RegisterClassW(&wc);
    s_dlgClassReg = TRUE;
}

HFONT dlg_font(HWND w)
{
    UINT dpi = GetDpiForWindow(w);
    if (!dpi) dpi = 96;
    if (s_uiFont && s_uiFontDpi == dpi) return s_uiFont;
    if (s_uiFont) DeleteObject(s_uiFont);
    s_uiFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    s_uiFontDpi = dpi;
    return s_uiFont;
}

int dlg_dpi(HWND w)
{
    UINT dpi = GetDpiForWindow(w);
    return dpi ? (int)dpi : 96;
}

HWND dlg_base_create(HWND owner, const wchar_t *title, int baseW, int baseH)
{
    dlg_register_class();
    int dpi = owner ? dlg_dpi(owner) : 96;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, WC_DLG, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        0, 0, baseW * dpi / 96, baseH * dpi / 96, owner, NULL, g.hInst, NULL);
    if (dlg) {
        /* 统一对所有模态对话框应用深色标题栏（24H2 需在创建后尽早设置） */
        theme_reload(0);
        theme_apply_dark_frame(dlg);
        dlg_center(dlg, owner);
    }
    return dlg;
}

void dlg_run(HWND dlg, HWND owner)
{
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    theme_apply_dark_frame(dlg);   /* 显示后再确认一次深色标题栏 */
    run_modal(dlg);
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

HWND dlg_ctrl(HWND parent, const wchar_t *cls, const wchar_t *text,
              DWORD style, DWORD exStyle, int id, int x, int y, int w, int h)
{
    int sc = dlg_dpi(parent);
    int odKind = 0, odDef = 0;
    int isCombo = wcscmp(cls, L"COMBOBOX") == 0;
    if (wcscmp(cls, L"BUTTON") == 0) {
        DWORD bs = style & 0x0000000F;
        if (bs == BS_GROUPBOX)            odKind = OD_GROUP;
        else if (bs == BS_CHECKBOX || bs == BS_AUTOCHECKBOX ||
                 bs == BS_3STATE || bs == BS_AUTO3STATE) odKind = OD_CHECK;
        else { odKind = OD_PUSH; odDef = (bs == BS_DEFPUSHBUTTON); }
        style = (style & ~0x0000000F) | BS_OWNERDRAW;
    } else if (isCombo) {
        /* 列表项 owner-draw；收起态由 combo_sub_proc 接管 WM_PAINT */
        style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    }
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x * sc / 96, y * sc / 96, w * sc / 96, h * sc / 96,
                             parent, (HMENU)(INT_PTR)id, g.hInst, NULL);
    if (c) {
        SendMessageW(c, WM_SETFONT, (WPARAM)dlg_font(parent), TRUE);
        if (odKind) {
            SetPropW(c, OD_KIND_PROP, (HANDLE)(INT_PTR)odKind);
            if (odDef) SetPropW(c, OD_DEF_PROP, (HANDLE)1);
            if (odKind == OD_GROUP) {
                /* 分组框纯装饰：仅拦截点击（WM_NCHITTEST 穿透，效果等同
                 * STATIC），不改绘制顺序——WS_EX_TRANSPARENT 会让分组框
                 * 最后绘制，自绘背景盖住框内控件 */
                SetWindowSubclass(c, group_sub_proc, GROUP_SUBCLASS_ID, 0);
            }
        }
        if (isCombo) {
            theme_enable_dark_controls(c);
            SetWindowSubclass(c, combo_sub_proc, COMBO_SUBCLASS_ID, 0);
        }
    }
    return c;
}

static void dlg_move(HWND ctrl, int x, int y, int w, int h)
{
    if (!ctrl) return;
    int sc = dlg_dpi(ctrl);
    SetWindowPos(ctrl, NULL, x * sc / 96, y * sc / 96, w * sc / 96, h * sc / 96,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ================= 颜色 swatch 子控件 ================= */

static const wchar_t *WC_SWATCH = L"W_SWATCH";
static BOOL s_swatchReg = FALSE;

/* 定义在后方 editor 状态区，前置声明供 swatch_proc 使用 */
int  ed_swatch_selected(HWND h, int idx);
void ed_swatch_click(HWND h, int idx);

static LRESULT CALLBACK swatch_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        int idx = (int)(INT_PTR)GetWindowLongPtrW(h, GWLP_USERDATA);
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB((COL_CARD >> 16) & 0xFF,
                                          (COL_CARD >> 8) & 0xFF, COL_CARD & 0xFF));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        int selected = ed_swatch_selected(h, idx);
        HBRUSH br = CreateSolidBrush(PAL2REF(idx % PAL_COUNT));
        HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1,
                             selected ? RGB(0x3D, 0x7E, 0xFF) : RGB(0xE8, 0xEA, 0xEF));
        HBRUSH obr = (HBRUSH)SelectObject(dc, br);
        HPEN opn = (HPEN)SelectObject(dc, pen);
        Ellipse(dc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
        SelectObject(dc, obr);
        SelectObject(dc, opn);
        DeleteObject(br);
        DeleteObject(pen);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        ed_swatch_click(h, (int)(INT_PTR)GetWindowLongPtrW(h, GWLP_USERDATA));
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void swatch_register(void)
{
    if (s_swatchReg) return;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = swatch_proc;
    wc.hInstance = g.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_HAND);
    wc.lpszClassName = WC_SWATCH;
    RegisterClassW(&wc);
    s_swatchReg = TRUE;
}

/* ================= 编辑器状态 ================= */

enum {
    IDE_TITLE = 100, IDE_SDATE, IDE_STIME, IDE_ALLDAY, IDE_EMODE, IDE_EDATE, IDE_ETIME,
    IDE_EDUR, IDE_EUNIT, IDE_REC, IDE_RINT, IDE_RUNIT, IDE_RUNTIL, IDE_RUNTILDATE,
    IDE_REMIND, IDE_NOTE, IDE_SAVE, IDE_DELETE,
    IDE_COLOR = 200   /* IDE_COLOR + 0..7 */
};

static const int REMIND_V[7] = { -1, 0, 5, 15, 30, 60, 1440 };
static const wchar_t *REMIND_T[7] = {
    L"不提醒", L"准时提醒", L"提前 5 分钟", L"提前 15 分钟",
    L"提前 30 分钟", L"提前 1 小时", L"提前 1 天"
};
/* 全天日程的提醒以「天」为单位，准点 = 当天 9:00 */
static const int ADREMIND_V[5] = { -1, 0, 1440, 4320, 10080 };
static const wchar_t *ADREMIND_T[5] = {
    L"不提醒", L"当天 9:00 提醒", L"提前 1 天", L"提前 3 天", L"提前 1 周"
};
static const wchar_t *UNIT_T[6] = { L"分钟", L"小时", L"天", L"周", L"月", L"年" };

typedef struct {
    Event ev;
    int   isNew;
    int   color;
    HWND  hTitle, hSDate, hSTime, hAllDay, hEMode, hEDate, hETime, hEDur, hEUnit;
    HWND  hRec, hRInt, hRUnit, hRUntil, hRUntilDtp, hRemind, hNote, hSave, hDel;
    HWND  hSw[8];
    HWND  hLb[7];   /* 左侧标签：标题/开始/结束/循环/提醒/颜色/备注 */
} Ed;
static Ed s_ed;
static HBRUSH s_dlgBrush = NULL;   /* 对话框背景刷（跟随主题） */
static HBRUSH s_editBg  = NULL;   /* 输入框背景刷 */

int ed_swatch_selected(HWND h, int idx)
{
    (void)h;
    return (idx == s_ed.color) ? 1 : 0;
}

void ed_swatch_click(HWND h, int idx)
{
    (void)h;
    s_ed.color = idx;
    for (int i = 0; i < 8; i++)
        if (s_ed.hSw[i]) InvalidateRect(s_ed.hSw[i], NULL, FALSE);
}

/* ---- 日期/时间控件读写（自绘 W_DPK，接口与原 DTP 相同） ---- */
static void dtp_set(HWND d, t64 t)
{
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    int y, m, dd, hh, mm;
    tbreak(t, &y, &m, &dd, &hh, &mm);
    st.wYear = (WORD)y; st.wMonth = (WORD)m; st.wDay = (WORD)dd;
    st.wHour = (WORD)hh; st.wMinute = (WORD)mm;
    SendMessageW(d, PKM_SET, 0, (LPARAM)&st);
}

static t64 dtp_get(HWND d)
{
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    if (!SendMessageW(d, PKM_GET, 0, (LPARAM)&st)) return 0;
    return tmk(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

/* ---- 布局（96 基准坐标，随 结束方式/循环 动态重排） ---- */
static void ed_show(HWND c, int show)
{
    if (c) ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

static void ed_relayout(HWND dlg)
{
    int allDay = dlg_check_get(s_ed.hAllDay);
    int endDur = !allDay && SendMessageW(s_ed.hEMode, CB_GETCURSEL, 0, 0) == 1;
    int custom = SendMessageW(s_ed.hRec, CB_GETCURSEL, 0, 0) == (int)REC_CUSTOM;
    int sc = dlg_dpi(dlg);

    int y = 16;
    dlg_move(s_ed.hLb[0], 16, y + 5, 48, 20);
    ed_show(s_ed.hTitle, 1);
    dlg_move(s_ed.hTitle, 72, y, 280, 26);
    y += 38;
    dlg_move(s_ed.hLb[1], 16, y + 5, 48, 20);
    dlg_move(s_ed.hSDate, 72, y, 108, 26);
    ed_show(s_ed.hSTime, !allDay);
    if (!allDay) dlg_move(s_ed.hSTime, 188, y, 82, 26);
    ed_show(s_ed.hAllDay, 1);
    dlg_move(s_ed.hAllDay, 280, y, 72, 26);
    y += 38;
    dlg_move(s_ed.hLb[2], 16, y + 5, 48, 20);
    if (allDay) {
        /* 全天：结束只到日期（含当天），不显示结束方式/时刻/时长 */
        ed_show(s_ed.hEMode, 0); ed_show(s_ed.hETime, 0);
        ed_show(s_ed.hEDur, 0);  ed_show(s_ed.hEUnit, 0);
        ed_show(s_ed.hEDate, 1);
        dlg_move(s_ed.hEDate, 72, y, 108, 26);
        y += 38;
    } else {
        ed_show(s_ed.hEMode, 1);
        dlg_move(s_ed.hEMode, 72, y, 198, 160);
        y += 34;
        if (endDur) {
            ed_show(s_ed.hEDate, 0); ed_show(s_ed.hETime, 0);
            ed_show(s_ed.hEDur, 1);  ed_show(s_ed.hEUnit, 1);
            dlg_move(s_ed.hEDur, 72, y, 60, 26);
            dlg_move(s_ed.hEUnit, 140, y, 130, 160);
        } else {
            ed_show(s_ed.hEDate, 1); ed_show(s_ed.hETime, 1);
            ed_show(s_ed.hEDur, 0);  ed_show(s_ed.hEUnit, 0);
            dlg_move(s_ed.hEDate, 72, y, 108, 26);
            dlg_move(s_ed.hETime, 188, y, 82, 26);
        }
        y += 38;
    }
    dlg_move(s_ed.hLb[3], 16, y + 5, 48, 20);
    dlg_move(s_ed.hRec, 72, y, 198, 200);
    if (custom) {
        y += 34;
        ed_show(s_ed.hRInt, 1); ed_show(s_ed.hRUnit, 1);
        dlg_move(s_ed.hRInt, 72, y, 60, 26);
        dlg_move(s_ed.hRUnit, 140, y, 130, 160);
        y += 34;
        ed_show(s_ed.hRUntil, 1); ed_show(s_ed.hRUntilDtp, 1);
        dlg_move(s_ed.hRUntil, 72, y, 120, 160);
        dlg_move(s_ed.hRUntilDtp, 200, y, 108, 26);
        y += 38;
    } else {
        ed_show(s_ed.hRInt, 0); ed_show(s_ed.hRUnit, 0);
        ed_show(s_ed.hRUntil, 0); ed_show(s_ed.hRUntilDtp, 0);
        y += 38;
    }
    dlg_move(s_ed.hLb[4], 16, y + 5, 48, 20);
    dlg_move(s_ed.hRemind, 72, y, 198, 200);
    y += 34;
    dlg_move(s_ed.hLb[5], 16, y + 4, 48, 20);
    for (int i = 0; i < 8; i++)
        dlg_move(s_ed.hSw[i], 72 + i * 30, y, 22, 22);
    y += 34;
    dlg_move(s_ed.hLb[6], 16, y + 5, 48, 20);
    dlg_move(s_ed.hNote, 72, y, 280, 72);
    y += 84;
    dlg_move(s_ed.hDel, 180, y, 76, 30);
    dlg_move(s_ed.hSave, 268, y, 84, 30);
    y += 48;

    /* y 是客户区内容高度；窗口总高还需补偿标题栏+边框（非客户区），
     * 否则 150% DPI 下底部"保存"按钮会被裁掉 */
    RECT rc0 = { 0, 0, 0, 0 };
    AdjustWindowRectEx(&rc0, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                       FALSE, WS_EX_DLGMODALFRAME);
    int frameH = rc0.bottom - rc0.top;
    SetWindowPos(dlg, NULL, 0, 0, 384 * sc / 96, y * sc / 96 + frameH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    /* SetWindowPos 改变窗口尺寸后 DWM 会重置标题栏深色，需重新应用 */
    theme_apply_dark_frame(dlg);
    /* 控件大量移动/显隐后，旧位置可能残留像素（STATIC 为 TRANSPARENT
     * 背景、子控件表面被 DWM 复用，仅失效父窗刷不掉 EDIT/STATIC 内部的
     * 残影），必须连同所有子控件一起擦除并立即重绘 */
    RedrawWindow(dlg, NULL, NULL,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
}

/* ---- 提醒下拉：定时/全天两套选项 ---- */
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

/* 分钟数映射到全天提醒档位 */
static int adremind_index(int minutes)
{
    for (int i = 0; i < 5; i++)
        if (ADREMIND_V[i] == minutes) return i;
    if (minutes < 0) return 0;
    if (minutes == 0) return 1;
    if (minutes >= 10080) return 4;
    if (minutes >= 4320)  return 3;
    return 2;                 /* 任意「提前若干分钟/小时」归入提前 1 天 */
}

/* 按当前模式重建提醒下拉并选中最接近 minutes 的档位 */
static void remind_rebuild(int allDay, int minutes)
{
    SendMessageW(s_ed.hRemind, CB_RESETCONTENT, 0, 0);
    if (allDay) {
        for (int i = 0; i < 5; i++)
            ComboBox_AddString(s_ed.hRemind, ADREMIND_T[i]);
        SendMessageW(s_ed.hRemind, CB_SETCURSEL,
                     (WPARAM)adremind_index(minutes), 0);
    } else {
        for (int i = 0; i < 7; i++)
            ComboBox_AddString(s_ed.hRemind, REMIND_T[i]);
        SendMessageW(s_ed.hRemind, CB_SETCURSEL,
                     (WPARAM)remind_index(minutes), 0);
    }
}

/* 读取当前下拉（按当前模式）对应的提醒分钟数 */
static int remind_current_minutes(int allDay)
{
    int i = (int)SendMessageW(s_ed.hRemind, CB_GETCURSEL, 0, 0);
    if (allDay) return (i >= 0 && i < 5) ? ADREMIND_V[i] : -1;
    return (i >= 0 && i < 7) ? REMIND_V[i] : -1;
}

/* ---- 载入 ---- */
static void ed_load(HWND dlg)
{
    SetWindowTextW(s_ed.hTitle, s_ed.ev.title);
    dtp_set(s_ed.hSDate, ev_start_t(&s_ed.ev));
    dtp_set(s_ed.hSTime, ev_start_t(&s_ed.ev));
    dtp_set(s_ed.hEDate, ev_end_t(&s_ed.ev));
    dtp_set(s_ed.hETime, ev_end_t(&s_ed.ev));
    dlg_check_set(s_ed.hAllDay, s_ed.ev.allDay);
    SendMessageW(s_ed.hEMode, CB_SETCURSEL, 0, 0);
    SendMessageW(s_ed.hRec, CB_SETCURSEL, (WPARAM)s_ed.ev.rec, 0);
    wchar_t num[16];
    _snwprintf(num, 16, L"%d", s_ed.ev.interval > 0 ? s_ed.ev.interval : 1);
    SetWindowTextW(s_ed.hRInt, num);
    SendMessageW(s_ed.hRUnit, CB_SETCURSEL, (WPARAM)s_ed.ev.unit, 0);
    if (s_ed.ev.until[0]) {
        SendMessageW(s_ed.hRUntil, CB_SETCURSEL, 1, 0);
        dtp_set(s_ed.hRUntilDtp, iso_to_t64(s_ed.ev.until));
    } else {
        SendMessageW(s_ed.hRUntil, CB_SETCURSEL, 0, 0);
        dtp_set(s_ed.hRUntilDtp, tnow());
    }
    EnableWindow(s_ed.hRUntilDtp, SendMessageW(s_ed.hRUntil, CB_GETCURSEL, 0, 0) == 1);
    remind_rebuild(s_ed.ev.allDay, s_ed.ev.remindMinutes);
    SetWindowTextW(s_ed.hNote, s_ed.ev.note);
    s_ed.color = s_ed.ev.colorIdx % PAL_COUNT;
    (void)dlg;
}

/* ---- 保存 ---- */
static void ed_save(HWND dlg)
{
    wchar_t title[128];
    GetWindowTextW(s_ed.hTitle, title, 128);
    const wchar_t *tp = title;
    while (*tp == L' ' || *tp == L'\t') tp++;
    if (!tp[0]) {
        MessageBoxW(dlg, L"请输入标题", L"日程助手", MB_OK | MB_ICONWARNING);
        return;
    }

    int allDay = dlg_check_get(s_ed.hAllDay);

    int y1, m1, d1; tbreak(dtp_get(s_ed.hSDate), &y1, &m1, &d1, NULL, NULL);
    t64 st = tmk(y1, m1, d1, 0, 0);
    if (st == 0) {
        MessageBoxW(dlg, L"开始日期无效", L"日程助手", MB_OK | MB_ICONWARNING);
        return;
    }

    t64 et;
    if (allDay) {
        /* 全天：结束只取日期（含当天），无时刻 */
        int y2, m2, d2; tbreak(dtp_get(s_ed.hEDate), &y2, &m2, &d2, NULL, NULL);
        et = tmk(y2, m2, d2, 0, 0);
        if (et == 0) {
            MessageBoxW(dlg, L"结束日期无效", L"日程助手", MB_OK | MB_ICONWARNING);
            return;
        }
    } else if (SendMessageW(s_ed.hEMode, CB_GETCURSEL, 0, 0) == 1) {
        int hh1, mm1; tbreak(dtp_get(s_ed.hSTime), NULL, NULL, NULL, &hh1, &mm1);
        st = tmk(y1, m1, d1, hh1, mm1);
        wchar_t dur[16]; GetWindowTextW(s_ed.hEDur, dur, 16);
        long long n = _wtoi(dur);
        if (n < 1) n = 1;
        TimeUnit u = (TimeUnit)SendMessageW(s_ed.hEUnit, CB_GETCURSEL, 0, 0);
        if (u < U_MIN || u > U_YEAR) u = U_HOUR;
        et = tadd_unit(st, n, u);
    } else {
        int hh1, mm1; tbreak(dtp_get(s_ed.hSTime), NULL, NULL, NULL, &hh1, &mm1);
        st = tmk(y1, m1, d1, hh1, mm1);
        if (st == 0) {
            MessageBoxW(dlg, L"开始时间无效", L"日程助手", MB_OK | MB_ICONWARNING);
            return;
        }
        int y2, m2, d2; tbreak(dtp_get(s_ed.hEDate), &y2, &m2, &d2, NULL, NULL);
        int hh2, mm2;   tbreak(dtp_get(s_ed.hETime), NULL, NULL, NULL, &hh2, &mm2);
        et = tmk(y2, m2, d2, hh2, mm2);
        if (et == 0) {
            MessageBoxW(dlg, L"结束时间无效", L"日程助手", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    if (et < st) {
        MessageBoxW(dlg, allDay ? L"结束日期不能早于开始日期"
                                : L"结束时间不能早于开始时间",
                    L"日程助手", MB_OK | MB_ICONWARNING);
        return;
    }

    Event ev = s_ed.ev;      /* 保留 id / lastNotified */
    memset(ev.title, 0, sizeof(ev.title));
    wcsncpy(ev.title, tp, 127);
    GetWindowTextW(s_ed.hNote, ev.note, 512);
    ev.allDay = allDay;
    if (allDay) {
        iso_date_from_t64(st, ev.start, sizeof(ev.start));
        iso_date_from_t64(et, ev.end, sizeof(ev.end));
    } else {
        iso_from_t64(st, ev.start, sizeof(ev.start));
        iso_from_t64(et, ev.end, sizeof(ev.end));
    }
    ev.colorIdx = s_ed.color;
    ev.remindMinutes = remind_current_minutes(allDay);
    int recIdx = (int)(INT_PTR)SendMessageW(s_ed.hRec, CB_GETCURSEL, 0, 0);
    ev.rec = (recIdx >= 0 && recIdx <= REC_CUSTOM) ? (RecKind)recIdx : REC_NONE;
    if (ev.rec == REC_CUSTOM) {
        wchar_t dur[16]; GetWindowTextW(s_ed.hRInt, dur, 16);
        ev.interval = _wtoi(dur);
        if (ev.interval < 1) ev.interval = 1;
        int u = (int)(INT_PTR)SendMessageW(s_ed.hRUnit, CB_GETCURSEL, 0, 0);
        ev.unit = (u >= U_MIN && u <= U_YEAR) ? (TimeUnit)u : U_DAY;
        if (SendMessageW(s_ed.hRUntil, CB_GETCURSEL, 0, 0) == 1) {
            int uy, um, ud; tbreak(dtp_get(s_ed.hRUntilDtp), &uy, &um, &ud, NULL, NULL);
            iso_from_t64(tmk(uy, um, ud, 23, 59), ev.until, sizeof(ev.until));
        } else {
            ev.until[0] = 0;
        }
    } else {
        ev.interval = 1;
        ev.unit = U_DAY;
        ev.until[0] = 0;
    }

    if (s_ed.isNew) schedule_add(&ev);
    else            schedule_update(&ev);
    widget_refresh();
    DestroyWindow(dlg);
}

static void ed_delete(HWND dlg)
{
    if (MessageBoxW(dlg, L"确定删除该日程吗？", L"日程助手",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    schedule_delete(s_ed.ev.id);
    widget_refresh();
    DestroyWindow(dlg);
}

/* ---- 子类过程 ---- */
static LRESULT CALLBACK ed_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDE_SAVE:
            if (HIWORD(wp) == BN_CLICKED) ed_save(dlg);
            return 0;
        case IDE_DELETE:
            if (HIWORD(wp) == BN_CLICKED) ed_delete(dlg);
            return 0;
        case IDE_EMODE:
            if (HIWORD(wp) == CBN_SELCHANGE) { ed_relayout(dlg); }
            return 0;
        case IDE_ALLDAY: {
            /* BS_OWNERDRAW 复选框由 dlg_base_proc 翻转 PROP，
             * 必须先放行基类处理，再读取新状态重排 */
            LRESULT r = DefSubclassProc(dlg, msg, wp, lp);
            if (HIWORD(wp) == BN_CLICKED) {
                int ad = dlg_check_get(s_ed.hAllDay);
                int prevMin = remind_current_minutes(!ad);  /* 翻转前的下拉值 */
                SendMessageW(s_ed.hEMode, CB_SETCURSEL, 0, 0);
                ed_relayout(dlg);
                remind_rebuild(ad, prevMin);
            }
            return r;
        }
        case IDE_REC:
            if (HIWORD(wp) == CBN_SELCHANGE) { ed_relayout(dlg); }
            return 0;
        case IDE_RUNTIL:
            if (HIWORD(wp) == CBN_SELCHANGE)
                EnableWindow(s_ed.hRUntilDtp,
                             SendMessageW(s_ed.hRUntil, CB_GETCURSEL, 0, 0) == 1);
            return 0;
        }
        break;
    case WM_CTLCOLORDLG:
        return (LRESULT)s_dlgBrush;
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB((COL_TXT >> 16) & 0xFF,
                              (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        SetBkColor(hdc, RGB((COL_INPUT >> 16) & 0xFF,
                            (COL_INPUT >> 8) & 0xFF, COL_INPUT & 0xFF));
        return (LRESULT)s_editBg;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB((COL_TXT >> 16) & 0xFF,
                                  (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        return (LRESULT)s_dlgBrush;
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB((COL_TXT >> 16) & 0xFF,
                                  (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        return (LRESULT)s_dlgBrush;
    case WM_ACTIVATE: {
        LRESULT r = DefSubclassProc(dlg, msg, wp, lp);
        /* 24H2 实测：对话框显示前对 owner-draw combo 做 CB_SETCURSEL 等
         * 操作会把顶层标题栏的深色状态冲掉（同步 attr20 重应用无效），
         * 在窗口真正激活后再应用一次可恢复深色标题栏 */
        if (LOWORD(wp) != WA_INACTIVE)
            theme_apply_dark_frame(dlg);
        return r;
    }
    case WM_TIMER:
        if (wp == 0xD1A) {
            KillTimer(dlg, 0xD1A);
            theme_apply_dark_frame(dlg);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        KillTimer(dlg, 0xD1A);
        if (s_dlgBrush) { DeleteObject(s_dlgBrush); s_dlgBrush = NULL; }
        if (s_editBg)  { DeleteObject(s_editBg);  s_editBg  = NULL; }
        RemoveWindowSubclass(dlg, ed_proc, 1);
        break;
    }
    return DefSubclassProc(dlg, msg, wp, lp);
}

/* ---- 创建 ---- */
int editor_open(HWND owner, const Event *ev)
{
    memset(&s_ed, 0, sizeof(s_ed));
    s_ed.isNew = ev ? 0 : 1;

    if (ev) {
        s_ed.ev = *ev;
    } else {
        gen_id(s_ed.ev.id, sizeof(s_ed.ev.id));
        t64 now = tnow();
        t64 st = (now / 300) * 300 + 300;   /* 下一个 5 分钟 */
        iso_from_t64(st, s_ed.ev.start, sizeof(s_ed.ev.start));
        iso_from_t64(tadd_unit(st, 1, U_HOUR), s_ed.ev.end, sizeof(s_ed.ev.end));
        s_ed.ev.colorIdx = 0;
        s_ed.ev.remindMinutes = g.st.defRemind;
        s_ed.ev.rec = REC_NONE;
        s_ed.ev.interval = 1;
        s_ed.ev.unit = U_DAY;
    }
    s_ed.color = s_ed.ev.colorIdx % PAL_COUNT;

    swatch_register();
    picker_register();
    HWND dlg = dlg_base_create(owner, s_ed.isNew ? L"新建日程" : L"编辑日程", 384, 380);
    if (!dlg) return 0;
    SetWindowSubclass(dlg, ed_proc, 1, 0);
    theme_reload(0);
    theme_apply_dark_frame(dlg);
    if (s_dlgBrush) DeleteObject(s_dlgBrush);
    if (s_editBg)  DeleteObject(s_editBg);
    s_dlgBrush = CreateSolidBrush(RGB((COL_CARD >> 16) & 0xFF,
                                      (COL_CARD >> 8) & 0xFF, COL_CARD & 0xFF));
    s_editBg  = CreateSolidBrush(RGB((COL_INPUT >> 16) & 0xFF,
                                     (COL_INPUT >> 8) & 0xFF, COL_INPUT & 0xFF));

    s_ed.hLb[0] = dlg_ctrl(dlg, L"STATIC", L"标题", 0, 0, 0, 16, 21, 48, 20);
    s_ed.hTitle = dlg_ctrl(dlg, L"EDIT", s_ed.ev.title, WS_TABSTOP | ES_AUTOHSCROLL,
                           WS_EX_CLIENTEDGE, IDE_TITLE, 0, 0, 0, 0);
    SendMessageW(s_ed.hTitle, EM_SETLIMITTEXT, 127, 0);

    s_ed.hLb[1] = dlg_ctrl(dlg, L"STATIC", L"开始", 0, 0, 0, 16, 59, 48, 20);
    s_ed.hSDate = dlg_ctrl(dlg, PICK_CLASS, L"date", WS_TABSTOP, 0,
                           IDE_SDATE, 0, 0, 0, 0);
    s_ed.hSTime = dlg_ctrl(dlg, PICK_CLASS, L"time", WS_TABSTOP, 0,
                           IDE_STIME, 0, 0, 0, 0);
    s_ed.hAllDay = dlg_ctrl(dlg, L"BUTTON", L"全天",
                            WS_TABSTOP | BS_AUTOCHECKBOX, 0,
                            IDE_ALLDAY, 0, 0, 0, 0);

    s_ed.hLb[2] = dlg_ctrl(dlg, L"STATIC", L"结束", 0, 0, 0, 16, 97, 48, 20);
    s_ed.hEMode = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                           0, IDE_EMODE, 0, 0, 0, 0);
    ComboBox_AddString(s_ed.hEMode, L"指定结束时间");
    ComboBox_AddString(s_ed.hEMode, L"持续时长");
    s_ed.hEDate = dlg_ctrl(dlg, PICK_CLASS, L"date", WS_TABSTOP, 0,
                           IDE_EDATE, 0, 0, 0, 0);
    s_ed.hETime = dlg_ctrl(dlg, PICK_CLASS, L"time", WS_TABSTOP, 0,
                           IDE_ETIME, 0, 0, 0, 0);
    s_ed.hEDur = dlg_ctrl(dlg, L"EDIT", L"1", WS_TABSTOP | ES_NUMBER,
                          WS_EX_CLIENTEDGE, IDE_EDUR, 0, 0, 0, 0);
    SendMessageW(s_ed.hEDur, EM_SETLIMITTEXT, 9, 0);
    s_ed.hEUnit = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                           0, IDE_EUNIT, 0, 0, 0, 0);
    for (int i = 0; i < 6; i++) ComboBox_AddString(s_ed.hEUnit, UNIT_T[i]);
    SendMessageW(s_ed.hEUnit, CB_SETCURSEL, U_HOUR, 0);

    s_ed.hLb[3] = dlg_ctrl(dlg, L"STATIC", L"循环", 0, 0, 0, 16, 135, 48, 20);
    s_ed.hRec = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                         0, IDE_REC, 0, 0, 0, 0);
    ComboBox_AddString(s_ed.hRec, L"不重复");
    ComboBox_AddString(s_ed.hRec, L"每天");
    ComboBox_AddString(s_ed.hRec, L"每周");
    ComboBox_AddString(s_ed.hRec, L"每月");
    ComboBox_AddString(s_ed.hRec, L"每年");
    ComboBox_AddString(s_ed.hRec, L"自定义");
    s_ed.hRInt = dlg_ctrl(dlg, L"EDIT", L"1", WS_TABSTOP | ES_NUMBER,
                          WS_EX_CLIENTEDGE, IDE_RINT, 0, 0, 0, 0);
    s_ed.hRUnit = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                           0, IDE_RUNIT, 0, 0, 0, 0);
    for (int i = 0; i < 6; i++) ComboBox_AddString(s_ed.hRUnit, UNIT_T[i]);
    s_ed.hRUntil = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                            0, IDE_RUNTIL, 0, 0, 0, 0);
    ComboBox_AddString(s_ed.hRUntil, L"永不停止");
    ComboBox_AddString(s_ed.hRUntil, L"指定截止日期");
    s_ed.hRUntilDtp = dlg_ctrl(dlg, PICK_CLASS, L"date", WS_TABSTOP, 0,
                               IDE_RUNTILDATE, 0, 0, 0, 0);

    s_ed.hLb[4] = dlg_ctrl(dlg, L"STATIC", L"提醒", 0, 0, 0, 16, 173, 48, 20);
    s_ed.hRemind = dlg_ctrl(dlg, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                            0, IDE_REMIND, 0, 0, 0, 0);
    for (int i = 0; i < 7; i++) ComboBox_AddString(s_ed.hRemind, REMIND_T[i]);

    s_ed.hLb[5] = dlg_ctrl(dlg, L"STATIC", L"颜色", 0, 0, 0, 16, 207, 48, 20);
    for (int i = 0; i < 8; i++) {
        s_ed.hSw[i] = CreateWindowExW(0, WC_SWATCH, L"", WS_CHILD | WS_VISIBLE,
            0, 0, 1, 1, dlg, (HMENU)(INT_PTR)(IDE_COLOR + i), g.hInst, NULL);
        SetWindowLongPtrW(s_ed.hSw[i], GWLP_USERDATA, (LONG_PTR)i);
    }

    s_ed.hLb[6] = dlg_ctrl(dlg, L"STATIC", L"备注", 0, 0, 0, 16, 241, 48, 20);
    s_ed.hNote = dlg_ctrl(dlg, L"EDIT", L"", WS_TABSTOP |
                          ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                          WS_EX_CLIENTEDGE, IDE_NOTE, 0, 0, 0, 0);

    if (!s_ed.isNew)
        s_ed.hDel = dlg_ctrl(dlg, L"BUTTON", L"删除", WS_TABSTOP | BS_PUSHBUTTON,
                             0, IDE_DELETE, 0, 0, 0, 0);
    s_ed.hSave = dlg_ctrl(dlg, L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON,
                          0, IDE_SAVE, 0, 0, 0, 0);

    ed_load(dlg);
    ed_relayout(dlg);
    SetTimer(dlg, 0xD1A, 150, NULL);
    dlg_run(dlg, owner);
    return 1;
}
