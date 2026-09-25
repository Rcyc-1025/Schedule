/* test_extract.c — text_extract 模块自检驱动。
 * 在内存中构造最小 xlsx/docx（stored 方式 ZIP：local header +
 * central directory + EOCD，CRC32 自实现），写入 %TEMP%，
 * 调 extract_text_from_file 验证输出；另验证 UTF-8 CSV 与 OLE 旧格式拒绝。
 * 只写 %TEMP%，不动其他目录。 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text_extract.h"

static int g_fail = 0;

#define CHECK(cond, msg) \
    do { if (cond) printf("PASS: %s\n", msg); \
         else { printf("FAIL: %s\n", msg); g_fail = 1; } } while (0)

/* ---- CRC32 (IEEE 反射) ---- */
static unsigned crc_table[256];
static void crc_init(void)
{
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
}
static unsigned crc32_buf(const unsigned char *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---- 动态字节缓冲 ---- */
typedef struct { unsigned char *p; size_t len, cap; } Buf;

static void bput(Buf *b, const void *d, size_t n)
{
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->len + n) nc <<= 1;
        b->p = (unsigned char *)realloc(b->p, nc);
        b->cap = nc;
    }
    memcpy(b->p + b->len, d, n);
    b->len += n;
}
static void b16(Buf *b, unsigned v)
{
    unsigned char t[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
    bput(b, t, 2);
}
static void b32(Buf *b, unsigned v)
{
    unsigned char t[4] = {
        (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
        (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF)
    };
    bput(b, t, 4);
}

/* ---- ZIP 打包（全部 stored，method 0）---- */
typedef struct {
    const char *name;
    const char *data;
    size_t dlen;
    unsigned crc;
    size_t lho;
} Member;

static void zip_build(Buf *out, Member *m, int n)
{
    Buf cd;
    memset(&cd, 0, sizeof(cd));
    for (int i = 0; i < n; i++) {
        m[i].crc = crc32_buf((const unsigned char *)m[i].data, m[i].dlen);
        m[i].lho = out->len;
        b32(out, 0x04034b50u);          /* local file header */
        b16(out, 20);                   /* version needed */
        b16(out, 0);                    /* flags */
        b16(out, 0);                    /* method = stored */
        b16(out, 0); b16(out, 0);       /* time/date */
        b32(out, m[i].crc);
        b32(out, (unsigned)m[i].dlen);  /* csize */
        b32(out, (unsigned)m[i].dlen);  /* usize */
        b16(out, (unsigned)strlen(m[i].name));
        b16(out, 0);                    /* extra len */
        bput(out, m[i].name, strlen(m[i].name));
        bput(out, m[i].data, m[i].dlen);
    }
    for (int i = 0; i < n; i++) {
        b32(&cd, 0x02014b50u);          /* central directory header */
        b16(&cd, 20);                   /* version made by */
        b16(&cd, 20);                   /* version needed */
        b16(&cd, 0);                    /* flags */
        b16(&cd, 0);                    /* method */
        b16(&cd, 0); b16(&cd, 0);       /* time/date */
        b32(&cd, m[i].crc);
        b32(&cd, (unsigned)m[i].dlen);
        b32(&cd, (unsigned)m[i].dlen);
        b16(&cd, (unsigned)strlen(m[i].name));
        b16(&cd, 0);                    /* extra len */
        b16(&cd, 0);                    /* comment len */
        b16(&cd, 0);                    /* disk start */
        b16(&cd, 0);                    /* internal attrs */
        b32(&cd, 0);                    /* external attrs */
        b32(&cd, (unsigned)m[i].lho);
        bput(&cd, m[i].name, strlen(m[i].name));
    }
    size_t cdOff = out->len;
    bput(out, cd.p, cd.len);
    size_t cdSize = out->len - cdOff;
    b32(out, 0x06054b50u);              /* EOCD */
    b16(out, 0); b16(out, 0);           /* disk numbers */
    b16(out, (unsigned)n); b16(out, (unsigned)n);
    b32(out, (unsigned)cdSize);         /* central directory size */
    b32(out, (unsigned)cdOff);          /* central directory offset */
    b16(out, 0);                        /* comment len */
    free(cd.p);
}

static int write_temp(const wchar_t *name, const unsigned char *data, size_t n,
                      wchar_t *pathOut, int cap)
{
    wchar_t tmp[MAX_PATH];
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (!k || k >= MAX_PATH) return 0;
    _snwprintf_s(pathOut, (size_t)cap, _TRUNCATE, L"%s%s", tmp, name);
    HANDLE h = CreateFileW(pathOut, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &wr, NULL) && wr == n;
    CloseHandle(h);
    return ok ? 1 : 0;
}

int main(void)
{
    crc_init();

    /* ---- 1. 最小 xlsx ---- */
    static const char contentTypes[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types/>";
    static const char workbook[] =
        "<?xml version=\"1.0\"?><workbook/>";
    static const char shared[] =
        "<?xml version=\"1.0\"?><sst count=\"3\" uniqueCount=\"3\">"
        "<si><t>\xE4\xBC\x9A\xE8\xAE\xAE</t></si>"                /* 会议 */
        "<si><t>\xE9\xA1\xB9\xE7\x9B\xAE &amp; \xE8\xAE\xA1\xE5\x88\x92</t></si>" /* 项目 & 计划 */
        "<si><t>\xE5\xB8\xA6,\xE9\x80\x97\xE5\x8F\xB7 \"\xE5\xBC\x95\xE5\x8F\xB7\"</t></si>" /* 带,逗号 "引号" */
        "</sst>";
    static const char sheet[] =
        "<?xml version=\"1.0\"?><worksheet><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>2</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>2026-09-21</v></c><c r=\"B2\"><v>09:30</v></c>"
        "<c r=\"C2\" t=\"s\"><v>1</v></c></row>"
        "</sheetData></worksheet>";

    Member xm[4] = {
        { "[Content_Types].xml",   contentTypes, sizeof(contentTypes) - 1, 0, 0 },
        { "xl/workbook.xml",       workbook,     sizeof(workbook) - 1,     0, 0 },
        { "xl/sharedStrings.xml",  shared,       sizeof(shared) - 1,       0, 0 },
        { "xl/worksheets/sheet1.xml", sheet,     sizeof(sheet) - 1,        0, 0 },
    };
    Buf xz;
    memset(&xz, 0, sizeof(xz));
    zip_build(&xz, xm, 4);

    wchar_t xlsxPath[MAX_PATH];
    CHECK(write_temp(L"test_extract.xlsx", xz.p, xz.len, xlsxPath, MAX_PATH),
          "xlsx 写入 %TEMP%");

    char *out = NULL;
    long outLen = 0;
    wchar_t err[160];
    err[0] = 0;
    int r = extract_text_from_file(xlsxPath, &out, &outLen, err, 160);
    CHECK(r == 1, "xlsx 提取成功");
    if (r == 1) {
        printf("  xlsx 输出: [%.120s]\n", out);
        CHECK(strstr(out, "\xE4\xBC\x9A\xE8\xAE\xAE") != NULL,               /* 会议 */
              "xlsx 共享字符串 t=\"s\" 索引 0");
        CHECK(strstr(out, "2026-09-21, 09:30, \xE9\xA1\xB9\xE7\x9B\xAE & \xE8\xAE\xA1\xE5\x88\x92") != NULL,
              "xlsx 数值单元格 + 实体反转义(&)");
        CHECK(strstr(out, "\"\xE5\xB8\xA6,\xE9\x80\x97\xE5\x8F\xB7 \"\"\xE5\xBC\x95\xE5\x8F\xB7\"\"\"") != NULL,
              "xlsx 含逗号/引号文本按 CSV 转义");
        free(out);
        out = NULL;
    } else {
        wprintf(L"  err: %s\n", err);
    }
    free(xz.p);

    /* ---- 2. 最小 docx ---- */
    static const char doc[] =
        "<?xml version=\"1.0\"?><w:document><w:body>"
        "<w:p><w:r><w:t>\xE4\xBD\xA0\xE5\xA5\xBD</w:t></w:r></w:p>"   /* 你好 */
        "<w:p><w:r><w:t>\xE7\xAC\xAC\xE4\xBA\x8C\xE6\xAE\xB5 &lt;\xE6\xA0\x87\xE7\xAD\xBE&gt;</w:t></w:r>"
        "<w:r><w:tab/></w:r>"
        "<w:r><w:t>\xE6\x9C\xAB\xE5\xB0\xBE</w:t></w:r></w:p>"        /* 第二段 <标签> / 末尾 */
        "</w:body></w:document>";
    Member dm[2] = {
        { "[Content_Types].xml",  contentTypes, sizeof(contentTypes) - 1, 0, 0 },
        { "word/document.xml",    doc,          sizeof(doc) - 1,          0, 0 },
    };
    Buf dz;
    memset(&dz, 0, sizeof(dz));
    zip_build(&dz, dm, 2);

    wchar_t docxPath[MAX_PATH];
    CHECK(write_temp(L"test_extract.docx", dz.p, dz.len, docxPath, MAX_PATH),
          "docx 写入 %TEMP%");
    r = extract_text_from_file(docxPath, &out, &outLen, err, 160);
    CHECK(r == 1, "docx 提取成功");
    if (r == 1) {
        printf("  docx 输出: [%.120s]\n", out);
        CHECK(strstr(out, "\xE4\xBD\xA0\xE5\xA5\xBD\n"
                           "\xE7\xAC\xAC\xE4\xBA\x8C\xE6\xAE\xB5 <\xE6\xA0\x87\xE7\xAD\xBE>"
                           "\t\xE6\x9C\xAB\xE5\xB0\xBE") != NULL,
              "docx 段落换行 + 实体反转义 + <w:tab/> 制表");
        free(out);
        out = NULL;
    } else {
        wprintf(L"  err: %s\n", err);
    }
    free(dz.p);

    /* ---- 3. UTF-8 BOM CSV ---- */
    static const unsigned char csv[] = {
        0xEF, 0xBB, 0xBF,
        'd','a','t','e',',','t','i','t','l','e','\n',
        '2','0','2','6','-','0','9','-','2','2',',','z','h','o','u','\n'
    };
    wchar_t csvPath[MAX_PATH];
    CHECK(write_temp(L"test_extract.csv", csv, sizeof(csv), csvPath, MAX_PATH),
          "csv 写入 %TEMP%");
    r = extract_text_from_file(csvPath, &out, &outLen, err, 160);
    CHECK(r == 1 && out && strstr(out, "2026-09-22,zhou") != NULL,
          "csv UTF-8 BOM 跳过并直读");
    free(out);
    out = NULL;

    /* ---- 4. OLE 旧格式（.xls）拒绝 ---- */
    static const unsigned char ole[16] = {
        0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    wchar_t xlsPath[MAX_PATH];
    CHECK(write_temp(L"test_extract.xls", ole, sizeof(ole), xlsPath, MAX_PATH),
          "xls 写入 %TEMP%");
    r = extract_text_from_file(xlsPath, &out, &outLen, err, 160);
    CHECK(r == 0 && out == NULL && wcsstr(err, L"旧格式") != NULL,
          "OLE 魔数拒绝并给出另存提示");
    wprintf(L"  err: %s\n", err);

    printf(g_fail ? "\nFAIL\n" : "\nALL PASS\n");
    return g_fail ? 1 : 0;
}
