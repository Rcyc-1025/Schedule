/* picker.c — 自绘日期/时间选择器（替代 SysDateTimePick32）
 * 见 picker.h 顶部的背景说明。纯 GDI 绘制，配色取 COL_* 全局变量。 */
#include "app.h"
#include "theme.h"
#include "picker.h"
#include <windowsx.h>
#include <stdlib.h>
#include <wchar.h>

/* ---------------- 小工具 ---------------- */

static COLORREF colref(DWORD c)
{
    return RGB((int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF), (int)(c & 0xFF));
}

static void fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void frame(HDC dc, const RECT *r, COLORREF border, COLORREF bg, int rad)
{
    HBRUSH bf = CreateSolidBrush(bg);
    HPEN pn = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, bf);
    HGDIOBJ op = SelectObject(dc, pn);
    RoundRect(dc, r->left, r->top, r->right + 1, r->bottom + 1, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(bf);
    DeleteObject(pn);
}

static void text(HDC dc, const wchar_t *s, const RECT *r, COLORREF c, UINT fmt)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, (RECT *)r, fmt | DT_NOPREFIX);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* 日期<->连续日序号（Howard Hinnant 民用日历，1970-01-01=0） */
static long days_epoch(int y, int m, int d)
{
    int yy = y - (m <= 2);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    int yoe = yy - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097L + (long)doe - 719468L;
}

static void civil_days(long z, int *y, int *m, int *d)
{
    z += 719468;
    int era = (z >= 0 ? (int)z : (int)z - 146096) / 146097;
    int doe = (int)(z - (long)era * 146097L);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

static int monday0(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = y; if (m < 3) yy--;
    int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7; /* 0=周日 */
    return (w + 6) % 7;                                                 /* 0=周一 */
}

static int dim_month(int y, int m)
{
    static const int dm[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

/* ---------------- 收起态主控件 W_DPK ---------------- */

typedef struct { int mode; SYSTEMTIME st; } DpkSt;   /* mode: 0 日期 1 时间 */

static void draw_triangle(HDC dc, const RECT *rc, COLORREF c)
{
    int h = rc->bottom - rc->top;
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int s = h / 5;
    POINT pts[3] = {
        { cx - s,     cy - s / 2 },
        { cx + s,     cy - s / 2 },
        { cx,         cy + s }
    };
    HBRUSH b = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, b);
    HPEN pn = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, pn);
    Polygon(dc, pts, 3);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(pn);
}

static const wchar_t *WC_CAL = L"W_PKCAL";
static const wchar_t *WC_TIM = L"W_PKTIME";
static HWND s_pendingOwner;
static int  s_pendingKind;

/* 前向声明 */
static LRESULT CALLBACK pop_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

static void dpk_paint(HWND h, DpkSt *s)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    int dpi = GetDpiForWindow(h);
    int rad = MulDiv(4, dpi, 96);
    int pad = MulDiv(10, dpi, 96);
    int en = IsWindowEnabled(h);
    int focused = GetFocus() == h;

    frame(dc, &rc, focused ? colref(COL_ACCENT) : colref(COL_LINE),
          colref(COL_INPUT), rad);

    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ of = SelectObject(dc, f);

    wchar_t buf[32];
    if (s->mode == 0)
        swprintf(buf, 32, L"%04d-%02d-%02d", s->st.wYear, s->st.wMonth, s->st.wDay);
    else
        swprintf(buf, 32, L"%02d:%02d", s->st.wHour, s->st.wMinute);

    int btnW = rc.bottom - rc.top;
    RECT tr = { rc.left + pad, rc.top, rc.right - btnW, rc.bottom };
    text(dc, buf, &tr, en ? colref(COL_TXT) : colref(COL_SUB),
         DT_VCENTER | DT_SINGLELINE | DT_LEFT);

    RECT ar = { rc.right - btnW, rc.top, rc.right, rc.bottom };
    draw_triangle(dc, &ar, colref(en ? COL_SUB : COL_LINE));

    if (focused) {
        RECT fr = { rc.left + pad/2, rc.top + 3,
                    rc.right - btnW - pad/2, rc.bottom - 3 };
        DrawFocusRect(dc, &fr);
    }
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

/* ---------------- 弹出浮窗 ---------------- */

enum { POP_CAL = 1, POP_TIME = 2 };
#define CAL_W 226
#define CAL_H 256
#define TIM_W 148
#define TIM_H 172

typedef struct {
    int kind;
    HWND owner;
    int dpi;
    HFONT font;
    SYSTEMTIME st;
    int vY, vM;          /* 月历视图年月 */
    int hovCell;         /* 月历悬停格 0..41，-1 */
    int hovBtn;          /* 1 左翻 2 右翻 3 今天 */
    int oH, oM;          /* 时间列滚动偏移（首行索引，可为负） */
    int col;             /* 时间键盘活动列 */
    int hovH, hovM;      /* 时间悬停项 */
} PopSt;

static void pop_commit(HWND h, PopSt *p)
{
    SendMessageW(p->owner, PKM_SET, 0, (LPARAM)&p->st);
    InvalidateRect(p->owner, NULL, FALSE);
    (void)h;
}

static int px(PopSt *p, int v) { return MulDiv(v, p->dpi, 96); }

/* ---- 月历绘制 ---- */

static void cal_paint(HWND h, PopSt *p)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    fill(dc, &rc, colref(COL_CARD));

    HGDIOBJ of = SelectObject(dc, p->font);

    int pad = px(p, 8);
    int headH = px(p, 28), weekH = px(p, 20), cellH = px(p, 26);
    int cellW = (rc.right - pad * 2) / 7;
    int gridY = px(p, 62);
    int btnW = px(p, 28);

    /* 头部翻页钮 */
    RECT lb = { pad, pad, pad + btnW, pad + headH };
    RECT rb = { rc.right - pad - btnW, pad, rc.right - pad, pad + headH };
    frame(dc, &lb, colref(COL_LINE),
          p->hovBtn == 1 ? colref(COL_INPUT) : colref(COL_CARD), px(p, 6));
    frame(dc, &rb, colref(COL_LINE),
          p->hovBtn == 2 ? colref(COL_INPUT) : colref(COL_CARD), px(p, 6));
    COLORREF navc = colref(COL_SUB);
    int lcx = (lb.left + lb.right) / 2, lcy = (lb.top + lb.bottom) / 2;
    int s7 = px(p, 5);
    POINT lt[3] = { { lcx + s7/2, lcy - s7 }, { lcx + s7/2, lcy + s7 }, { lcx - s7/2, lcy } };
    int rcxv = (rb.left + rb.right) / 2, rcyv = (rb.top + rb.bottom) / 2;
    POINT rt[3] = { { rcxv - s7/2, rcyv - s7 }, { rcxv - s7/2, rcyv + s7 }, { rcxv + s7/2, rcyv } };
    HBRUSH bb = CreateSolidBrush(navc);
    HPEN pp = CreatePen(PS_SOLID, 1, navc);
    HGDIOBJ o1 = SelectObject(dc, bb), o2 = SelectObject(dc, pp);
    Polygon(dc, lt, 3); Polygon(dc, rt, 3);
    SelectObject(dc, o1); SelectObject(dc, o2);
    DeleteObject(bb); DeleteObject(pp);

    wchar_t title[32];
    swprintf(title, 32, L"%d 年 %d 月", p->vY, p->vM);
    RECT tr = { pad, pad, rc.right - pad, pad + headH };
    text(dc, title, &tr, colref(COL_TXT), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 周标题（周一起） */
    static const wchar_t *wk[7] = { L"一",L"二",L"三",L"四",L"五",L"六",L"日" };
    int wy = px(p, 40);
    for (int i = 0; i < 7; i++) {
        RECT c = { pad + i * cellW, wy, pad + (i + 1) * cellW, wy + weekH };
        text(dc, wk[i], &c, colref(COL_SUB), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 42 格日期 */
    SYSTEMTIME now; GetLocalTime(&now);
    long base = days_epoch(p->vY, p->vM, 1) - monday0(p->vY, p->vM, 1);
    for (int k = 0; k < 42; k++) {
        int y, m, d;
        civil_days(base + k, &y, &m, &d);
        int col = k % 7, row = k / 7;
        RECT c = { pad + col * cellW, gridY + row * cellH,
                   pad + (col + 1) * cellW, gridY + (row + 1) * cellH };
        int inMonth = (m == p->vM);
        int isToday = (y == now.wYear && m == now.wMonth && d == now.wDay);
        int isSel = (y == p->st.wYear && m == p->st.wMonth && d == p->st.wDay);
        if (isSel) {
            RECT rb2 = { c.left + 2, c.top + 2, c.right - 2, c.bottom - 2 };
            frame(dc, &rb2, colref(COL_ACCENT), colref(COL_ACCENT), px(p, 6));
        } else if (k == p->hovCell && inMonth) {
            RECT rb2 = { c.left + 2, c.top + 2, c.right - 2, c.bottom - 2 };
            frame(dc, &rb2, colref(COL_LINE), colref(COL_INPUT), px(p, 6));
        }
        COLORREF fc = isSel ? RGB(255,255,255)
                    : isToday ? colref(COL_ACCENT)
                    : inMonth ? colref(COL_TXT) : colref(COL_SUB);
        wchar_t num[8]; swprintf(num, 8, L"%d", d);
        text(dc, num, &c, fc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 今天按钮 */
    RECT tb = { pad, rc.bottom - pad - px(p, 26),
                rc.right - pad, rc.bottom - pad };
    frame(dc, &tb, colref(COL_LINE),
          p->hovBtn == 3 ? colref(COL_INPUT) : colref(COL_CARD), px(p, 6));
    text(dc, L"今天", &tb, colref(COL_ACCENT), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 外边框 */
    HPEN bp = CreatePen(PS_SOLID, 1, colref(COL_LINE));
    HGDIOBJ opn = SelectObject(dc, bp);
    HGDIOBJ obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, rc.right, rc.bottom);
    SelectObject(dc, opn); SelectObject(dc, obr);
    DeleteObject(bp);

    SelectObject(dc, of);
    EndPaint(h, &ps);
}

/* 月历命中：返回 0 空白 1 左翻 2 右翻 3 今天 4 日期格（*out=索引） */
static int cal_hit(PopSt *p, int x, int y, int *out)
{
    int W = px(p, CAL_W), H = px(p, CAL_H);
    int pad = px(p, 8), btnW = px(p, 28), headH = px(p, 28);
    if (y >= pad && y <= pad + headH) {
        if (x >= pad && x <= pad + btnW) return 1;
        if (x >= W - pad - btnW && x <= W - pad) return 2;
    }
    int cellW = (W - pad * 2) / 7, cellH = px(p, 26), gridY = px(p, 62);
    if (y >= gridY && y < gridY + cellH * 6 && x >= pad && x < W - pad) {
        int c = (x - pad) / cellW, r = (y - gridY) / cellH;
        if (c >= 0 && c < 7 && r >= 0 && r < 6) { *out = r * 7 + c; return 4; }
    }
    int tbY = H - pad - px(p, 26);
    if (y >= tbY && y <= H - pad && x >= pad && x <= W - pad) return 3;
    return 0;
}

static void cal_select(PopSt *p, int k)
{
    long base = days_epoch(p->vY, p->vM, 1) - monday0(p->vY, p->vM, 1);
    int y, m, d;
    civil_days(base + k, &y, &m, &d);
    p->st.wYear = (WORD)y; p->st.wMonth = (WORD)m; p->st.wDay = (WORD)d;
    p->vY = y; p->vM = m;
}

static void cal_shift_month(PopSt *p, int delta)
{
    int m = p->vM + delta;
    int y = p->vY;
    while (m < 1) { m += 12; y--; }
    while (m > 12) { m -= 12; y++; }
    p->vY = y; p->vM = m;
}

/* ---- 时间浮窗绘制 ---- */

static void time_paint(HWND h, PopSt *p)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    fill(dc, &rc, colref(COL_CARD));
    HGDIOBJ of = SelectObject(dc, p->font);

    int pad = px(p, 8), rowH = px(p, 26), rows = 6;
    int colW = (rc.right - pad * 2) / 2;
    int top = pad;
    int vals[2] = { p->st.wHour, p->st.wMinute };
    int offs[2] = { p->oH, p->oM };
    int hovs[2] = { p->hovH, p->hovM };
    int maxv[2] = { 23, 59 };

    for (int c = 0; c < 2; c++) {
        int x0 = pad + c * colW;
        for (int k = 0; k < rows; k++) {
            int idx = offs[c] + k;
            if (idx < 0 || idx > maxv[c]) continue;
            RECT r = { x0 + 2, top + k * rowH + 2,
                       x0 + colW - 2, top + (k + 1) * rowH - 2 };
            int sel = (idx == vals[c]);
            int hov = (idx == hovs[c]);
            if (sel)
                frame(dc, &r, colref(COL_ACCENT), colref(COL_ACCENT), px(p, 6));
            else if (hov)
                frame(dc, &r, colref(COL_LINE), colref(COL_INPUT), px(p, 6));
            wchar_t num[8]; swprintf(num, 8, L"%02d", idx);
            text(dc, num, &r, sel ? RGB(255,255,255) : colref(COL_TXT),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    /* 中间冒号 */
    RECT cr = { rc.right / 2 - px(p, 8), top, rc.right / 2 + px(p, 8),
                top + rowH * rows };
    text(dc, L":", &cr, colref(COL_SUB), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HPEN bp = CreatePen(PS_SOLID, 1, colref(COL_LINE));
    HGDIOBJ opn = SelectObject(dc, bp);
    HGDIOBJ obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, rc.right, rc.bottom);
    SelectObject(dc, opn); SelectObject(dc, obr);
    DeleteObject(bp);

    SelectObject(dc, of);
    EndPaint(h, &ps);
}

/* 时间列命中：列 0/1，行；返回 0 无 1 有效项 */
static int time_hit(PopSt *p, int x, int y, int *c, int *idx)
{
    int W = px(p, TIM_W);
    int pad = px(p, 8), rowH = px(p, 26), rows = 6;
    int colW = (W - pad * 2) / 2;
    if (x < pad || x >= W - pad || y < pad || y >= pad + rowH * rows) return 0;
    *c = (x - pad) / colW;
    if (*c > 1) *c = 1;
    int k = (y - pad) / rowH;
    if (k < 0 || k >= rows) return 0;
    *idx = (*c == 0 ? p->oH : p->oM) + k;
    if (*idx < 0 || *idx > (*c == 0 ? 23 : 59)) return 0;
    return 1;
}

/* ---- 弹窗过程 ---- */

static LRESULT CALLBACK pop_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PopSt *p = (PopSt *)GetWindowLongPtrW(h, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        p = (PopSt *)calloc(1, sizeof(PopSt));
        p->kind = s_pendingKind;
        p->owner = s_pendingOwner;
        p->dpi = GetDpiForWindow(h);
        p->font = (HFONT)SendMessageW(p->owner, WM_GETFONT, 0, 0);
        SendMessageW(p->owner, PKM_GET, 0, (LPARAM)&p->st);
        p->vY = p->st.wYear; p->vM = p->st.wMonth;
        p->hovCell = -1; p->hovH = -1; p->hovM = -1;
        p->oH = clampi(p->st.wHour - 2, -2, 20);
        p->oM = clampi(p->st.wMinute - 2, -2, 56);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(h);
        return 0;
    case WM_PAINT:
        if (!p) break;
        if (p->kind == POP_CAL) cal_paint(h, p); else time_paint(h, p);
        return 0;
    case WM_LBUTTONDOWN: {
        if (!p) break;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (p->kind == POP_CAL) {
            int k = -1, hit = cal_hit(p, x, y, &k);
            if (hit == 1)      { cal_shift_month(p, -1); InvalidateRect(h, NULL, FALSE); }
            else if (hit == 2) { cal_shift_month(p, 1);  InvalidateRect(h, NULL, FALSE); }
            else if (hit == 3) {
                GetLocalTime(&p->st);
                p->vY = p->st.wYear; p->vM = p->st.wMonth;
                pop_commit(h, p); DestroyWindow(h);
            } else if (hit == 4) {
                cal_select(p, k);
                pop_commit(h, p); DestroyWindow(h);
            }
        } else {
            int c, idx;
            if (time_hit(p, x, y, &c, &idx)) {
                if (c == 0) {
                    p->st.wHour = (WORD)idx;
                    p->oH = clampi(idx - 2, -2, 20);
                    pop_commit(h, p);
                    InvalidateRect(h, NULL, FALSE);
                } else {
                    p->st.wMinute = (WORD)idx;
                    p->oM = clampi(idx - 2, -2, 56);
                    pop_commit(h, p);
                    DestroyWindow(h);
                }
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!p) break;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (p->kind == POP_CAL) {
            int k = -1, hit = cal_hit(p, x, y, &k);
            int nb = (hit == 1 || hit == 2 || hit == 3) ? hit : 0;
            int nc = (hit == 4) ? k : -1;
            if (nb != p->hovBtn || nc != p->hovCell) {
                p->hovBtn = nb; p->hovCell = nc;
                InvalidateRect(h, NULL, FALSE);
            }
        } else {
            int c, idx;
            if (time_hit(p, x, y, &c, &idx)) {
                int nh = c == 0 ? idx : p->hovH;
                int nm = c == 1 ? idx : p->hovM;
                if (nh != p->hovH || nm != p->hovM) {
                    p->hovH = nh; p->hovM = nm;
                    InvalidateRect(h, NULL, FALSE);
                }
            } else if (p->hovH != -1 || p->hovM != -1) {
                p->hovH = -1; p->hovM = -1;
                InvalidateRect(h, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!p) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (p->kind == POP_CAL) {
            cal_shift_month(p, delta > 0 ? -1 : 1);
        } else {
            POINT pt;
            pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);   /* 屏幕坐标 */
            ScreenToClient(h, &pt);
            int W = px(p, TIM_W), pad = px(p, 8), colW = (W - pad*2)/2;
            int step = delta > 0 ? -1 : 1;
            if (pt.x < pad + colW) p->oH = clampi(p->oH + step, -2, 20);
            else                   p->oM = clampi(p->oM + step, -2, 56);
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        if (!p) break;
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (p->kind == POP_CAL) {
            int changed = 1;
            long e = days_epoch(p->st.wYear, p->st.wMonth, p->st.wDay);
            if (wp == VK_LEFT)  e -= 1;
            else if (wp == VK_RIGHT) e += 1;
            else if (wp == VK_UP) e -= 7;
            else if (wp == VK_DOWN) e += 7;
            else if (wp == VK_PRIOR) { cal_shift_month(p, -1); changed = 0; }
            else if (wp == VK_NEXT)  { cal_shift_month(p, 1);  changed = 0; }
            else if (wp == VK_RETURN) { pop_commit(h, p); DestroyWindow(h); return 0; }
            else changed = 0;
            if (changed) {
                int y, m, d; civil_days(e, &y, &m, &d);
                p->st.wYear=(WORD)y; p->st.wMonth=(WORD)m; p->st.wDay=(WORD)d;
                p->vY=y; p->vM=m;
                pop_commit(h, p);
            }
            InvalidateRect(h, NULL, FALSE);
        } else {
            if (wp == VK_LEFT) p->col = 0;
            else if (wp == VK_RIGHT) p->col = 1;
            else if (wp == VK_UP || wp == VK_DOWN) {
                int step = wp == VK_UP ? -1 : 1;
                if (p->col == 0) {
                    int v = (p->st.wHour + step + 24) % 24;
                    p->st.wHour = (WORD)v;
                    p->oH = clampi(v - 2, -2, 20);
                } else {
                    int v = (p->st.wMinute + step + 60) % 60;
                    p->st.wMinute = (WORD)v;
                    p->oM = clampi(v - 2, -2, 56);
                }
                pop_commit(h, p);
            } else if (wp == VK_RETURN) { DestroyWindow(h); return 0; }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_NCDESTROY:
        if (p) free(p);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* 弹出 + 本地消息循环（仿 TrackPopupMenu：点外部关闭并吞掉当次点击） */
static void pop_run(HWND owner, int kind)
{
    s_pendingOwner = owner;
    s_pendingKind = kind;
    int dpi = GetDpiForWindow(owner);
    int W = MulDiv(kind == POP_CAL ? CAL_W : TIM_W, dpi, 96);
    int H = MulDiv(kind == POP_CAL ? CAL_H : TIM_H, dpi, 96);

    HWND root = GetAncestor(owner, GA_ROOT);
    RECT wr; GetWindowRect(owner, &wr);
    int x = wr.left, y = wr.bottom + 2;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (y + H > work.bottom) y = wr.top - H - 2;
    if (y < work.top) y = work.top;
    if (x + W > work.right) x = work.right - W;
    if (x < work.left) x = work.left;

    HWND pop = CreateWindowExW(WS_EX_TOOLWINDOW,
        kind == POP_CAL ? WC_CAL : WC_TIM, L"",
        WS_POPUP, x, y, W, H, root, NULL, g.hInst, NULL);
    if (!pop) return;
    ShowWindow(pop, SW_SHOW);
    SetFocus(pop);

    MSG m;
    while (IsWindow(pop)) {
        if (GetMessageW(&m, NULL, 0, 0) <= 0) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
        /* 仅按键消息触发外部关闭；浮窗弹出瞬间系统会合成一条 WM_MOUSEMOVE
         * 给光标下的旧窗口，不能把它误判为外部点击 */
        if (m.message == WM_LBUTTONDOWN || m.message == WM_LBUTTONUP ||
            m.message == WM_RBUTTONDOWN || m.message == WM_RBUTTONUP ||
            m.message == WM_MBUTTONDOWN || m.message == WM_MBUTTONUP) {
            if (m.hwnd != pop && !IsChild(pop, m.hwnd)) {
                DestroyWindow(pop);
                continue;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (IsWindow(owner)) SetFocus(owner);
}

/* ---- 主控件过程 ---- */

static LRESULT CALLBACK dpk_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    DpkSt *s = (DpkSt *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        s = (DpkSt *)calloc(1, sizeof(DpkSt));
        s->mode = (cs->lpszName && cs->lpszName[0] == L't') ? 1 : 0;
        GetLocalTime(&s->st);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (s) dpk_paint(h, s);
        return 0;
    case WM_ENABLE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (s && IsWindowEnabled(h))
            pop_run(h, s->mode == 0 ? POP_CAL : POP_TIME);
        return 0;
    case WM_KEYDOWN:
        if (s && IsWindowEnabled(h) &&
            (wp == VK_SPACE || wp == VK_DOWN || wp == VK_RETURN)) {
            pop_run(h, s->mode == 0 ? POP_CAL : POP_TIME);
            return 0;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case PKM_SET:
        if (s && lp) { memcpy(&s->st, (const void *)lp, sizeof(SYSTEMTIME));
                       InvalidateRect(h, NULL, FALSE); }
        return 0;
    case PKM_GET:
        if (s && lp) { memcpy((void *)lp, &s->st, sizeof(SYSTEMTIME)); return TRUE; }
        return FALSE;
    case WM_NCDESTROY:
        if (s) free(s);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void picker_register(void)
{
    static int reg = 0;
    if (reg) return;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.hInstance = g.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpfnWndProc = dpk_proc;
    wc.hbrBackground = NULL;
    wc.lpszClassName = PICK_CLASS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = pop_proc;
    wc.lpszClassName = WC_CAL;
    RegisterClassW(&wc);
    wc.lpszClassName = WC_TIM;
    RegisterClassW(&wc);
    reg = 1;
}
