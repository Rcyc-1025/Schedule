/* render.c — GDI+ flat API 封装：初始化、字体缓存、圆角矩形/圆/文本 */
#include "app.h"
#include "render.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static ULONG_PTR s_token = 0;
static void caches_free(void);  /* 前置声明：gfx_shutdown 先于定义使用 */

/* ================= 生命周期 ================= */

int gfx_init(void)
{
    if (s_token) return 1;
    GdiplusStartupInput si;
    memset(&si, 0, sizeof(si));
    si.GdiplusVersion = 1;
    return GdiplusStartup(&s_token, &si, NULL) == 0; /* Ok = 0 */
}

/* ================= 字体缓存 ================= */

typedef struct { GpFont *f; UINT dpi; } FontSlot;
static FontSlot s_fonts[RD_FONT_COUNT];

static const struct { int pt, bold; } FONT_SPEC[RD_FONT_COUNT] = {
    { 15, 1 },   /* 标题 */
    { 11, 0 },   /* 正文 */
    {  9, 0 },   /* 小字 */
    { 10, 0 }    /* 日历 */
};

void *rd_font(HWND w, int kind)
{
    if (kind < 0 || kind >= RD_FONT_COUNT) return NULL;
    UINT dpi = GetDpiForWindow(w);
    if (!dpi) dpi = 96;
    FontSlot *s = &s_fonts[kind];
    if (s->f && s->dpi == dpi) return s->f;
    if (s->f) { GdipDeleteFont(s->f); s->f = NULL; }

    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(FONT_SPEC[kind].pt, (int)dpi, 72);
    lf.lfWeight = FONT_SPEC[kind].bold ? FW_BOLD : FW_NORMAL;
    wcscpy(lf.lfFaceName, L"Microsoft YaHei UI");
    HDC sdc = GetDC(NULL);
    GdipCreateFontFromLogfontW(sdc, &lf, &s->f);
    ReleaseDC(NULL, sdc);
    s->dpi = dpi;
    return s->f;
}

static void fonts_free(void)
{
    for (int i = 0; i < RD_FONT_COUNT; i++) {
        if (s_fonts[i].f) GdipDeleteFont(s_fonts[i].f);
        s_fonts[i].f = NULL;
        s_fonts[i].dpi = 0;
    }
}

void gfx_shutdown(void)
{
    fonts_free();
    caches_free();
    if (s_token) { GdiplusShutdown(s_token); s_token = 0; }
}

/* ================= 画刷/画笔/StringFormat 缓存 =================
 * rd_text/rd_round_rect 等每帧被调用几十次，此前每次都
 * Create+Delete GDI+ 对象。主题色板很小（<20 色），按值缓存后
 * 命中时零分配，进程生命周期内持有，gfx_shutdown 统一释放。 */

static struct { DWORD argb; GpBrush *b; } s_brushes[32];
static int s_nBrushes;

static GpBrush *brush_for(DWORD argb)
{
    for (int i = 0; i < s_nBrushes; i++)
        if (s_brushes[i].argb == argb) return s_brushes[i].b;
    if (s_nBrushes < 32) {
        GpSolidFill *b = NULL;
        if (GdipCreateSolidFill(argb, &b) == 0) {
            s_brushes[s_nBrushes].argb = argb;
            s_brushes[s_nBrushes].b = (GpBrush *)b;
            s_nBrushes++;
            return (GpBrush *)b;
        }
    }
    /* 缓存满：退回临时创建（调用方不释放，泄漏可忽略且极少发生） */
    GpSolidFill *b = NULL;
    GdipCreateSolidFill(argb, &b);
    return (GpBrush *)b;
}

static struct { DWORD argb; float w; GpPen *p; } s_pens[16];
static int s_nPens;

static GpPen *pen_for(DWORD argb, float width)
{
    for (int i = 0; i < s_nPens; i++)
        if (s_pens[i].argb == argb && s_pens[i].w == width) return s_pens[i].p;
    if (s_nPens < 16) {
        GpPen *p = NULL;
        if (GdipCreatePen1(argb, width > 0 ? width : 1.0f,
                           2 /*UnitPixel*/, &p) == 0) {
            s_pens[s_nPens].argb = argb;
            s_pens[s_nPens].w = width;
            s_pens[s_nPens].p = p;
            s_nPens++;
            return p;
        }
    }
    GpPen *p = NULL;
    GdipCreatePen1(argb, width > 0 ? width : 1.0f, 2, &p);
    return p;
}

/* 文本对齐组合共 3(halign) x 3(valign) = 9 种，按组合缓存 */
static GpStringFormat *s_tfmt[9];

static GpStringFormat *tfmt_for(int halign, int valign)
{
    GpStringFormat *sf = s_tfmt[halign * 3 + valign];
    if (!sf && GdipCreateStringFormat(0, 0, &sf) == 0) {
        GdipSetStringFormatAlign(sf, halign);
        GdipSetStringFormatLineAlign(sf, valign);
        s_tfmt[halign * 3 + valign] = sf;
    }
    return sf;
}

static void caches_free(void)
{
    for (int i = 0; i < s_nBrushes; i++) GdipDeleteBrush(s_brushes[i].b);
    s_nBrushes = 0;
    for (int i = 0; i < s_nPens; i++) GdipDeletePen(s_pens[i].p);
    s_nPens = 0;
    for (int i = 0; i < 9; i++) {
        if (s_tfmt[i]) GdipDeleteStringFormat(s_tfmt[i]);
        s_tfmt[i] = NULL;
    }
}

/* ================= 文本 ================= */

void rd_text(void *gfx, const wchar_t *s, void *font, DWORD argb,
             float x, float y, float w, float h, int flags)
{
    if (!gfx || !font || !s || !s[0] || w <= 0 || h <= 0) return;
    int halign = flags & 3;
    int ha = halign == 1 ? 1 : (halign == 2 ? 2 : 0);
    int va = (flags & RD_VCENTER) ? 1 : ((flags & RD_VBOTTOM) ? 2 : 0);
    GpStringFormat *sf = tfmt_for(ha, va);
    if (!sf) return;

    /* RD_ELLIPSIS：GDI+ 的 NoWrap 标志会使 CJK 字符串静默不绘制，
     * 因此保持默认 format（可换行），改用超宽布局测宽 + 手动截断加 "…"。
     * 截断后的字符串必然单行放得下，绘制时不会触发换行 */
    const wchar_t *draw = s;
    wchar_t buf[256];
    if (flags & RD_ELLIPSIS) {
        static const wchar_t ELL[] = L"…";
        RectF wide = { 0, 0, 100000.0f, 100000.0f }, bounds = { 0, 0, 0, 0 };
        size_t len = wcslen(s);
        GdipMeasureString((GpGraphics *)gfx, s, (INT)len, (GpFont *)font,
                          &wide, sf, &bounds, NULL, NULL);
        float fullw = bounds.Width;
        if (fullw > w) {
            RectF eb = wide;
            bounds.Width = 0;
            GdipMeasureString((GpGraphics *)gfx, ELL, 1, (GpFont *)font,
                              &eb, sf, &bounds, NULL, NULL);
            float avail = w - bounds.Width;      /* 扣掉省略号后的可用宽 */
            if (avail <= 0) {
                draw = L"";
            } else {
                size_t keep = (size_t)((double)len * (double)avail /
                                       (fullw > 1.0f ? (double)fullw : 1.0));
                if (keep > len) keep = len;
                if (keep > 254) keep = 254;
                memcpy(buf, s, keep * sizeof(wchar_t));
                buf[keep] = 0;
                for (;;) {
                    bounds.Width = 0;
                    GdipMeasureString((GpGraphics *)gfx, buf, (INT)keep,
                                      (GpFont *)font, &wide, sf, &bounds, NULL, NULL);
                    if (bounds.Width <= avail || keep == 0) break;
                    keep--;
                    buf[keep] = 0;
                }
                if (keep == 0) {
                    draw = ELL;
                } else {
                    memcpy(buf + keep, ELL, 2 * sizeof(wchar_t));
                    draw = buf;
                }
            }
        }
    }

    if (draw[0]) {
        GpBrush *brush = brush_for(argb);
        if (brush) {
            RectF rc = { x, y, w, h };
            GdipDrawString((GpGraphics *)gfx, draw, (INT)wcslen(draw),
                           (GpFont *)font, &rc, sf, brush);
        }
    }
}

/* ================= 图元 ================= */

void rd_round_rect(void *gfx, float x, float y, float w, float h, float r,
                   DWORD fillArgb, DWORD strokeArgb, float strokeWidth)
{
    if (!gfx || w <= 0 || h <= 0) return;
    if (r < 0.5f) r = 0.5f;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    GpPath *path = NULL;
    if (GdipCreatePath(0 /*FillModeAlternate*/, &path) != 0) return;
    float d = r * 2;
    GdipAddPathArc(path, x,         y,         d, d, 180.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y,         d, d, 270.0f, 90.0f);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    GdipAddPathArc(path, x,         y + h - d, d, d,  90.0f, 90.0f);
    GdipClosePathFigure(path);

    if (fillArgb)
        GdipFillPath((GpGraphics *)gfx, brush_for(fillArgb), path);
    if (strokeArgb)
        GdipDrawPath((GpGraphics *)gfx, pen_for(strokeArgb, strokeWidth), path);
    GdipDeletePath(path);
}

void rd_circle(void *gfx, float cx, float cy, float r,
               DWORD fillArgb, DWORD strokeArgb, float strokeWidth)
{
    if (!gfx || r <= 0) return;
    float x = cx - r, y = cy - r, d = r * 2;
    if (fillArgb)
        GdipFillEllipse((GpGraphics *)gfx, brush_for(fillArgb), x, y, d, d);
    if (strokeArgb)
        GdipDrawEllipse((GpGraphics *)gfx,
                        pen_for(strokeArgb, strokeWidth), x, y, d, d);
}

void rd_fill_rect(void *gfx, float x, float y, float w, float h, DWORD argb)
{
    if (!gfx) return;
    GdipFillRectangle((GpGraphics *)gfx, brush_for(argb), x, y, w, h);
}
