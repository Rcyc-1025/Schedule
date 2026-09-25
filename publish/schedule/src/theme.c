/* theme.c — 系统主题配色实现
 *
 * 策略：
 *   1. 浅/深色：读注册表 HKCU\...\Themes\Personalize\AppsUseLightTheme
 *      （0=深色，1=浅色，缺失=浅色）。深色下背景/卡片加深、文字反白。
 *   2. 强调色：优先读注册表 HKCU\...\Explorer\Accent\AccentPalette
 *      （256 字节 BGRA，第 5 项为主强调色）；缺失则回退 DwmGetColorizationColor
 *      （用户开启"从背景自动取色"时，该值即壁纸主题色）。
 *   3. 变化监听：主窗口在 WM_SETTINGCHANGE / WM_THEMECHANGED 调用 theme_reload。
 */
#include "app.h"
#include "theme.h"
#include <dwmapi.h>
#include <stdlib.h>
#include <string.h>

DWORD g_colBg     = 0xFFF6F7FA;
DWORD g_colCard   = 0xFFFFFFFF;
DWORD g_colTxt    = 0xFF1F2329;
DWORD g_colSub    = 0xFF878E99;
DWORD g_colAccent = 0xFF3D7EFF;
DWORD g_colLine   = 0xFFE8EAEF;
DWORD g_colInput  = 0xFFF2F4F8;
int   g_themeDark = 0;

/* ---------- 注册表读取辅助 ---------- */

static DWORD reg_read_dword(HKEY root, const wchar_t *sub, const wchar_t *val, DWORD def)
{
    HKEY k = NULL;
    DWORD data = def, type = 0, sz = sizeof(data);
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return def;
    if (RegQueryValueExW(k, val, NULL, &type, (LPBYTE)&data, &sz) != ERROR_SUCCESS ||
        type != REG_DWORD) data = def;
    RegCloseKey(k);
    return data;
}

/* 读取 AccentPalette 中第 idx 项（BGRA），失败返回 0 */
static DWORD accent_from_palette(int idx)
{
    HKEY k = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
            0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    BYTE buf[256]; DWORD type = 0, sz = sizeof(buf);
    DWORD ret = 0;
    if (RegQueryValueExW(k, L"AccentPalette", NULL, &type, buf, &sz) == ERROR_SUCCESS &&
        type == REG_BINARY && sz >= (DWORD)((idx + 1) * 4)) {
        int off = idx * 4;
        /* AccentPalette 每项为 BGRA */
        BYTE bb = buf[off], gg = buf[off + 1], rr = buf[off + 2], aa = buf[off + 3];
        ret = ((DWORD)aa << 24) | ((DWORD)rr << 16) | ((DWORD)gg << 8) | bb;
    }
    RegCloseKey(k);
    return ret;
}

/* DwmGetColorizationColor 返回 0xAABBGGRR（alpha 已预乘），转换为 ARGB */
static DWORD accent_from_dwm(void)
{
    typedef HRESULT (WINAPI *Fn)(DWORD *, BOOL *);
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) return 0;
    Fn fn = (Fn)(void *)GetProcAddress(dwm, "DwmGetColorizationColor");
    if (!fn) return 0;
    DWORD color = 0; BOOL opaque = FALSE;
    if (fn(&color, &opaque) != S_OK) return 0;
    BYTE bb = (BYTE)((color >> 16) & 0xFF);
    BYTE gg = (BYTE)((color >> 8) & 0xFF);
    BYTE rr = (BYTE)(color & 0xFF);
    return ((DWORD)0xFF << 24) | ((DWORD)rr << 16) | ((DWORD)gg << 8) | bb;
}

/* 取系统强调色 ARGB（失败回退默认蓝） */
static DWORD system_accent(void)
{
    DWORD c = accent_from_palette(4);   /* AccentPalette 第 5 项 */
    if (!c) c = accent_from_dwm();
    if (!c) c = 0xFF3D7EFF;
    /* 确保 alpha 不透明 */
    return (c & 0x00FFFFFF) | 0xFF000000;
}

/* ---------- 配色计算 ---------- */

void theme_reload(int force)
{
    DWORD light = reg_read_dword(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", 1);
    g_themeDark = (light == 0) ? 1 : 0;

    g_colAccent = system_accent();

    if (g_themeDark) {
        /* 深色模式：深灰背景 + 浅灰文字 */
        g_colBg    = 0xFF1E1F22;
        g_colCard  = 0xFF2B2D31;
        g_colTxt   = 0xFFE7E9EA;
        g_colSub   = 0xFF9AA0A6;
        g_colLine  = 0xFF3C3F44;
        g_colInput = 0xFF232428;
    } else {
        /* 浅色模式：原配色 */
        g_colBg    = 0xFFF6F7FA;
        g_colCard  = 0xFFFFFFFF;
        g_colTxt   = 0xFF1F2329;
        g_colSub   = 0xFF878E99;
        g_colLine  = 0xFFE8EAEF;
        g_colInput = 0xFFF2F4F8;
    }
    (void)force;
}

/* ---- uxtheme 未文档化 dark mode（解析逻辑见下文）---- */
typedef BOOL  (WINAPI *PFN_AllowDarkModeForWindow)(HWND, BOOL);
typedef DWORD (WINAPI *PFN_SetPreferredAppMode)(DWORD);
typedef void  (WINAPI *PFN_RefreshColorPolicy)(void);
static PFN_AllowDarkModeForWindow s_allowDarkCtl = NULL;
static PFN_SetPreferredAppMode s_setAppMode = NULL;
static PFN_RefreshColorPolicy s_refreshPolicy = NULL;
static int s_darkApiInit = 0;
static void theme_dark_api_resolve(void);

/* DWM 窗口属性（Win11 22000+）：
 * 34=边框色 35=标题栏色 36=标题文字色；0xFFFFFFFF=COLOR_DEFAULT 恢复系统色 */
#define DWMWA_BORDER_COLOR   34
#define DWMWA_CAPTION_COLOR  35
#define DWMWA_TEXT_COLOR     36
#define DWM_COLOR_DEFAULT    ((COLORREF)0xFFFFFFFF)

void theme_apply_dark_frame(HWND hwnd)
{
    if (!hwnd) return;
    /* DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Win10 1809+)，作老版本兜底 */
    BOOL use = g_themeDark ? TRUE : FALSE;
    theme_dark_api_resolve();
    if (s_allowDarkCtl) s_allowDarkCtl(hwnd, use);
    DwmSetWindowAttribute(hwnd, 20, &use, sizeof(use));

    /* 24H2 + PerMonitorV2 实测：进程内设置 attr20 对标题栏不可靠（多次设置
     * 仍是浅色，从 DPI-unaware 的外部进程设置才生效），而 Win11 显式标题栏
     * 颜色（attr34/35/36）在窗口所属进程内即时稳定生效，故以它为准。
     * 浅色模式必须显式恢复 COLOR_DEFAULT，否则自定义色会残留。 */
    if (g_themeDark) {
        COLORREF border  = RGB((g_colLine >> 16) & 0xFF,
                               (g_colLine >> 8) & 0xFF, g_colLine & 0xFF);
        COLORREF caption = RGB((g_colCard >> 16) & 0xFF,
                               (g_colCard >> 8) & 0xFF, g_colCard & 0xFF);
        COLORREF textc   = RGB((g_colTxt >> 16) & 0xFF,
                               (g_colTxt >> 8) & 0xFF, g_colTxt & 0xFF);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,  &border,  sizeof(border));
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR,    &textc,   sizeof(textc));
    } else {
        COLORREF defc = DWM_COLOR_DEFAULT;
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,  &defc, sizeof(defc));
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &defc, sizeof(defc));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR,    &defc, sizeof(defc));
    }

    /* 已显示窗口强制重建非客户区，让新颜色立刻落地 */
    if (IsWindowVisible(hwnd)) {
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

/* ---------- Win32 经典控件深色（uxtheme 未文档化 API，Win10 1903+） ----------
 * 让 ComboBox 等经典控件整体跟随深色（静态区/箭头按钮/弹出列表）。
 * 注意：SetPreferredAppMode 必须在进程使用任何主题绘制之前调用，故拆为
 * theme_dark_mode_init()（WinMain 最早执行）+ 控件级 AllowDarkModeForWindow。 */
static void theme_dark_api_resolve(void)
{
    if (s_darkApiInit) return;
    s_darkApiInit = 1;
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    s_allowDarkCtl = (PFN_AllowDarkModeForWindow)
        GetProcAddress(ux, MAKEINTRESOURCEA(133));
    s_setAppMode = (PFN_SetPreferredAppMode)
        GetProcAddress(ux, MAKEINTRESOURCEA(135));
    s_refreshPolicy = (PFN_RefreshColorPolicy)
        GetProcAddress(ux, MAKEINTRESOURCEA(132));
}

void theme_dark_mode_init(void)
{
    if (!g_themeDark) return;
    theme_dark_api_resolve();
    if (s_setAppMode) s_setAppMode(1 /*AllowDark*/);
    if (s_refreshPolicy) s_refreshPolicy();
}

void theme_enable_dark_controls(HWND hwnd)
{
    if (!g_themeDark || !hwnd) return;
    theme_dark_api_resolve();
    if (s_allowDarkCtl) s_allowDarkCtl(hwnd, TRUE);
}
