/* samples.c — 生成四个可直接打开/编辑/再导入的真实示范文件：
 *   日程示范.ics   iCalendar（定时事件 + 全天事件）
 *   日程示范.csv   带表头（UTF-8 BOM，Excel 双击不乱码）
 *   日程示范.xlsx  真实 OOXML（stored ZIP，Excel/WPS 可直接打开）
 *   日程示范.json  日程对象数组
 * 四份文件内容一致（同样 3 条日程），便于用户对照。 */
#include "app.h"
#include "samples.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "shell32.lib")

/* ================= 字节缓冲 ================= */

typedef struct { unsigned char *p; size_t len, cap; } Buf;

static void b_init(Buf *b) { b->p = NULL; b->len = b->cap = 0; }

static void b_put(Buf *b, const void *d, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->len + n + 1) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, nc);
        if (!np) return;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->len, d, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void b_puts(Buf *b, const char *s) { b_put(b, s, strlen(s)); }

static void b_u16(Buf *b, unsigned short v)
{
    unsigned char t[2] = { (unsigned char)(v & 0xFF), (unsigned char)(v >> 8) };
    b_put(b, t, 2);
}

static void b_u32(Buf *b, unsigned long v)
{
    unsigned char t[4] = {
        (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
        (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)
    };
    b_put(b, t, 4);
}

/* ================= CRC32（stored ZIP 需要）================= */

static unsigned crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_ready = 1;
}

static unsigned crc32_buf(const unsigned char *p, size_t n)
{
    if (!crc_ready) crc_init();
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ================= stored ZIP 打包 ================= */

typedef struct { const char *name; const Buf *data; } ZipEntry;

static void zip_build(Buf *out, const ZipEntry *entries, int count)
{
    /* central directory 项的元信息在遍历 local header 时收集 */
    unsigned long *crc = (unsigned long *)malloc((size_t)count * sizeof(unsigned long));
    size_t *off = (size_t *)malloc((size_t)count * sizeof(size_t));

    for (int i = 0; i < count; i++) {
        size_t nl = strlen(entries[i].name);
        size_t dl = entries[i].data->len;
        crc[i] = crc32_buf(entries[i].data->p, dl);
        off[i] = out->len;
        /* local file header */
        b_u32(out, 0x04034b50UL);
        b_u16(out, 20);          /* version needed */
        b_u16(out, 0);           /* flags */
        b_u16(out, 0);           /* method: stored */
        b_u16(out, 0); b_u16(out, 0);  /* mod time/date */
        b_u32(out, crc[i]);
        b_u32(out, (unsigned long)dl);
        b_u32(out, (unsigned long)dl);
        b_u16(out, (unsigned short)nl);
        b_u16(out, 0);           /* extra len */
        b_put(out, entries[i].name, nl);
        b_put(out, entries[i].data->p, dl);
    }

    size_t cdOff = out->len;
    for (int i = 0; i < count; i++) {
        size_t nl = strlen(entries[i].name);
        size_t dl = entries[i].data->len;
        b_u32(out, 0x02014b50UL);
        b_u16(out, 20);          /* version made by */
        b_u16(out, 20);          /* version needed */
        b_u16(out, 0);           /* flags */
        b_u16(out, 0);           /* method */
        b_u16(out, 0); b_u16(out, 0);
        b_u32(out, crc[i]);
        b_u32(out, (unsigned long)dl);
        b_u32(out, (unsigned long)dl);
        b_u16(out, (unsigned short)nl);
        b_u16(out, 0); b_u16(out, 0);   /* extra / comment len */
        b_u16(out, 0);                  /* disk number */
        b_u16(out, 0);                  /* internal attrs */
        b_u32(out, 0);                  /* external attrs */
        b_u32(out, (unsigned long)off[i]);
        b_put(out, entries[i].name, nl);
    }
    size_t cdSize = out->len - cdOff;
    /* EOCD */
    b_u32(out, 0x06054b50UL);
    b_u16(out, 0); b_u16(out, 0);
    b_u16(out, (unsigned short)count);
    b_u16(out, (unsigned short)count);
    b_u32(out, (unsigned long)cdSize);
    b_u32(out, (unsigned long)cdOff);
    b_u16(out, 0);

    free(crc); free(off);
}

/* ================= 示范数据（4 份文件共用）================= */

static const char *S_HEAD[3] = { "日期", "标题", "备注" };
static const char *S_ROWS[3][3] = {
    { "2026-09-21", "项目评审", "3 楼会议室" },
    { "2026-09-21", "团队午餐", "餐厅二楼" },
    { "2026-09-21", "晚间复盘", "会议室 A" }
};

/* ---- ICS（同一天多个全天日程，VALUE=DATE）---- */
static void build_ics(Buf *b)
{
    b_puts(b,
"BEGIN:VCALENDAR\r\n"
"VERSION:2.0\r\n"
"PRODID:-//ScheduleWidget//Sample//CN\r\n"
"BEGIN:VEVENT\r\n"
"UID:sample-1@schedulewidget\r\n"
"DTSTAMP:20260101T000000Z\r\n"
"DTSTART;VALUE=DATE:20260921\r\n"
"SUMMARY:项目评审\r\n"
"DESCRIPTION:3 楼会议室\r\n"
"END:VEVENT\r\n"
"BEGIN:VEVENT\r\n"
"UID:sample-2@schedulewidget\r\n"
"DTSTAMP:20260101T000000Z\r\n"
"DTSTART;VALUE=DATE:20260921\r\n"
"SUMMARY:团队午餐\r\n"
"DESCRIPTION:餐厅二楼\r\n"
"END:VEVENT\r\n"
"BEGIN:VEVENT\r\n"
"UID:sample-3@schedulewidget\r\n"
"DTSTAMP:20260101T000000Z\r\n"
"DTSTART;VALUE=DATE:20260921\r\n"
"SUMMARY:晚间复盘\r\n"
"DESCRIPTION:会议室 A\r\n"
"END:VEVENT\r\n"
"END:VCALENDAR\r\n");
}

/* ---- CSV（UTF-8 BOM）---- */
static void build_csv(Buf *b)
{
    static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    b_put(b, bom, 3);
    b_puts(b, "日期,标题,备注\r\n");
    for (int i = 0; i < 3; i++) {
        /* 备注含空格无需引号；空备注保留尾部逗号 */
        b_puts(b, S_ROWS[i][0]); b_put(b, ",", 1);
        b_puts(b, S_ROWS[i][1]); b_put(b, ",", 1);
        b_puts(b, S_ROWS[i][2]);
        b_put(b, "\r\n", 2);
    }
}

/* ---- JSON（同一天多个全天日程，仅有 date 无 time）---- */
static void build_json(Buf *b)
{
    b_puts(b,
"[\r\n"
"  { \"date\": \"2026-09-21\", \"title\": \"项目评审\", \"desc\": \"3 楼会议室\" },\r\n"
"  { \"date\": \"2026-09-21\", \"title\": \"团队午餐\", \"desc\": \"餐厅二楼\" },\r\n"
"  { \"date\": \"2026-09-21\", \"title\": \"晚间复盘\", \"desc\": \"会议室 A\" }\r\n"
"]\r\n");
}

/* ---- XLSX：所有单元格走 sharedStrings ---- */
static void xml_escape_put(Buf *b, const char *s)
{
    for (; *s; s++) {
        if (*s == '&')      b_puts(b, "&amp;");
        else if (*s == '<') b_puts(b, "&lt;");
        else if (*s == '>') b_puts(b, "&gt;");
        else                b_put(b, s, 1);
    }
}

static const char *COL_LETTER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static void build_xlsx(Buf *out)
{
    Buf ct, rels, wb, wbrels, ss, sheet;
    b_init(&ct); b_init(&rels); b_init(&wb);
    b_init(&wbrels); b_init(&ss); b_init(&sheet);

    b_puts(&ct,
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
"<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
"<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
"<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
"</Types>");

    b_puts(&rels,
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
"</Relationships>");

    b_puts(&wb,
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
"<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
"<sheets><sheet name=\"日程\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");

    b_puts(&wbrels,
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
"</Relationships>");

    /* 数据矩阵：首行表头，后 3 行；空备注不输出单元格 */
    const char *mat[4][3];
    for (int c = 0; c < 3; c++) mat[0][c] = S_HEAD[c];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) mat[r + 1][c] = S_ROWS[r][c];

    b_puts(&ss,
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
"<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    b_puts(&sheet,
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
"<sheetData>");

    int idx = 0;
    char ref[48];
    for (int r = 0; r < 4; r++) {
        char rowTag[32];
        _snprintf(rowTag, sizeof(rowTag), "<row r=\"%d\">", r + 1);
        rowTag[sizeof(rowTag) - 1] = 0;
        b_puts(&sheet, rowTag);
        for (int c = 0; c < 3; c++) {
            const char *v = mat[r][c];
            if (!v || !v[0]) continue;                 /* 空单元格 */
            b_puts(&ss, "<si><t>");
            xml_escape_put(&ss, v);
            b_puts(&ss, "</t></si>");
            _snprintf(ref, sizeof(ref), "<c r=\"%c%d\" t=\"s\"><v>%d</v></c>",
                      COL_LETTER[c], r + 1, idx++);
            ref[sizeof(ref) - 1] = 0;
            b_puts(&sheet, ref);
        }
        b_puts(&sheet, "</row>");
    }
    b_puts(&sheet, "</sheetData></worksheet>");
    b_puts(&ss, "</sst>");

    ZipEntry entries[6] = {
        { "[Content_Types].xml",            &ct },
        { "_rels/.rels",                    &rels },
        { "xl/workbook.xml",                &wb },
        { "xl/_rels/workbook.xml.rels",     &wbrels },
        { "xl/sharedStrings.xml",           &ss },
        { "xl/worksheets/sheet1.xml",       &sheet }
    };
    zip_build(out, entries, 6);

    b_init(&ct); b_init(&rels); b_init(&wb);
    b_init(&wbrels); b_init(&ss); b_init(&sheet);
    free(ct.p); free(rels.p); free(wb.p);
    free(wbrels.p); free(ss.p); free(sheet.p);
}

/* ================= 落盘 ================= */

static int write_file(const wchar_t *path, const unsigned char *data, size_t len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD wr = 0;
    int ok = WriteFile(h, data, (DWORD)len, &wr, NULL) && wr == len;
    CloseHandle(h);
    return ok;
}

static int join_path(wchar_t *out, int cap, const wchar_t *dir, const wchar_t *name)
{
    int n = _snwprintf(out, cap, L"%s\\%s", dir, name);
    out[cap - 1] = 0;
    return n > 0 && n < cap;
}

int samples_write_all(const wchar_t *dir)
{
    Buf ics, csv, js, xls;
    b_init(&ics); b_init(&csv); b_init(&js); b_init(&xls);
    build_ics(&ics); build_csv(&csv); build_json(&js); build_xlsx(&xls);

    wchar_t path[MAX_PATH];
    int ok = 1;
    if (ok) ok &= join_path(path, MAX_PATH, dir, L"日程示范.ics") &&
                  write_file(path, ics.p, ics.len);
    if (ok) ok &= join_path(path, MAX_PATH, dir, L"日程示范.csv") &&
                  write_file(path, csv.p, csv.len);
    if (ok) ok &= join_path(path, MAX_PATH, dir, L"日程示范.json") &&
                  write_file(path, js.p, js.len);
    if (ok) ok &= join_path(path, MAX_PATH, dir, L"日程示范.xlsx") &&
                  write_file(path, xls.p, xls.len);

    free(ics.p); free(csv.p); free(js.p); free(xls.p);
    return ok;
}

int samples_open_dir(void)
{
    wchar_t dir[MAX_PATH];
    if (_snwprintf(dir, MAX_PATH, L"%s\\samples", g.dir) <= 0) return 0;
    dir[MAX_PATH - 1] = 0;
    CreateDirectoryW(dir, NULL);   /* 已存在则忽略 ERROR_ALREADY_EXISTS */
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) return 0;
    if (!samples_write_all(dir)) return 0;
    /* 用资源管理器打开示范文件夹 */
    HINSTANCE r = ShellExecuteW(NULL, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}
