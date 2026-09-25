/* schedule_import.c — 标准日程文件本地直解析（ics / csv / xlsx / json）
 * 不经过 AI、不联网。解析后逐条调用 schedule_add_raw() 落盘。 */
#include "app.h"
#include "schedule.h"
#include "schedule_import.h"
#include "text_extract.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================= 日期/时间灵活解析 ================= */

/* 支持：2026-09-21 / 2026/9/1 / 2026.09.01 / 20260921 / 2026年9月1日
 * 成功写 "YYYY-MM-DD" 到 out（至少 11 字节）。 */
static int flex_date(const char *s, char *out)
{
    if (!s) return 0;
    int nums[6]; int cnt = 0, cur = 0, has = 0;
    for (const char *p = s; *p && cnt < 6; p++) {
        if (*p >= '0' && *p <= '9') { cur = cur * 10 + (*p - '0'); has = 1; }
        else if (has) { nums[cnt++] = cur; cur = 0; has = 0; }
    }
    if (has) nums[cnt++] = cur;

    int y, m, d;
    if (cnt == 1 && nums[0] >= 19000101 && nums[0] <= 99991231) {
        y = nums[0] / 10000; m = (nums[0] / 100) % 100; d = nums[0] % 100;
    } else if (cnt >= 3 && nums[0] >= 1000 && nums[0] <= 9999) {
        y = nums[0]; m = nums[1]; d = nums[2];
    } else return 0;

    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    _snprintf(out, 11, "%04d-%02d-%02d", y, m, d);
    out[10] = 0;
    return 1;
}

/* 支持：9:00 / 09:00 / 09:00:00（以冒号定位时分）。
 * 成功写 "HH:MM" 到 out（至少 6 字节）；无冒号失败。 */
static int flex_time(const char *s, char *out)
{
    if (!s) return 0;
    const char *c = strchr(s, ':');
    if (!c) return 0;
    int h = 0, mi = 0, mul = 1;
    const char *p = c - 1;
    while (p >= s && *p >= '0' && *p <= '9') { h += (*p - '0') * mul; mul *= 10; p--; }
    if (mul == 1) return 0;                       /* 冒号前无数字 */
    if (sscanf(c + 1, "%2d", &mi) != 1) return 0;
    if (h > 23 || mi > 59) return 0;
    _snprintf(out, 6, "%02d:%02d", h, mi);
    out[5] = 0;
    return 1;
}

static void add_one(const char *date, const char *time,
                    const char *title, const char *desc,
                    int *ok, int *fail)
{
    if (schedule_add_raw(date, time,
                         title && title[0] ? title : "(无标题)",
                         desc && desc[0] ? desc : NULL))
        (*ok)++;
    else
        (*fail)++;
}

/* ================= JSON ================= */

static const char *jstr(cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return (cJSON_IsString(v) && v->valuestring) ? v->valuestring : NULL;
}

/* 从 ISO "YYYY-MM-DDTHH:MM" 或 "YYYY-MM-DD HH:MM" 拆日期/时间 */
static void split_iso(const char *v, char *date /*11*/, char *time /*6*/)
{
    date[0] = 0; time[0] = 0;
    if (!v) return;
    size_t n = strlen(v);
    if (n >= 10) {
        memcpy(date, v, 10); date[10] = 0;
        if (!flex_date(date, date)) date[0] = 0;
    }
    const char *t = strchr(v, 'T');
    if (!t) t = strchr(v, ' ');
    if (t && strlen(t + 1) >= 5) {
        memcpy(time, t + 1, 5); time[5] = 0;
        char norm[6];
        if (!flex_time(time, norm)) time[0] = 0;
        else strcpy(time, norm);
    }
}

int import_parse_json_text(const char *s, int *ok, int *fail)
{
    if (!s) return 0;
    cJSON *root = cJSON_Parse(s);
    if (!root) return 0;
    cJSON *arr = root;
    if (!cJSON_IsArray(root)) {
        /* 容忍 {"events":[...]} / {"items":[...]} 包裹 */
        cJSON *a = cJSON_GetObjectItemCaseSensitive(root, "events");
        if (!cJSON_IsArray(a)) a = cJSON_GetObjectItemCaseSensitive(root, "items");
        if (!cJSON_IsArray(a)) { cJSON_Delete(root); return 0; }
        arr = a;
    }
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsObject(it)) { (*fail)++; continue; }
        char date[16] = "", time[16] = "";
        const char *jd = jstr(it, "date");
        const char *jt = jstr(it, "time");
        if (jd) { if (!flex_date(jd, date)) date[0] = 0; }
        if (jt) { if (!flex_time(jt, time)) time[0] = 0; }
        if (!date[0]) {
            const char *start = jstr(it, "start");
            if (start) split_iso(start, date, time);
        }
        if (!date[0]) { (*fail)++; continue; }
        /* 无时刻字段 → 全天日程（最细到天） */
        const char *title = jstr(it, "title");
        if (!title) title = jstr(it, "summary");
        if (!title) title = jstr(it, "name");
        const char *desc = jstr(it, "desc");
        if (!desc) desc = jstr(it, "description");
        if (!desc) desc = jstr(it, "note");
        add_one(date, time, title, desc, ok, fail);
    }
    cJSON_Delete(root);
    return 1;
}

/* ================= ICS (iCalendar) ================= */

/* RFC 5545 行折叠：CRLF 后紧跟空格/TAB 表示续行，拼接时去掉 CRLF 与该空白。
 * 返回 malloc 缓冲（调用方 free）。 */
static char *ics_unfold(const char *s)
{
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == '\r' || ch == '\n') {
            size_t nlLen = (ch == '\r' && s[i + 1] == '\n') ? 2 : 1;
            char after = s[i + nlLen];
            if (after == ' ' || after == '\t') {
                i += (long)nlLen;       /* 跳过换行和续行空白 */
                continue;
            }
            out[w++] = '\n';           /* 统一换行 */
            i += (long)(nlLen - 1);
            continue;
        }
        out[w++] = ch;
    }
    out[w] = 0;
    return out;
}

/* ICS 文本转义反转义：\\ \, \; \n \N */
static void ics_unescape(char *s)
{
    size_t r = 0, w = 0;
    for (; s[r]; r++) {
        if (s[r] == '\\' && s[r + 1]) {
            char nx = s[++r];
            if (nx == 'n' || nx == 'N') s[w++] = '\n';
            else if (nx == ',') s[w++] = ',';
            else if (nx == ';') s[w++] = ';';
            else if (nx == '\\') s[w++] = '\\';
            else { s[w++] = '\\'; s[w++] = nx; }
        } else s[w++] = s[r];
    }
    s[w] = 0;
}

/* 解析 DTSTART 值：YYYYMMDD 或 YYYYMMDDTHHMMSS（Z/时区忽略，按本地时间） */
static void ics_dtstart(const char *val, char *date /*11*/, char *time /*6*/)
{
    date[0] = 0; time[0] = 0;
    char digits[32]; int n = 0;
    for (const char *p = val; *p && n < 30; p++)
        if (*p >= '0' && *p <= '9') digits[n++] = *p;
    digits[n] = 0;
    if (n < 8) return;
    _snprintf(date, 11, "%.4s-%.2s-%.2s", digits, digits + 4, digits + 6);
    date[10] = 0;
    if (n >= 13) {
        _snprintf(time, 6, "%.2s:%.2s", digits + 8, digits + 10);
        time[5] = 0;
        int hh = (digits[8] - '0') * 10 + (digits[9] - '0');
        int mm = (digits[10] - '0') * 10 + (digits[11] - '0');
        if (hh > 23 || mm > 59) time[0] = 0;
    }
}

int import_parse_ics_text(const char *raw, int *ok, int *fail)
{
    if (!raw) return 0;
    char *s = ics_unfold(raw);
    if (!s) return 0;

    int inEvent = 0, saw = 0;
    char date[16] = "", time[16] = "";
    char title[512] = "", desc[2048] = "";

    char *save = NULL;
    for (char *line = strtok_s(s, "\n", &save); line;
         line = strtok_s(NULL, "\n", &save)) {
        if (_stricmp(line, "BEGIN:VEVENT") == 0) {
            inEvent = 1; saw = 1;
            date[0] = time[0] = title[0] = desc[0] = 0;
            continue;
        }
        if (!inEvent) continue;
        if (_stricmp(line, "END:VEVENT") == 0) {
            if (date[0]) {
                /* time 为空（VALUE=DATE 的全天事件）→ 全天日程 */
                add_one(date, time, title, desc, ok, fail);
            } else (*fail)++;
            inEvent = 0;
            continue;
        }
        /* 属性名取到 ';' 或 ':'；value 为第一个 ':' 之后 */
        char *colon = strchr(line, ':');
        if (!colon) continue;
        char *name = line;
        size_t nlen = (size_t)(colon - line);
        char *semi = memchr(line, ';', nlen);
        if (semi) nlen = (size_t)(semi - line);
        char *value = colon + 1;

        /* name 已截到 ';' 或 ':' 前，故 DTSTART 无论有无参数长度均为 7 */
        if (nlen == 7 && _strnicmp(name, "DTSTART", 7) == 0) {
            ics_dtstart(value, date, time);
        } else if (nlen == 7 && _strnicmp(name, "SUMMARY", 7) == 0) {
            strncpy(title, value, sizeof(title) - 1); title[sizeof(title)-1] = 0;
            ics_unescape(title);
        } else if (nlen == 11 && _strnicmp(name, "DESCRIPTION", 11) == 0) {
            strncpy(desc, value, sizeof(desc) - 1); desc[sizeof(desc)-1] = 0;
            ics_unescape(desc);
        }
    }
    free(s);
    return saw;
}

/* ================= CSV ================= */

#define CSV_MAXCOL 32
#define CSV_MAXFIELD 2048

typedef struct {
    char *f[CSV_MAXCOL];
    int   n;
} CsvRow;

static void row_reset(CsvRow *r)
{
    for (int i = 0; i < r->n; i++) { free(r->f[i]); r->f[i] = NULL; }
    r->n = 0;
}

static void row_push(CsvRow *r, char *field)
{
    if (r->n < CSV_MAXCOL) {
        if (strlen(field) >= CSV_MAXFIELD) field[CSV_MAXFIELD - 1] = 0;
        r->f[r->n++] = field;
    } else free(field);
}

/* 取下一个逻辑行（引号内换行属于同一记录）。*io 为文本偏移。
 * 返回字段已 malloc 的 CsvRow；文本结束返回 0。 */
static int csv_next_row(const char *s, size_t *io, CsvRow *out)
{
    int any = 0, inQuote = 0, started = 0;
    size_t cap = 64, len = 0;
    char *f = (char *)malloc(cap);
    if (!f) return 0;
    f[0] = 0;

    for (;;) {
        char c = s[*io];
        if (c == 0) {
            if (!started && !any) { free(f); return 0; }
            row_push(out, f);
            return 1;
        }
        started = 1;
        if (inQuote) {
            if (c == '"') {
                if (s[*io + 1] == '"') { (*io)++; if (len + 1 < cap) f[len++] = '"'; }
                else inQuote = 0;
            } else {
                if (len + 1 >= cap) { cap *= 2; f = realloc(f, cap); }
                f[len++] = c;
            }
            (*io)++;
            continue;
        }
        if (c == '"') { inQuote = 1; (*io)++; continue; }
        if (c == ',') {
            f[len] = 0; row_push(out, f);
            f = (char *)malloc(64); f[0] = 0; cap = 64; len = 0;
            (*io)++; continue;
        }
        if (c == '\n' || c == '\r') {
            if (c == '\r' && s[*io + 1] == '\n') (*io)++;
            (*io)++;
            f[len] = 0; row_push(out, f);
            return 1;
        }
        if (len + 1 >= cap) { cap *= 2; f = realloc(f, cap); }
        f[len++] = c;
        (*io)++;
    }
}

static char *trim_dup(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    const char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
    size_t n = (size_t)(e - s);
    char *r = (char *)malloc(n + 1);
    memcpy(r, s, n); r[n] = 0;
    return r;
}

static int lower_contains(const char *hay, const char *needle)
{
    /* ASCII 小写包含匹配；中文 needle 直接字节匹配（strcmp 语义在 strstr 内成立） */
    size_t hl = strlen(hay), nl = strlen(needle);
    if (nl > hl) return 0;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t k = 0;
        for (; k < nl; k++) {
            char a = hay[i + k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != needle[k]) break;
        }
        if (k == nl) return 1;
    }
    return 0;
}

/* 判断表头单元格属于哪一列：返回 0date 1time 2title 3desc，-1 无 */
static int header_kind(const char *h)
{
    static const char *keys[4][9] = {
        { "date", "日期", "start date", NULL },
        { "time", "时间", NULL },
        { "title", "summary", "subject", "name", "事件", "标题", "主题", "名称", NULL },
        { "desc", "description", "note", "备注", "描述", "说明", "详情", NULL }
    };
    for (int k = 0; k < 4; k++)
        for (int i = 0; keys[k][i]; i++)
            if (lower_contains(h, keys[k][i])) return k;
    return -1;
}

int import_parse_csv_text(const char *s, int *ok, int *fail)
{
    if (!s) return 0;
    size_t io = 0;
    /* 跳过 UTF-8 BOM（示范 CSV 带 BOM） */
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        io = 3;
    CsvRow row; memset(&row, 0, sizeof(row));
    int col[4] = { 0, 1, 2, 3 };   /* date/time/title/desc 列索引（默认列序） */
    int haveHeader = 0, saw = 0;

    while (csv_next_row(s, &io, &row)) {
        saw = 1;
        /* 跳过完全空行 */
        int empty = 1;
        for (int i = 0; i < row.n; i++) {
            /* trim 每个字段前后空白（xlsx 导出、或外部工具可能在逗号后加空格） */
            char *p = row.f[i];
            if (p) {
                while (*p == ' ' || *p == '\t') { memmove(p, p + 1, strlen(p)); }
                size_t L = strlen(p);
                while (L > 0 && (p[L-1] == ' ' || p[L-1] == '\t')) { p[--L] = 0; }
            }
            if (row.f[i] && row.f[i][0]) { empty = 0; }
        }
        if (empty) { row_reset(&row); continue; }

        if (!haveHeader) {
            /* 第一行含「日期/date」字样才认定为表头 */
            int isHeader = 0, assign[4] = { -1, -1, -1, -1 };
            for (int i = 0; i < row.n; i++) {
                int k = header_kind(row.f[i]);
                if (k == 0) isHeader = 1;
                if (k >= 0 && assign[k] < 0) assign[k] = i;
            }
            if (isHeader) {
                if (assign[0] >= 0) col[0] = assign[0];
                if (assign[1] >= 0) col[1] = assign[1];
                if (assign[2] >= 0) col[2] = assign[2];
                if (assign[3] >= 0) col[3] = assign[3];
                haveHeader = 1;
                row_reset(&row);
                continue;
            }
        }

        char *rawDate = (col[0] < row.n) ? row.f[col[0]] : NULL;
        char *rawTime = (col[1] < row.n) ? row.f[col[1]] : NULL;
        char *rawTitle = (col[2] < row.n) ? row.f[col[2]] : NULL;
        char *rawDesc = (col[3] < row.n) ? row.f[col[3]] : NULL;

        char date[16] = "", time[16] = "";
        if (rawDate) flex_date(rawDate, date);
        if (!date[0] && rawTime) {
            /* 日期列可能写成 "2026-09-21 09:00" 合并形式 */
            flex_date(rawTime, date);
        }
        if (rawTime) flex_time(rawTime, time);
        /* 时间列留空 → 全天日程 */

        if (date[0]) {
            char *t = rawTitle ? trim_dup(rawTitle) : NULL;
            char *d = rawDesc  ? trim_dup(rawDesc)  : NULL;
            add_one(date, time, t, d, ok, fail);
            free(t); free(d);
        } else (*fail)++;
        row_reset(&row);
    }
    return saw;
}

/* ================= 文件分派 ================= */

int schedule_import_file(const wchar_t *path, int *ok, int *fail,
                         wchar_t *errMsg, int errCap)
{
    if (ok) *ok = 0;
    if (fail) *fail = 0;
    #define SETERR(msg) do { if (errMsg && errCap > 0) { \
        wcsncpy(errMsg, msg, errCap - 1); errMsg[errCap - 1] = 0; } } while (0)

    if (!path) { SETERR(L"未选择文件"); return 0; }
    const wchar_t *dot = wcsrchr(path, L'.');
    if (!dot) { SETERR(L"文件没有扩展名"); return 0; }

    int isIcs = (_wcsicmp(dot, L".ics") == 0);
    int isCsv = (_wcsicmp(dot, L".csv") == 0);
    int isXls = (_wcsicmp(dot, L".xlsx") == 0);
    int isJson = (_wcsicmp(dot, L".json") == 0);
    if (!isIcs && !isCsv && !isXls && !isJson) {
        SETERR(L"仅支持 ics / csv / xlsx / json 文件");
        return 0;
    }

    char *u8 = NULL; long u8Len = 0;
    wchar_t exErr[160]; exErr[0] = 0;
    if (!extract_text_from_file(path, &u8, &u8Len, exErr, 160)) {
        SETERR(exErr[0] ? exErr : L"读取文件失败");
        return 0;
    }

    int parsed;
    if (isJson)      parsed = import_parse_json_text(u8, ok, fail);
    else if (isIcs)  parsed = import_parse_ics_text(u8, ok, fail);
    else             parsed = import_parse_csv_text(u8, ok, fail);  /* csv + xlsx */
    free(u8);

    if (!parsed) { SETERR(isJson ? L"JSON 结构无法识别（应为日程数组）" :
                                  L"未能识别出有效的日程行"); return 0; }
    return 1;
}
