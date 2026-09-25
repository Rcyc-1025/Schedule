/* util.c — 通用工具实现 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================= 编码转换 ================= */

void u8_to_w16(const char *s, wchar_t *out, int cap)
{
    if (cap <= 0) return;
    out[0] = 0;
    if (!s || !*s) return;
    int need = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (need <= 0) return;
    if (need > cap) need = cap;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, need);
    out[need - 1] = 0;
}

wchar_t *u8_to_w16_dup(const char *s)
{
    if (!s) s = "";
    int need = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (need <= 0) need = 1;
    wchar_t *w = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, need);
    w[need - 1] = 0;
    return w;
}

void w16_to_u8(const wchar_t *s, char *out, int cap)
{
    if (cap <= 0) return;
    out[0] = 0;
    if (!s || !*s) return;
    int need = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return;
    if (need > cap) need = cap;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, need, NULL, NULL);
    out[need - 1] = 0;
}

char *w16_to_u8_dup(const wchar_t *s)
{
    if (!s) s = L"";
    int need = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (need <= 0) need = 1;
    char *c = (char *)malloc((size_t)need);
    if (!c) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, c, need, NULL, NULL);
    c[need - 1] = 0;
    return c;
}

wchar_t *wstr_dup(const wchar_t *s)
{
    if (!s) s = L"";
    size_t n = wcslen(s);
    wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    memcpy(w, s, (n + 1) * sizeof(wchar_t));
    return w;
}

void utf16_assign(wchar_t **dst, const wchar_t *src)
{
    wchar_t *n = wstr_dup(src);
    free(*dst);
    *dst = n;
}

/* ================= 动态缓冲 ================= */

void sb8_init(StrBuf8 *b) { b->p = NULL; b->len = 0; b->cap = 0; }
void sb8_free(StrBuf8 *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static void sb8_reserve(StrBuf8 *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + extra + 1) nc *= 2;
    char *np = (char *)realloc(b->p, nc);
    if (np) { b->p = np; b->cap = nc; }
}

void sb8_put(StrBuf8 *b, const char *s, size_t n)
{
    if (!b || !s || !n) return;
    sb8_reserve(b, n);
    if (!b->p) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

void sb8_puts(StrBuf8 *b, const char *s) { if (s) sb8_put(b, s, strlen(s)); }

void sb16_init(StrBuf16 *b) { b->p = NULL; b->len = 0; b->cap = 0; }
void sb16_free(StrBuf16 *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static void sb16_reserve(StrBuf16 *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + extra + 1) nc *= 2;
    wchar_t *np = (wchar_t *)realloc(b->p, nc * sizeof(wchar_t));
    if (np) { b->p = np; b->cap = nc; }
}

void sb16_put(StrBuf16 *b, const wchar_t *s, size_t n)
{
    if (!b || !s || !n) return;
    sb16_reserve(b, n);
    if (!b->p) return;
    memcpy(b->p + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->p[b->len] = 0;
}

void sb16_puts(StrBuf16 *b, const wchar_t *s) { if (s) sb16_put(b, s, wcslen(s)); }

/* ================= 民用历法（Howard Hinnant 算法） ================= */

int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(int y, int m)
{
    static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 30;
    if (m == 2 && is_leap(y)) return 29;
    return dm[m - 1];
}

t64 days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    t64 era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);            /* [0, 399] */
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (t64)doe - 719468;
}

void civil_from_days(t64 z, int *y, int *m, int *d)
{
    z += 719468;
    t64 era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    t64 yy = (t64)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp + (mp < 10 ? 3 : -9);
    if (y) *y = (int)(yy + (mm <= 2));
    if (m) *m = (int)mm;
    if (d) *d = (int)dd;
}

t64 tmk(int y, int m, int d, int hh, int mm)
{
    return days_from_civil(y, m, d) * 86400 + (t64)hh * 3600 + (t64)mm * 60;
}

void tbreak(t64 t, int *y, int *m, int *d, int *hh, int *mm)
{
    t64 days = t / 86400;
    long long rem = t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    int Y, M, D;
    civil_from_days(days, &Y, &M, &D);
    if (y) *y = Y;
    if (m) *m = M;
    if (d) *d = D;
    if (hh) *hh = (int)(rem / 3600);
    if (mm) *mm = (int)((rem % 3600) / 60);
}

t64 tnow(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return tmk(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

t64 day_start(t64 t)
{
    t64 days = t / 86400;
    if (t % 86400 < 0) days -= 1;
    return days * 86400;
}

int weekday_mon0(t64 t)
{
    /* 1970-01-01 是周四；返回 0=周一 ... 6=周日 */
    t64 days = t / 86400;
    if (t % 86400 < 0) days -= 1;
    int w = (int)((days + 3) % 7);
    if (w < 0) w += 7;
    return w;
}

t64 tadd_unit(t64 t, long long n, TimeUnit u)
{
    if (n == 0) return t;
    int y, m, d, hh, mm;
    tbreak(t, &y, &m, &d, &hh, &mm);
    switch (u) {
    case U_MIN:   return t + n * 60;
    case U_HOUR:  return t + n * 3600;
    case U_DAY:   return t + n * 86400;
    case U_WEEK:  return t + n * 7 * 86400;
    case U_MONTH: {
        long long tot = (long long)y * 12 + (m - 1) + n;
        int y2 = (int)(tot >= 0 ? tot / 12 : (tot - 11) / 12);
        int m2 = (int)(tot - (long long)y2 * 12) + 1;
        if (m2 < 1) { m2 += 12; y2 -= 1; }
        if (m2 > 12) { m2 -= 12; y2 += 1; }
        if (y2 < 1 || y2 > 9999) return t;
        int d2 = d < days_in_month(y2, m2) ? d : days_in_month(y2, m2);
        return tmk(y2, m2, d2, hh, mm);
    }
    case U_YEAR: {
        int y2 = y + (int)n;
        if (y2 < 1 || y2 > 9999) return t;
        int d2 = d < days_in_month(y2, m) ? d : days_in_month(y2, m);
        return tmk(y2, m, d2, hh, mm);
    }
    }
    return t;
}

/* ================= ISO 时间 ================= */

int iso_parse(const char *s, int *y, int *m, int *d, int *hh, int *mm)
{
    if (!s) return -1;
    int Y = 0, M = 0, D = 0, H = 0, Mi = 0;
    int n = sscanf(s, "%4d-%2d-%2d%*[^0-9]%2d:%2d", &Y, &M, &D, &H, &Mi);
    if (n < 3) return -1;
    if (n < 5) { H = 0; Mi = 0; }
    if (Y < 1 || Y > 9999 || M < 1 || M > 12 || D < 1 || D > 31) return -1;
    if (H < 0 || H > 23 || Mi < 0 || Mi > 59) return -1;
    if (y) *y = Y;
    if (m) *m = M;
    if (d) *d = D;
    if (hh) *hh = H;
    if (mm) *mm = Mi;
    return 0;
}

t64 iso_to_t64(const char *s)
{
    int y, m, d, hh, mm;
    if (iso_parse(s, &y, &m, &d, &hh, &mm) != 0) return 0;
    return tmk(y, m, d, hh, mm);
}

void iso_from_t64(t64 t, char *out, int cap)
{
    int y, m, d, hh, mm;
    tbreak(t, &y, &m, &d, &hh, &mm);
    _snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d", y, m, d, hh, mm);
    out[cap - 1] = 0;
}

void iso_date_from_t64(t64 t, char *out, int cap)
{
    int y, m, d;
    tbreak(t, &y, &m, &d, NULL, NULL);
    _snprintf(out, cap, "%04d-%02d-%02d", y, m, d);
    out[cap - 1] = 0;
}

/* ================= 显示格式化 ================= */

void fmt_time(t64 t, wchar_t *out, int cap)
{
    int hh, mm; tbreak(t, NULL, NULL, NULL, &hh, &mm);
    _snwprintf(out, cap, L"%02d:%02d", hh, mm);
    out[cap - 1] = 0;
}

void fmt_range(t64 s, t64 e, int allDay, wchar_t *out, int cap)
{
    int sy, sm, sd, sh, smi; tbreak(s, &sy, &sm, &sd, &sh, &smi);
    int ey, em, ed, eh, emi; tbreak(e, &ey, &em, &ed, &eh, &emi);
    int sameDay = (sy == ey && sm == em && sd == ed);
    if (allDay) {
        if (sameDay)
            _snwprintf(out, cap, L"全天");
        else if (sy == ey)
            _snwprintf(out, cap, L"全天 · %d月%d日 ~ %d月%d日", sm, sd, em, ed);
        else
            _snwprintf(out, cap, L"全天 · %04d-%02d-%02d ~ %04d-%02d-%02d",
                       sy, sm, sd, ey, em, ed);
        out[cap - 1] = 0;
        return;
    }
    if (s == e)
        _snwprintf(out, cap, L"%02d:%02d", sh, smi);
    else if (sameDay)
        _snwprintf(out, cap, L"%02d:%02d – %02d:%02d", sh, smi, eh, emi);
    else if (sy == ey && (e - s) < 24 * 3600)
        _snwprintf(out, cap, L"%d月%d日 %02d:%02d ~ %d月%d日 %02d:%02d",
                   sm, sd, sh, smi, em, ed, eh, emi);
    else if (sy == ey)
        _snwprintf(out, cap, L"%d月%d日 ~ %d月%d日", sm, sd, em, ed);
    else
        _snwprintf(out, cap, L"%04d-%02d-%02d ~ %04d-%02d-%02d",
                   sy, sm, sd, ey, em, ed);
    out[cap - 1] = 0;
}

/* ================= 其他 ================= */

void gen_id(char *out, int cap)
{
    static volatile long counter = 0;
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    unsigned a = (unsigned)qpc.HighPart ^ (unsigned)GetTickCount();
    unsigned b = (unsigned)qpc.LowPart ^ (unsigned)(InterlockedIncrement(&counter) * 2654435761u);
    unsigned c = (unsigned)(uintptr_t)out ^ (unsigned)time(NULL);
    _snprintf(out, cap, "%08x%08x%08x%08x", a, b, c, b ^ 0x9e3779b9u);
    out[cap - 1] = 0;
}

void run_modal(HWND dlg)
{
    MSG m;
    for (;;) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                PostQuitMessage((int)m.wParam);
                return;
            }
            if (!IsDialogMessageW(dlg, &m)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        if (!IsWindow(dlg)) break;
        WaitMessage();
    }
}

void dlg_center(HWND dlg, HWND owner)
{
    RECT rd, ro;
    GetWindowRect(dlg, &rd);
    if (!owner || !GetWindowRect(owner, &ro)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &ro, 0);
    }
    int x = ro.left + ((ro.right - ro.left) - (rd.right - rd.left)) / 2;
    int y = ro.top + ((ro.bottom - ro.top) - (rd.bottom - rd.top)) / 3;
    SetWindowPos(dlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

int ui_scale(HWND w)
{
    UINT dpi = GetDpiForWindow(w);
    if (!dpi) dpi = 96;
    return (int)dpi;
}
