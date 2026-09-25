/* widget.c — 主窗口：无边框桌面层组件（钉在桌面之上、普通窗口之下）、
 *            月历/议程、收起 pill、托盘转发。日程文件导入见 ai_window.c */
#include "app.h"
#include "widget.h"
#include "render.h"
#include "editor.h"
#include "settings.h"
#include "ai_window.h"
#include "notify.h"
#include "fullscreen.h"
#include "storage.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PT_X(l) ((int)(short)LOWORD(l))
#define PT_Y(l) ((int)(short)HIWORD(l))

/* ---- 基准尺寸（96 DPI，绘制时乘 scale）---- */
#define WIN_W    340
#define PILL_W   232
#define PILL_H   56
#define PILL_HOLD_MS  4000   /* 每条日程在 pill 停留时长 */
#define PILL_ANIM_MS  450    /* 上下滚动动画时长 */
#define HEAD_H   46
#define BTN_SZ   30
#define CAL_TH   30
#define CAL_WH   22
#define CAL_CELL 28
#define AG_HEAD  24
#define CHIP_H   42
#define CHIP_GAP 5

/* 窗口高度 = 头部 + 月历块（月份行/星期行/6 行日期）+ 议程区（组头 +
 * 约 3 个 chip + 半个 chip 的滚动余量，提示可滚动）+ 底部留白 */
#define AG_VIEW  (AG_HEAD + 3 * (CHIP_H + CHIP_GAP) + CHIP_H / 2)
#define WIN_H    (HEAD_H + CAL_TH + CAL_WH + 6 * CAL_CELL + AG_VIEW + 12)

static const wchar_t *WC_MAIN = L"SW_MAIN_WIN";

/* ---- 命中区 ---- */
typedef enum {
    HZ_NONE = 0, HZ_NEW, HZ_MENU, HZ_COLLAPSE,
    HZ_CAL_PREV, HZ_CAL_NEXT, HZ_CAL_TODAY, HZ_CAL_DAY, HZ_CHIP
} HitKind;

typedef struct { HitKind kind; int idx; float x, y, w, h; } Hit;

#define MAX_CHIP 64
#define MAX_HIT  320

typedef struct {
    int  collapsed;
    int  calY, calM;          /* 月历显示年月 */
    t64  calSel;              /* 选中日 00:00 */
    int  agendaScroll;
    Hit  hits[MAX_HIT]; int hitCount;
    int  hover;
    int  chipEv[MAX_CHIP]; int chipCount;
    /* 收起态 pill 多日程自动轮播 */
    int   pillN;            /* 当天可轮播日程数 */
    int   pillIdx;          /* 当前停留项索引 */
    float pillSlide;        /* 滚动动画进度 0..1，1=静止 */
    DWORD pillHoldStart;    /* 当前项停留起始时刻（GetTickCount） */
    DWORD pillAnimStart;    /* 本次滚动动画起始时刻 */
    int   pillCurEv;        /* 当前 pill 显示的事件索引（供悬停 tooltip 取备注） */
    HWND  hwndTip;          /* 原生 tooltip 窗口 */
    int   tipActive;        /* tooltip 当前是否在显示（避免 MOUSEMOVE 反复激活闪烁） */
    wchar_t tipLastNote[512]; /* 上一次 tooltip 文本，仅变化时才 TTM_UPDATETIPTEXTW */
} WState;

static WState s;

/* ================= 基础辅助 ================= */

static int base_w(void) { return s.collapsed ? PILL_W : WIN_W; }
static int base_h(void) { return s.collapsed ? PILL_H : WIN_H; }

static float sc_of(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    if (!dpi) dpi = 96;
    return dpi / 96.0f;
}

static void push_hit(HitKind k, int idx, float x, float y, float w, float h)
{
    if (s.hitCount >= MAX_HIT) return;
    Hit *p = &s.hits[s.hitCount++];
    p->kind = k; p->idx = idx; p->x = x; p->y = y; p->w = w; p->h = h;
}

static int hit_at(float px, float py)
{
    for (int i = 0; i < s.hitCount; i++) {
        Hit *p = &s.hits[i];
        if (px >= p->x && px < p->x + p->w && py >= p->y && py < p->y + p->h)
            return i;
    }
    return -1;
}

/* ================= 原生 tooltip（悬停显示完整备注） ================= */

static void tip_show(const wchar_t *text, int screenX, int screenY)
{
    if (!s.hwndTip || !text || !text[0]) return;

    int textChanged = (wcscmp(text, s.tipLastNote) != 0);

    /* 激活/更新后获取实际尺寸，定位到鼠标上方并做屏幕边界夹紧 */
    if (!s.tipActive || textChanged) {
        TOOLINFOW ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        ti.hwnd = g.hwndMain;
        ti.uId = 1;
        ti.lpszText = (LPWSTR)text;
        SendMessageW(s.hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        SendMessageW(s.hwndTip, TTM_TRACKPOSITION, 0, (LPARAM)MAKELPARAM(screenX, screenY));
        if (!s.tipActive)
            SendMessageW(s.hwndTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);

        /* 获取 tooltip 实际宽高 + 屏幕工作区 */
        RECT tr; GetWindowRect(s.hwndTip, &tr);
        int w = tr.right - tr.left;
        int h = tr.bottom - tr.top;
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

        /* X：右边界夹紧，不让 tooltip 超出屏幕右侧 */
        int fx = screenX;
        if (fx + w > wa.right) fx = wa.right - w - 8;
        if (fx < wa.left) fx = wa.left + 8;

        /* Y：默认在鼠标上方，超出顶部时退到鼠标下方 */
        int fy = screenY - 18 - h - 10;
        if (fy < wa.top) fy = screenY + 18 + 10;

        SendMessageW(s.hwndTip, TTM_TRACKPOSITION, 0, (LPARAM)MAKELPARAM(fx, fy));
        wcscpy_s(s.tipLastNote, 512, text);
        s.tipActive = 1;
    }
    /* 同元素同文本：不做任何操作，避免 MOUSEMOVE 频繁重绘 */
}

static void tip_hide(void)
{
    if (!s.tipActive) return;
    TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = g.hwndMain;
    ti.uId = 1;
    SendMessageW(s.hwndTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
    s.tipActive = 0;
    s.tipLastNote[0] = 0;
}

/* 循环标记文本 */
static void rec_label(const Event *ev, wchar_t *out, int cap)
{
    switch (ev->rec) {
    case REC_DAILY:   wcscpy(out, L"每天"); break;
    case REC_WEEKLY:  wcscpy(out, L"每周"); break;
    case REC_MONTHLY: wcscpy(out, L"每月"); break;
    case REC_YEARLY:  wcscpy(out, L"每年"); break;
    case REC_CUSTOM: {
        static const wchar_t *UN[6] = { L"分钟", L"小时", L"天", L"周", L"月", L"年" };
        _snwprintf(out, cap, L"每%d%s", ev->interval > 0 ? ev->interval : 1,
                   UN[ev->unit <= U_YEAR ? ev->unit : U_DAY]);
        break;
    }
    default: out[0] = 0; break;
    }
    out[cap - 1] = 0;
}

/* ================= pill 绘制 ================= */

/* pill 单页内容（一条日程的标题/副标题/色点） */
typedef struct { wchar_t title[160], sub[160]; DWORD dot; } PillItem;

/* 由一次出现记录算出 pill 显示内容 */
static void pill_build(const Occ *occ, t64 now, PillItem *it)
{
    it->dot = PAL_ARGB[0];
    it->title[0] = it->sub[0] = 0;
    Event *ev = schedule_at(occ->evIdx);
    if (!ev) { wcscpy(it->title, L"未命名日程"); return; }
    it->dot = PAL_ARGB[ev->colorIdx % PAL_COUNT];
    wcscpy(it->title, ev->title[0] ? ev->title : L"未命名日程");

    if (ev->allDay &&
        day_start(now) >= day_start(occ->s) &&
        day_start(now) <= day_start(occ->e)) {
        /* 进行中的全天日程：副标题优先显示备注 */
        wcscpy(it->sub, ev->note[0] ? ev->note : L"今天");
    } else if (!ev->allDay && now >= occ->s && now <= occ->e) {
        wchar_t tm[16]; fmt_time(occ->e, tm, 16);
        _snwprintf(it->sub, 160, L"进行中 · 至 %s", tm);
    } else {
        int sy, sm, sd, hh, mm; tbreak(occ->s, &sy, &sm, &sd, &hh, &mm);
        long long d = occ->s - now;
        wchar_t rel[32];
        if (d >= 86400)      _snwprintf(rel, 32, L"%lld天后", (d + 3599) / 86400);
        else if (d >= 3600)  _snwprintf(rel, 32, L"%lld小时后", (d + 59) / 3600);
        else                 _snwprintf(rel, 32, L"%lld分钟后", (d + 59) / 60);
        if (ev->allDay) {
            /* 未来全天日程：有备注优先显示备注，否则显示日期 · 相对时间 */
            if (ev->note[0]) wcscpy(it->sub, ev->note);
            else             _snwprintf(it->sub, 160, L"%02d-%02d · %s", sm, sd, rel);
        } else
            _snwprintf(it->sub, 160, L"%02d-%02d %02d:%02d · %s", sm, sd, hh, mm, rel);
    }
    it->sub[159] = it->title[159] = 0;
}

/* 在纵向偏移 yoff 处绘制一页 pill 内容（yoff 为物理像素，正值下移） */
static void paint_pill_page(GpGraphics *gfx, int cw, int ch, float sc,
                            void *fb, void *fs, const PillItem *it, float yoff)
{
    rd_circle(gfx, 20 * sc, ch / 2.0f + yoff, 4.5f * sc, it->dot, 0, 0);
    rd_text(gfx, it->title, fb, COL_TXT, 34 * sc, 8 * sc + yoff,
            cw - 44.0f * sc, 20 * sc, RD_HLEFT | RD_VCENTER | RD_ELLIPSIS);
    rd_text(gfx, it->sub, fs, COL_SUB, 34 * sc, 29 * sc + yoff,
            cw - 44.0f * sc, 18 * sc, RD_HLEFT | RD_VCENTER | RD_ELLIPSIS);
}

static void paint_pill(GpGraphics *gfx, int cw, int ch, float sc)
{
    rd_fill_rect(gfx, 0, 0, (float)cw, (float)ch, COL_CARD);
    rd_round_rect(gfx, 0.5f, 0.5f, (float)cw - 1, (float)ch - 1, 10 * sc, 0, COL_LINE, 1.0f);

    void *fb = rd_font(g.hwndMain, RD_FONT_BODY);
    void *fs = rd_font(g.hwndMain, RD_FONT_SMALL);
    t64 now = tnow();

    /* 当天出现的日程（含跨天覆盖到今天的全天日程），多条时自动轮播 */
    static Occ pocc[64];
    t64 t0 = day_start(now);
    int pn = schedule_occurrences(t0, t0 + 86400 - 1, pocc, 64);
    s.pillN = pn;

    if (pn == 0) {
        /* 当天无日程：回退显示「下一个即将日程」单页（保持原行为） */
        PillItem it;
        Occ up;
        if (schedule_next_upcoming(now, &up)) { pill_build(&up, now, &it); s.pillCurEv = up.evIdx; }
        else { wcscpy(it.title, L"无即将日程"); wcscpy(it.sub, L"点击展开 · ＋ 新建日程"); it.dot = PAL_ARGB[0]; s.pillCurEv = -1; }
        paint_pill_page(gfx, cw, ch, sc, fb, fs, &it, 0);
        return;
    }

    if (s.pillIdx >= pn) { s.pillIdx = 0; s.pillSlide = 1.0f; s.pillHoldStart = 0; }

    if (pn == 1) {
        PillItem it; pill_build(&pocc[0], now, &it);
        s.pillCurEv = pocc[0].evIdx;
        paint_pill_page(gfx, cw, ch, sc, fb, fs, &it, 0);
        return;
    }

    /* 多条：按 停留 → 到点向上滚动切下一条 的节奏轮播 */
    DWORD tc = GetTickCount();
    if (s.pillHoldStart == 0) { s.pillHoldStart = tc; s.pillSlide = 1.0f; }
    if (s.pillSlide >= 1.0f) {
        if (tc - s.pillHoldStart >= PILL_HOLD_MS) {
            s.pillIdx = (s.pillIdx + 1) % pn;
            s.pillSlide = 0.0f;
            s.pillAnimStart = tc;
        }
    } else {
        float p = (float)(tc - s.pillAnimStart) / PILL_ANIM_MS;
        if (p >= 1.0f) { p = 1.0f; s.pillHoldStart = tc; }
        s.pillSlide = p;
    }

    GdipSetClipRect(gfx, 0, 0, (float)cw, (float)ch, 0 /*Replace*/);
    float p = s.pillSlide;
    float ease = p * p * (3 - 2 * p);           /* smoothstep 缓动 */
    float off = ease * (float)ch;
    PillItem cur, nxt;
    int prev = (s.pillIdx - 1 + pn) % pn;
    pill_build(&pocc[prev], now, &cur);         /* 滚出（向上） */
    pill_build(&pocc[s.pillIdx], now, &nxt);    /* 滚入（自下） */
    s.pillCurEv = pocc[s.pillIdx].evIdx;        /* 悬停时取当前可见项的备注 */
    paint_pill_page(gfx, cw, ch, sc, fb, fs, &cur, -off);
    paint_pill_page(gfx, cw, ch, sc, fb, fs, &nxt, (float)ch - off);
    GdipResetClip(gfx);
}

/* ================= 日程页绘制 ================= */

static void paint_head(GpGraphics *gfx, int cw, float sc)
{
    void *ft = rd_font(g.hwndMain, RD_FONT_TITLE);
    void *fb = rd_font(g.hwndMain, RD_FONT_BODY);

    rd_text(gfx, L"日程助手", ft, COL_TXT, 14 * sc, 0, 150 * sc, (float)HEAD_H * sc,
            RD_HLEFT | RD_VCENTER);

    struct { HitKind k; const wchar_t *g; } btns[3] = {
        { HZ_COLLAPSE, L"—" }, { HZ_MENU, L"⋯" }, { HZ_NEW, L"＋" }
    };
    for (int i = 0; i < 3; i++) {
        float bx = cw - 8 * sc - BTN_SZ * sc - i * (BTN_SZ + 4) * sc;
        float by = ((float)HEAD_H - BTN_SZ) * sc / 2;
        int hover = (s.hover >= 0 && s.hits[s.hover].kind == btns[i].k);
        push_hit(btns[i].k, 0, bx, by, BTN_SZ * sc, BTN_SZ * sc);
        if (hover) rd_round_rect(gfx, bx, by, BTN_SZ * sc, BTN_SZ * sc, 8 * sc,
                                 0x14000000, 0, 0);
        rd_text(gfx, btns[i].g, fb, COL_SUB, bx, by, BTN_SZ * sc, BTN_SZ * sc,
                RD_HCENTER | RD_VCENTER);
    }
}

static void paint_calendar(GpGraphics *gfx, int cw, float sc)
{
    float y0 = (float)HEAD_H * sc;
    float mx = 10 * sc;
    float cellw = ((float)cw - 2 * mx) / 7;
    void *fd = rd_font(g.hwndMain, RD_FONT_DAY);
    void *fb = rd_font(g.hwndMain, RD_FONT_BODY);
    void *fs = rd_font(g.hwndMain, RD_FONT_SMALL);

    /* 月份切换行 */
    push_hit(HZ_CAL_PREV, 0, 6 * sc, y0, 26 * sc, 26 * sc);
    push_hit(HZ_CAL_NEXT, 0, cw - 32.0f * sc, y0, 26 * sc, 26 * sc);
    int hv = s.hover;
    if (hv >= 0 && s.hits[hv].kind == HZ_CAL_PREV)
        rd_round_rect(gfx, 6 * sc, y0, 26 * sc, 26 * sc, 6 * sc, 0x14000000, 0, 0);
    if (hv >= 0 && s.hits[hv].kind == HZ_CAL_NEXT)
        rd_round_rect(gfx, cw - 32.0f * sc, y0, 26 * sc, 26 * sc, 6 * sc, 0x14000000, 0, 0);
    rd_text(gfx, L"‹", fb, COL_SUB, 6 * sc, y0, 26 * sc, 26 * sc, RD_HCENTER | RD_VCENTER);
    rd_text(gfx, L"›", fb, COL_SUB, cw - 32.0f * sc, y0, 26 * sc, 26 * sc, RD_HCENTER | RD_VCENTER);

    /* 「今」一键回到今日：仅当显示月份不是本月或选中日不是今天时出现 */
    {
        int ty, tm, td;
        t64 today0 = day_start(tnow());
        tbreak(today0, &ty, &tm, &td, NULL, NULL);
        if (s.calY != ty || s.calM != tm || s.calSel != today0) {
            float tx = 36 * sc;
            int hov = (hv >= 0 && s.hits[hv].kind == HZ_CAL_TODAY);
            push_hit(HZ_CAL_TODAY, 0, tx, y0, 26 * sc, 26 * sc);
            if (hov)
                rd_round_rect(gfx, tx, y0, 26 * sc, 26 * sc, 6 * sc, 0x22000000, 0, 0);
            rd_text(gfx, L"今", fb, COL_ACCENT, tx, y0, 26 * sc, 26 * sc,
                    RD_HCENTER | RD_VCENTER);
        }
        (void)td;
    }

    wchar_t mon[32];
    _snwprintf(mon, 32, L"%d年%d月", s.calY, s.calM);
    rd_text(gfx, mon, fb, COL_TXT, 40 * sc, y0, cw - 80.0f * sc, (float)CAL_TH * sc,
            RD_HCENTER | RD_VCENTER);

    /* 星期行 */
    float wy = y0 + (float)CAL_TH * sc;
    static const wchar_t *WD = L"一二三四五六日";
    for (int i = 0; i < 7; i++) {
        wchar_t one[2] = { WD[i], 0 };
        rd_text(gfx, one, fs, COL_SUB, mx + i * cellw, wy, cellw,
                (float)CAL_WH * sc, RD_HCENTER | RD_VCENTER);
    }

    /* 日期网格 */
    float gy = wy + (float)CAL_WH * sc;
    int dim = days_in_month(s.calY, s.calM);
    int offset = weekday_mon0(tmk(s.calY, s.calM, 1, 0, 0));
    t64 today = day_start(tnow());
    t64 monthStart = tmk(s.calY, s.calM, 1, 0, 0);
    t64 monthEnd = monthStart + (t64)dim * 86400 - 1;

    /* 有日程的日期标记 */
    unsigned char hasEv[32];
    memset(hasEv, 0, sizeof(hasEv));
    Occ occs[256];
    int n = schedule_occurrences(monthStart, monthEnd, occs, 256);
    for (int i = 0; i < n; i++) {
        t64 d0 = day_start(occs[i].s < monthStart ? monthStart : occs[i].s);
        t64 d1 = day_start(occs[i].e > monthEnd ? monthEnd : occs[i].e);
        for (t64 d = d0; d <= d1 && d < d0 + 32 * 86400; d += 86400) {
            int yy, mm, dd; tbreak(d, &yy, &mm, &dd, NULL, NULL);
            if (dd >= 1 && dd <= 31) hasEv[dd] = 1;
        }
    }

    for (int cell = 0; cell < 42; cell++) {
        int day = 1 - offset + cell;
        if (day < 1 || day > dim) continue;
        int col = cell % 7, row = cell / 7;
        float x = mx + col * cellw, y = gy + row * (float)CAL_CELL * sc;
        t64 d = tmk(s.calY, s.calM, day, 0, 0);
        int isToday = (d == today);
        int isSel = (d == s.calSel);

        wchar_t num[8];
        _snwprintf(num, 8, L"%d", day);
        if (isToday)
            rd_circle(gfx, x + cellw / 2, y + (float)CAL_CELL * sc / 2, 10.5f * sc,
                      COL_ACCENT, 0, 0);
        else if (isSel)
            rd_circle(gfx, x + cellw / 2, y + (float)CAL_CELL * sc / 2, 11.5f * sc,
                      0, COL_ACCENT, 1.5f);
        rd_text(gfx, num, fd, isToday ? 0xFFFFFFFF : COL_TXT,
                x, y, cellw, (float)CAL_CELL * sc, RD_HCENTER | RD_VCENTER);
        if (hasEv[day])
            rd_circle(gfx, x + cellw / 2, y + (float)CAL_CELL * sc - 5 * sc,
                      1.8f * sc, isToday ? 0xFFFFFFFF : COL_ACCENT, 0, 0);

        push_hit(HZ_CAL_DAY, day, x, y, cellw, (float)CAL_CELL * sc);
    }
}

static void paint_agenda(GpGraphics *gfx, int cw, int ch, float sc)
{
    float y0 = ((float)HEAD_H + CAL_TH + CAL_WH + 6 * CAL_CELL) * sc;
    float avh = (float)ch - y0;
    if (avh <= 10) return;

    void *fb = rd_font(g.hwndMain, RD_FONT_BODY);
    void *fs = rd_font(g.hwndMain, RD_FONT_SMALL);

    t64 ws = s.calSel, we = s.calSel + 1LL * 86400 - 1;
    static Occ occs[256];
    int n = schedule_occurrences(ws, we, occs, 256);

    /* 先算总高以夹取滚动 */
    float total = 0;
    t64 today = day_start(tnow());
    t64 lastDay = 0; int haveDay = 0;
    s.chipCount = 0;
    for (int i = 0; i < n; i++) {
        t64 day = day_start(occs[i].s);
        if (day < ws) day = ws;
        if (!haveDay || day != lastDay) { total += (float)AG_HEAD * sc; haveDay = 1; lastDay = day; }
        total += ((float)CHIP_H + CHIP_GAP) * sc;
        if (s.chipCount < MAX_CHIP) s.chipCount++;
    }
    if (n == 0) total = (float)AG_HEAD * sc;
    float maxScroll = total - avh;
    if (maxScroll < 0) maxScroll = 0;
    if (s.agendaScroll < 0) s.agendaScroll = 0;
    if (s.agendaScroll > maxScroll) s.agendaScroll = (int)maxScroll;

    GdipSetClipRect(gfx, 0, y0, (float)cw, avh, 0 /*Replace*/);

    if (n == 0) {
        rd_text(gfx, L"当日暂无日程", fs, COL_SUB, 0, y0 + 20 * sc,
                (float)cw, 20 * sc, RD_HCENTER | RD_VTOP);
        GdipResetClip(gfx);
        return;
    }

    float yy = y0 - s.agendaScroll;
    static const wchar_t *WD = L"一二三四五六日";
    haveDay = 0; lastDay = 0;
    int chip = 0;
    for (int i = 0; i < n; i++) {
        t64 day = day_start(occs[i].s);
        if (day < ws) day = ws;
        if (!haveDay || day != lastDay) {
            /* 组头 */
            int y1, m1, d1, wdi; tbreak(day, &y1, &m1, &d1, NULL, NULL);
            wdi = weekday_mon0(day);
            wchar_t head[64];
            if (day == today)
                _snwprintf(head, 64, L"今天 %d月%d日 周%c", m1, d1, WD[wdi]);
            else if (day == today + 86400)
                _snwprintf(head, 64, L"明天 %d月%d日 周%c", m1, d1, WD[wdi]);
            else {
                int cy2; tbreak(tnow(), &cy2, NULL, NULL, NULL, NULL);
                if (y1 != cy2) _snwprintf(head, 64, L"%d年%d月%d日 周%c", y1, m1, d1, WD[wdi]);
                else           _snwprintf(head, 64, L"%d月%d日 周%c", m1, d1, WD[wdi]);
            }
            head[63] = 0;
            rd_text(gfx, head, fs, COL_SUB, 14 * sc, yy, cw - 28.0f * sc,
                    (float)AG_HEAD * sc, RD_HLEFT | RD_VCENTER);
            yy += (float)AG_HEAD * sc;
            haveDay = 1; lastDay = day;
        }
        if (chip < MAX_CHIP) {
            Event *ev = schedule_at(occs[i].evIdx);
            float cx = 10 * sc, cwid = (float)cw - 20 * sc;
            int hover = (s.hover >= 0 && s.hits[s.hover].kind == HZ_CHIP &&
                         s.hits[s.hover].idx == chip);
            push_hit(HZ_CHIP, chip, cx, yy, cwid, (float)CHIP_H * sc);
            s.chipEv[chip] = occs[i].evIdx;
            rd_round_rect(gfx, cx, yy, cwid, (float)CHIP_H * sc, 8 * sc,
                          COL_CARD, hover ? COL_ACCENT : COL_LINE, 1.0f);
            if (ev) {
                rd_round_rect(gfx, cx + 6 * sc, yy + 8 * sc, 4 * sc,
                              ((float)CHIP_H - 16) * sc, 2 * sc,
                              PAL_ARGB[ev->colorIdx % PAL_COUNT], 0, 0);
                rd_text(gfx, ev->title[0] ? ev->title : L"未命名日程", fb, COL_TXT,
                        cx + 18 * sc, yy + 6 * sc, cwid - 26 * sc, 18 * sc,
                        RD_HLEFT | RD_VTOP | RD_ELLIPSIS);
                wchar_t rng[96], rec[32]; rec[0] = 0;
                fmt_range(occs[i].s, occs[i].e, ev->allDay, rng, 96);
                if (ev->rec != REC_NONE) rec_label(ev, rec, 32);
                if (ev->allDay) {
                    /* 全天事件：只显示备注（有则显示、无则整行省略，不再显示「全天」） */
                    if (ev->note[0]) {
                        rd_text(gfx, ev->note, fs, COL_SUB, cx + 18 * sc, yy + 24 * sc,
                                cwid - 26 * sc, 14 * sc, RD_HLEFT | RD_VTOP | RD_ELLIPSIS);
                    }
                } else if (rec[0]) {
                    wchar_t line[128];
                    _snwprintf(line, 128, L"%s · %s", rng, rec);
                    line[127] = 0;
                    rd_text(gfx, line, fs, COL_SUB, cx + 18 * sc, yy + 24 * sc,
                            cwid - 26 * sc, 14 * sc, RD_HLEFT | RD_VTOP | RD_ELLIPSIS);
                } else {
                    rd_text(gfx, rng, fs, COL_SUB, cx + 18 * sc, yy + 24 * sc,
                            cwid - 26 * sc, 14 * sc, RD_HLEFT | RD_VTOP | RD_ELLIPSIS);
                }
            }
            yy += ((float)CHIP_H + CHIP_GAP) * sc;
            chip++;
        } else {
            yy += ((float)CHIP_H + CHIP_GAP) * sc;
        }
    }
    GdipResetClip(gfx);
}

static void paint_schedule_page(GpGraphics *gfx, int cw, int ch, float sc)
{
    paint_calendar(gfx, cw, sc);
    paint_agenda(gfx, cw, ch, sc);
}

/* ================= 主体绘制 ================= */

static void paint_body(GpGraphics *gfx, int cw, int ch, float sc)
{
    rd_fill_rect(gfx, 0, 0, (float)cw, (float)ch, COL_BG);
    paint_head(gfx, cw, sc);
    paint_schedule_page(gfx, cw, ch, sc);
}

static void paint_all(HDC hdc, int cw, int ch)
{
    s.hitCount = 0;
    GpGraphics *gfx = NULL;
    if (GdipCreateFromHDC(hdc, &gfx) != 0 || !gfx) return;
    GdipSetSmoothingMode(gfx, 2 /*AntiAlias*/);
    GdipSetTextRenderingHint(gfx, 3 /*ClearType*/);
    float sc = sc_of(g.hwndMain);
    if (s.collapsed) paint_pill(gfx, cw, ch, sc);
    else             paint_body(gfx, cw, ch, sc);
    GdipDeleteGraphics(gfx);
}

/* ================= 交互 ================= */

static void do_export(void)
{
    wchar_t file[MAX_PATH]; wcscpy(file, L"schedule.json");
    OPENFILENAMEW of; memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = g.hwndMain;
    of.lpstrFilter = L"JSON 日程文件 (*.json)\0*.json\0所有文件\0*.*\0";
    of.lpstrFile = file; of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"json";
    of.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&of)) return;
    int r = events_export(file);
    MessageBoxW(g.hwndMain, r == 0 ? L"导出成功" : L"导出失败，无法写入文件",
                L"日程助手", MB_OK | (r ? MB_ICONERROR : MB_ICONINFORMATION));
}

static void do_import(void)
{
    wchar_t file[MAX_PATH]; file[0] = 0;
    OPENFILENAMEW of; memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = g.hwndMain;
    of.lpstrFilter = L"JSON 日程文件 (*.json)\0*.json\0所有文件\0*.*\0";
    of.lpstrFile = file; of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return;
    int n = events_import(file);
    if (n < 0) {
        MessageBoxW(g.hwndMain, L"导入失败：文件无法读取或 JSON 无效",
                    L"日程助手", MB_OK | MB_ICONERROR);
    } else {
        wchar_t msg[64];
        _snwprintf(msg, 64, L"已导入 %d 条日程", n);
        msg[63] = 0;
        MessageBoxW(g.hwndMain, msg, L"日程助手", MB_OK | MB_ICONINFORMATION);
        InvalidateRect(g.hwndMain, NULL, FALSE);
    }
}

void widget_open_menu(void)
{
    HMENU m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING, 1, L"新建日程");
    AppendMenuW(m, MF_STRING, 2, L"设置");
    AppendMenuW(m, MF_STRING, 7, L"导入日程文件…");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 3, L"导出日程…");
    AppendMenuW(m, MF_STRING, 4, L"恢复备份（JSON）…");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g.st.autostart ? MF_CHECKED : 0), 5, L"开机自启");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 8, L"清除所有日程");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 6, L"退出");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g.hwndMain);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             pt.x, pt.y, 0, g.hwndMain, NULL);
    DestroyMenu(m);
    PostMessageW(g.hwndMain, WM_NULL, 0, 0);

    switch (cmd) {
    case 1: editor_open(g.hwndMain, NULL); break;
    case 2: settings_open(g.hwndMain); break;
    case 7: ai_window_toggle(); break;
    case 3: do_export(); break;
    case 4: do_import(); break;
    case 5:
        g.st.autostart = !g.st.autostart;
        autostart_set(g.st.autostart);
        settings_save();
        break;
    case 8:
        if (MessageBoxW(g.hwndMain, L"确定清除所有日程吗？此操作不可撤销。",
                        L"日程助手", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            schedule_clear();
            widget_refresh();
        }
        break;
    case 6: DestroyWindow(g.hwndMain); break;
    default: break;
    }
}

/* ================= 窗口过程 ================= */

static void handle_click(int hi)
{
    if (hi < 0 || hi >= s.hitCount) return;
    Hit *p = &s.hits[hi];
    switch (p->kind) {
    case HZ_NEW:      editor_open(g.hwndMain, NULL); break;
    case HZ_MENU:     widget_open_menu(); break;
    case HZ_COLLAPSE: widget_set_collapsed(1); break;
    case HZ_CAL_PREV:
        s.calM--; if (s.calM < 1) { s.calM = 12; s.calY--; }
        s.agendaScroll = 0;
        InvalidateRect(g.hwndMain, NULL, FALSE);
        break;
    case HZ_CAL_NEXT:
        s.calM++; if (s.calM > 12) { s.calM = 1; s.calY++; }
        s.agendaScroll = 0;
        InvalidateRect(g.hwndMain, NULL, FALSE);
        break;
    case HZ_CAL_TODAY: {
        /* 一键回到今日：月份、选中日、议程滚动全部复位 */
        t64 td64 = day_start(tnow());
        int ty, tm;
        tbreak(td64, &ty, &tm, NULL, NULL, NULL);
        s.calY = ty; s.calM = tm; s.calSel = td64;
        s.agendaScroll = 0;
        InvalidateRect(g.hwndMain, NULL, FALSE);
        break;
    }
    case HZ_CAL_DAY: {
        s.calSel = tmk(s.calY, s.calM, p->idx, 0, 0);
        s.agendaScroll = 0;
        InvalidateRect(g.hwndMain, NULL, FALSE);
        break;
    }
    case HZ_CHIP:
        if (p->idx < s.chipCount)
            editor_open(g.hwndMain, schedule_at(s.chipEv[p->idx]));
        break;
    default: break;
    }
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        theme_reload(1);
        theme_apply_dark_frame(hwnd);
        /* 原生 tooltip：悬停 chip/pill 时显示完整备注 */
        s.hwndTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (s.hwndTip) {
            TOOLINFOW ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            ti.hwnd = hwnd;
            ti.uId = 1;
            ti.lpszText = L"";
            SendMessageW(s.hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(s.hwndTip, TTM_SETMAXTIPWIDTH, 0, 360);
            SendMessageW(s.hwndTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
            /* 与主题色统一：COL_CARD 深背景 + COL_TXT 浅文字 */
            SendMessageW(s.hwndTip, TTM_SETTIPBKCOLOR, 0,
                         RGB((COL_CARD >> 16) & 0xFF, (COL_CARD >> 8) & 0xFF, COL_CARD & 0xFF));
            SendMessageW(s.hwndTip, TTM_SETTIPTEXTCOLOR, 0,
                         RGB((COL_TXT >> 16) & 0xFF, (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        }
        s.tipActive = 0;
        s.tipLastNote[0] = 0;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        /* 兼容 DC/位图按尺寸缓存：仅在窗口尺寸变化时重建，
         * 避免每次重绘都分配/释放 GDI 位图 */
        static HDC s_mem; static HBITMAP s_bmp; static int s_w, s_h;
        int cw = rc.right, ch = rc.bottom;
        if (!s_mem || s_w != cw || s_h != ch) {
            if (s_bmp) DeleteObject(s_bmp);
            if (s_mem) DeleteDC(s_mem);
            HDC sdc = GetDC(hwnd);
            s_mem = CreateCompatibleDC(sdc);
            s_bmp = CreateCompatibleBitmap(sdc, cw, ch);
            ReleaseDC(hwnd, sdc);
            SelectObject(s_mem, s_bmp);
            s_w = cw; s_h = ch;
        }
        paint_all(s_mem, cw, ch);
        BitBlt(hdc, 0, 0, cw, ch, s_mem, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTCLIENT;    /* 窗口固定不可拖动：头部不再充当标题栏 */
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { PT_X(lp), PT_Y(lp) };
        int hi = s.collapsed ? -1 : hit_at((float)pt.x, (float)pt.y);
        if (hi != s.hover) {
            s.hover = hi;
            InvalidateRect(hwnd, NULL, FALSE);
            SetCursor(LoadCursorW(NULL, hi >= 0 ? IDC_HAND : IDC_ARROW));
        }
        /* 悬停显示完整备注：展开态取当前 chip，收起态取当前 pill 事件 */
        const wchar_t *note = NULL;
        if (!s.collapsed && hi >= 0 && s.hits[hi].kind == HZ_CHIP) {
            Event *ev = schedule_at(s.chipEv[s.hits[hi].idx]);
            if (ev && ev->note[0]) note = ev->note;
        } else if (s.collapsed && s.pillCurEv >= 0) {
            Event *ev = schedule_at(s.pillCurEv);
            if (ev && ev->note[0]) note = ev->note;
        }
        if (note) {
            POINT sc = pt; ClientToScreen(hwnd, &sc);
            tip_show(note, sc.x + 18, sc.y + 18);
        } else {
            tip_hide();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (s.hover != -1) { s.hover = -1; InvalidateRect(hwnd, NULL, FALSE); }
        tip_hide();
        return 0;
    case WM_LBUTTONDOWN:
        tip_hide();
        return 0;
    case WM_LBUTTONUP:
        if (s.collapsed) {
            widget_set_collapsed(0);    /* 点击 pill 展开（不再支持拖动） */
            return 0;
        }
        handle_click(hit_at((float)PT_X(lp), (float)PT_Y(lp)));
        return 0;
    case WM_MOUSEWHEEL: {
        static int s_wheelAcc = 0;   /* 触摸板惯性事件 delta<120 时累积 */
        int delta = (int)(short)HIWORD(wp);
        s_wheelAcc += delta;
        int lines = s_wheelAcc / WHEEL_DELTA;   /* 携带方向与格数 */
        if (lines == 0) return 0;
        s_wheelAcc -= lines * WHEEL_DELTA;
        if (s.collapsed && s.pillN > 1) {
            /* 收起态：滚轮切换 pill 条目；切向相反方向（上滚=前一条，下滚=后一条） */
            int now = GetTickCount();
            s.pillIdx = (s.pillIdx + lines + s.pillN) % s.pillN;
            s.pillSlide = 1.0f;         /* 硬切，跳过动画 */
            s.pillHoldStart = now;      /* 重置轮播计时（暂停 PILL_HOLD_MS） */
            s.pillAnimStart = 0;
            tip_hide();                 /* 避免悬停 tooltip 显示上一条内容 */
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        s.agendaScroll += -lines * 64 * (int)sc_of(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_TIMER:
        switch (wp) {
        case TIMER_FULLSCREEN: fullscreen_tick(); break;
        case TIMER_REMIND:     notify_scan(); break;
        case TIMER_CLOCK:
            /* 每分钟清理刚过期的日程（定时事件结束后、全天事件跨日后消失）；
             * 有删除时复位议程滚动并刷新，pill/月历标记一并更新 */
            if (schedule_purge_expired() > 0) {
                s.agendaScroll = 0;
                widget_refresh();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case TIMER_PILL:
            /* 仅在收起态、当天有多条日程时驱动轮播；静止期不重绘，避免空转耗电 */
            if (s.collapsed && s.pillN >= 2) {
                DWORD tc = GetTickCount();
                if (s.pillSlide < 1.0f ||
                    (s.pillHoldStart && tc - s.pillHoldStart >= PILL_HOLD_MS))
                    InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED: {
        theme_reload(1);
        theme_apply_dark_frame(hwnd);
        /* 主题色变化时同步 tooltip 背景/文字 */
        if (s.hwndTip) {
            SendMessageW(s.hwndTip, TTM_SETTIPBKCOLOR, 0,
                         RGB((COL_CARD >> 16) & 0xFF, (COL_CARD >> 8) & 0xFF, COL_CARD & 0xFF));
            SendMessageW(s.hwndTip, TTM_SETTIPTEXTCOLOR, 0,
                         RGB((COL_TXT >> 16) & 0xFF, (COL_TXT >> 8) & 0xFF, COL_TXT & 0xFF));
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_APP_TRAY:
        tray_on_msg(lp);
        return 0;
    case WM_DPICHANGED:
        widget_apply_pos();   /* DPI 变化后重新计算右下角位置 */
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_FULLSCREEN);
        KillTimer(hwnd, TIMER_REMIND);
        KillTimer(hwnd, TIMER_CLOCK);
        KillTimer(hwnd, TIMER_PILL);
        tray_remove();
        PostQuitMessage(0);
        return 0;
    }
    /* explorer 重启后 TaskbarCreated → 重建托盘图标 */
    if (msg == tray_taskbar_msg() && tray_taskbar_msg()) {
        tray_add();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================= 公开接口 ================= */

static void enable_round_corners(HWND hwnd)
{
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    RtlGetVersionPtr p = (RtlGetVersionPtr)(void *)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    BOOL win11 = (p && p(&vi) == 0 && vi.dwBuildNumber >= 22000);
    if (win11) {
        DWORD pref = 2; /* DWMWCP_ROUND */
        DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                              &pref, sizeof(pref));
    } else {
        RECT r; GetWindowRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;
        int rad = 10 * ui_scale(hwnd) / 96;
        if (rad < 4) rad = 4;
        HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, rad, rad);
        SetWindowRgn(hwnd, rgn, FALSE);
    }
}

void widget_pin_desktop(void)
{
    /* 桌面层钉扎：把主窗口插到 Progman 正上方（桌面壁纸/图标之上、
     * 所有普通应用窗口之下）。
     *
     * 关键：SetWindowPos(hwnd, X) 把 hwnd 放到 X 下方。如果直接用
     * SetWindowPos(widget, progman) 会把组件沉到 Progman 之下（壁纸
     * 后面）→ 不可见。必须找到 Progman 正上方的窗口，插到它下方，
     * 这样组件就在 Progman 正上方。
     *
     * - 普通模式：Progman 在底部，组件插到其上方、应用之下 → 被应用遮挡
     * - 桌面模式（Win+D 最小化所有应用）：没有可见应用遮挡 → 组件可见
     * - 若组件已在 Progman 正上方 → 无需移动
     * 不碰 Progman/WorkerW 层级本身，与 Wallpaper Engine 兼容。 */
    if (!g.hwndMain || !IsWindowVisible(g.hwndMain)) return;
    HWND progman = FindWindowW(L"Progman", NULL);
    if (!progman) {
        /* 回退：沿 Z 链向下找 Progman/WorkerW，插到其正上方 */
        HWND below = GetWindow(g.hwndMain, GW_HWNDNEXT);
        HWND anchor = NULL;
        while (below) {
            wchar_t cls[64];
            if (!GetClassNameW(below, cls, 64)) break;
            if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) break;
            anchor = below;
            below = GetWindow(below, GW_HWNDNEXT);
        }
        if (anchor)
            SetWindowPos(g.hwndMain, anchor, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    /* 找 Progman 正上方的窗口 */
    HWND aboveProg = GetWindow(progman, GW_HWNDPREV);
    if (!aboveProg) {
        /* Progman 在最顶部（罕见）—— 把组件也置顶 */
        SetWindowPos(g.hwndMain, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else if (aboveProg != g.hwndMain) {
        /* 组件不在 Progman 正上方 → 插到 aboveProg 下方（Progman 上方） */
        SetWindowPos(g.hwndMain, aboveProg, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    /* else: 组件已在 Progman 正上方，无需移动 */
}

void widget_apply_pos(void)
{
    HWND hwnd = g.hwndMain;
    if (!hwnd) return;
    int w = base_w() * ui_scale(hwnd) / 96;
    int h = base_h() * ui_scale(hwnd) / 96;
    if (w < 1) w = 1; if (h < 1) h = 1;
    /* 固定在所在显示器工作区右下角 */
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    RECT wa;
    if (mon) { MONITORINFO mi; mi.cbSize = sizeof(mi); GetMonitorInfoW(mon, &mi); wa = mi.rcWork; }
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int margin = 16 * ui_scale(hwnd) / 96;
    int x = wa.right - w - margin;
    int y = wa.bottom - h - margin;
    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    enable_round_corners(hwnd);
    widget_pin_desktop();
}

void widget_set_collapsed(int c)
{
    c = c ? 1 : 0;
    if (!g.hwndMain || s.collapsed == c) return;
    s.collapsed = c;
    g.st.collapsed = c;
    settings_save();
    /* 收起/展开后重新计算右下角位置（而非沿用旧 rect 锚点，
     * 跨 DPI 或位置漂移后也能正确归位） */
    widget_apply_pos();
    s.hover = -1;
    if (c) { s.pillIdx = 0; s.pillSlide = 1.0f; s.pillHoldStart = 0; s.pillN = 0; }
    InvalidateRect(g.hwndMain, NULL, FALSE);
}

void widget_toggle_visible(void)
{
    if (!g.hwndMain) return;
    if (IsWindowVisible(g.hwndMain)) {
        ShowWindow(g.hwndMain, SW_HIDE);
    } else {
        /* 保持桌面层语义：不抢焦点、不置顶，重新钉到桌面之上 */
        ShowWindow(g.hwndMain, SW_SHOWNOACTIVATE);
        widget_pin_desktop();
    }
}

void widget_refresh(void)
{
    if (g.hwndMain) InvalidateRect(g.hwndMain, NULL, FALSE);
    ai_window_refresh();   /* 导入窗口若打开：同步按钮/状态并重绘 */
}

void widget_create(void)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = main_proc;
    wc.hInstance = g.hInst;
    wc.hIcon = g.hIcon;
    wc.hIconSm = g.hIcon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = WC_MAIN;
    RegisterClassExW(&wc);

    memset(&s, 0, sizeof(s));
    s.collapsed = g.st.collapsed ? 1 : 0;
    s.hover = -1;
    int y, m, d, hh, mm;
    tbreak(tnow(), &y, &m, &d, &hh, &mm);
    s.calY = y; s.calM = m;
    s.calSel = tmk(y, m, d, 0, 0);

    int w = base_w(), h = base_h();
    /* 无 TOPMOST：普通顶层窗口，由 widget_pin_desktop 钉在桌面之上 */
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, WC_MAIN,
        L"日程助手", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        NULL, NULL, g.hInst, NULL);
    g.hwndMain = hwnd;
    if (!hwnd) return;

    widget_apply_pos();
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    widget_pin_desktop();

    SetTimer(hwnd, TIMER_FULLSCREEN, 1000, NULL);
    SetTimer(hwnd, TIMER_REMIND, 30000, NULL);
    SetTimer(hwnd, TIMER_CLOCK, 60000, NULL);
    SetTimer(hwnd, TIMER_PILL, 30, NULL);   /* pill 轮播：动画期高频，停留期空转 */
}
