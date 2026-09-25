/* render.h — GDI+ 绘制辅助（flat API）
 * 依赖：必须先包含 app.h（提供 gdiplusflat.h 类型） */
#ifndef RENDER_H
#define RENDER_H

#include <windows.h>

/* ================= GDI+ flat API 最小 C 声明 =================
 * SDK 的 gdiplusflat.h 依赖 C++ 语义（枚举名作类型），C 模式无法编译；
 * flat API 本身是稳定的 C ABI（gdiplus.dll），此处手工声明所需子集。 */
#ifndef GDIP_FLAT_DECL
#define GDIP_FLAT_DECL

typedef float  REAL;
typedef DWORD  ARGB;
typedef int    GpStatus;
typedef struct GpGraphics     GpGraphics;
typedef struct GpPath         GpPath;
typedef struct GpBrush        GpBrush;
typedef struct GpSolidFill    GpSolidFill;
typedef struct GpPen          GpPen;
typedef struct GpFont         GpFont;
typedef struct GpStringFormat GpStringFormat;

typedef struct { REAL X, Y, Width, Height; } GdipRectF;
typedef GdipRectF RectF;

typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GdiplusStartupInput;

#define GDIPAPI __stdcall
__declspec(dllimport) GpStatus GDIPAPI GdiplusStartup(ULONG_PTR *token,
    const GdiplusStartupInput *input, void *output);
__declspec(dllimport) VOID GDIPAPI GdiplusShutdown(ULONG_PTR token);

__declspec(dllimport) GpStatus GDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
__declspec(dllimport) GpStatus GDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
__declspec(dllimport) GpStatus GDIPAPI GdipGetDC(GpGraphics *graphics, HDC *hdc);
__declspec(dllimport) GpStatus GDIPAPI GdipReleaseDC(GpGraphics *graphics, HDC hdc);
__declspec(dllimport) GpStatus GDIPAPI GdipSetSmoothingMode(GpGraphics *g, int mode);
__declspec(dllimport) GpStatus GDIPAPI GdipSetTextRenderingHint(GpGraphics *g, int mode);
__declspec(dllimport) GpStatus GDIPAPI GdipSetClipRect(GpGraphics *g,
    REAL x, REAL y, REAL w, REAL h, int combineMode);
__declspec(dllimport) GpStatus GDIPAPI GdipResetClip(GpGraphics *g);

__declspec(dllimport) GpStatus GDIPAPI GdipCreatePath(int fillMode, GpPath **path);
__declspec(dllimport) GpStatus GDIPAPI GdipAddPathArc(GpPath *path,
    REAL x, REAL y, REAL w, REAL h, REAL startAngle, REAL sweepAngle);
__declspec(dllimport) GpStatus GDIPAPI GdipClosePathFigure(GpPath *path);
__declspec(dllimport) GpStatus GDIPAPI GdipDeletePath(GpPath *path);

__declspec(dllimport) GpStatus GDIPAPI GdipCreateSolidFill(ARGB color, GpSolidFill **brush);
__declspec(dllimport) GpStatus GDIPAPI GdipDeleteBrush(GpBrush *brush);
__declspec(dllimport) GpStatus GDIPAPI GdipFillPath(GpGraphics *g, GpBrush *b, GpPath *p);
__declspec(dllimport) GpStatus GDIPAPI GdipFillRectangle(GpGraphics *g,
    GpBrush *b, REAL x, REAL y, REAL w, REAL h);
__declspec(dllimport) GpStatus GDIPAPI GdipFillEllipse(GpGraphics *g,
    GpBrush *b, REAL x, REAL y, REAL w, REAL h);
__declspec(dllimport) GpStatus GDIPAPI GdipCreatePen1(ARGB color,
    REAL width, int unit, GpPen **pen);
__declspec(dllimport) GpStatus GDIPAPI GdipDeletePen(GpPen *pen);
__declspec(dllimport) GpStatus GDIPAPI GdipDrawPath(GpGraphics *g, GpPen *p, GpPath *path);
__declspec(dllimport) GpStatus GDIPAPI GdipDrawEllipse(GpGraphics *g,
    GpPen *p, REAL x, REAL y, REAL w, REAL h);

__declspec(dllimport) GpStatus GDIPAPI GdipCreateStringFormat(int attr,
    WORD lang, GpStringFormat **format);
__declspec(dllimport) GpStatus GDIPAPI GdipDeleteStringFormat(GpStringFormat *f);
__declspec(dllimport) GpStatus GDIPAPI GdipSetStringFormatFlags(GpStringFormat *f, int flags);
__declspec(dllimport) GpStatus GDIPAPI GdipSetStringFormatAlign(GpStringFormat *f, int align);
__declspec(dllimport) GpStatus GDIPAPI GdipSetStringFormatLineAlign(GpStringFormat *f, int align);
__declspec(dllimport) GpStatus GDIPAPI GdipSetStringFormatTrimming(GpStringFormat *f, int trimming);
__declspec(dllimport) GpStatus GDIPAPI GdipDrawString(GpGraphics *g,
    const WCHAR *s, INT len, const GpFont *font, const GdipRectF *layout,
    const GpStringFormat *fmt, const GpBrush *brush);
__declspec(dllimport) GpStatus GDIPAPI GdipMeasureString(GpGraphics *g,
    const WCHAR *s, INT len, const GpFont *font, const GdipRectF *layout,
    const GpStringFormat *fmt, GdipRectF *bounding, INT *codepoints, INT *lines);
__declspec(dllimport) GpStatus GDIPAPI GdipCreateFontFromLogfontW(HDC hdc,
    const LOGFONTW *logfont, GpFont **font);
__declspec(dllimport) GpStatus GDIPAPI GdipDeleteFont(GpFont *font);

#endif /* GDIP_FLAT_DECL */

/* GDI+ 生命周期 */
int  gfx_init(void);            /* 0 失败 */
void gfx_shutdown(void);

/* 字体缓存（按窗口 DPI 自动重建）
 * kind: RD_FONT_TITLE 标题15pt粗 / BODY 正文11pt / SMALL 小字9pt / DAY 日历10pt */
enum { RD_FONT_TITLE = 0, RD_FONT_BODY, RD_FONT_SMALL, RD_FONT_DAY, RD_FONT_COUNT };
void *rd_font(HWND w, int kind);

/* 文本绘制标志 */
enum {
    RD_HLEFT = 0, RD_HCENTER = 1, RD_HRIGHT = 2,   /* 低 2 位水平对齐 */
    RD_VTOP   = 0, RD_VCENTER = 4, RD_VBOTTOM = 8, /* 位 2-3 垂直对齐 */
    RD_ELLIPSIS = 16                               /* 单行省略号 */
};

/* 绘制文本（argb 为 ARGB 颜色，支持 alpha） */
void  rd_text(void *gfx, const wchar_t *s, void *font, DWORD argb,
              float x, float y, float w, float h, int flags);

/* 图元（fill/stroke 传 0 表示不绘制） */
void rd_round_rect(void *gfx, float x, float y, float w, float h, float r,
                   DWORD fillArgb, DWORD strokeArgb, float strokeWidth);
void rd_circle(void *gfx, float cx, float cy, float r,
               DWORD fillArgb, DWORD strokeArgb, float strokeWidth);
void rd_fill_rect(void *gfx, float x, float y, float w, float h, DWORD argb);

#endif
