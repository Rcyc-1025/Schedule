/* util.h — 通用工具：编码转换、民用历法时间运算、字符串、模态循环 */
#ifndef UTIL_H
#define UTIL_H

#include <windows.h>

/* 民用秒：以本地墙钟计数的自 1970-01-01 起秒数（忽略时区/夏令时，纯日历算术） */
typedef long long t64;

/* 重复间隔单位（短到分钟，长到年） */
typedef enum { U_MIN = 0, U_HOUR, U_DAY, U_WEEK, U_MONTH, U_YEAR } TimeUnit;

/* ---- 编码转换 ---- */
void     u8_to_w16(const char *s, wchar_t *out, int cap);   /* 截断安全 */
wchar_t *u8_to_w16_dup(const char *s);                      /* 失败返回 NULL */
void     w16_to_u8(const wchar_t *s, char *out, int cap);
char    *w16_to_u8_dup(const wchar_t *s);
wchar_t *wstr_dup(const wchar_t *s);
void     utf16_assign(wchar_t **dst, const wchar_t *src);   /* free 旧值后复制 */

/* ---- 动态 UTF-8/UTF-16 缓冲 ---- */
typedef struct { char *p; size_t len, cap; } StrBuf8;
void  sb8_init(StrBuf8 *b);
void  sb8_free(StrBuf8 *b);
void  sb8_put(StrBuf8 *b, const char *s, size_t n);
void  sb8_puts(StrBuf8 *b, const char *s);

typedef struct { wchar_t *p; size_t len, cap; } StrBuf16;
void   sb16_init(StrBuf16 *b);
void   sb16_free(StrBuf16 *b);
void   sb16_put(StrBuf16 *b, const wchar_t *s, size_t n);
void   sb16_puts(StrBuf16 *b, const wchar_t *s);

/* ---- 民用历法 ---- */
int  is_leap(int y);
int  days_in_month(int y, int m);
t64  days_from_civil(int y, int m, int d);
void civil_from_days(t64 z, int *y, int *m, int *d);

t64  tmk(int y, int m, int d, int hh, int mm);   /* 分量 -> 民用秒 */
void tbreak(t64 t, int *y, int *m, int *d, int *hh, int *mm);  /* 可传 NULL */
t64  tnow(void);                                  /* 本地当前时刻 */
t64  day_start(t64 t);                            /* 当日 00:00 */
int  weekday_mon0(t64 t);                         /* 0=周一 ... 6=周日 */

/* 月/年按日历加减并对月末 clamp（1/31 + 1月 -> 2/28），其余单位按秒 */
t64  tadd_unit(t64 t, long long n, TimeUnit u);

/* ---- ISO 时间字符串 ---- */
/* 接受 "YYYY-MM-DDTHH:MM" | "YYYY-MM-DD HH:MM" | "YYYY-MM-DD"，返回 0 成功 */
int  iso_parse(const char *s, int *y, int *m, int *d, int *hh, int *mm);
t64  iso_to_t64(const char *s);                   /* 解析失败返回 0 */
void iso_from_t64(t64 t, char *out, int cap);     /* "YYYY-MM-DDTHH:MM" */
void iso_date_from_t64(t64 t, char *out, int cap); /* "YYYY-MM-DD" 全天日程用 */

/* ---- 显示格式化 ---- */
void fmt_time(t64 t, wchar_t *out, int cap);      /* 14:30 */
void fmt_range(t64 s, t64 e, int allDay, wchar_t *out, int cap); /* 智能区间 */

/* ---- 其他 ---- */
void gen_id(char *out, int cap);                  /* 32 位 hex id */
void run_modal(HWND dlg);                         /* 手动模态消息循环 */
void dlg_center(HWND dlg, HWND owner);            /* 相对属主居中 */
int  ui_scale(HWND w);                            /* DPI 缩放百分比(96=100) */

#endif
