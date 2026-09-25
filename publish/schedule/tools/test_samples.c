/* test_samples.c — 验证 samples 模块生成的 4 个示范文件真实可解析：
 * xlsx 经 text_extract + CSV 解析、ics/json/csv 各自直解析，均应得 3 条。 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int g_calls = 0;
static int g_descOk = 0;
int schedule_add_raw(const char *date, const char *time,
                     const char *title, const char *desc)
{
    (void)date; (void)time;
    if (title && title[0] && desc && desc[0]) g_descOk++;
    g_calls++;
    return 1;
}

#include "../src/schedule_import.c"
#include "../src/samples.c"

App g;   /* samples_open_dir 用不到，只为链接提供符号 */

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS: %s\n", msg); \
         else { printf("FAIL: %s\n", msg); g_fail = 1; } } while (0)

static int parse_file(const wchar_t *path, int isIcs, int isJson)
{
    char *u8 = NULL; long n = 0;
    wchar_t err[128] = L"";
    if (!extract_text_from_file(path, &u8, &n, err, 128)) {
        wprintf(L"extract failed: %s\n", err);
        return -1;
    }
    int ok = 0, fail = 0, parsed;
    if (isIcs)       parsed = import_parse_ics_text(u8, &ok, &fail);
    else if (isJson) parsed = import_parse_json_text(u8, &ok, &fail);
    else             parsed = import_parse_csv_text(u8, &ok, &fail);
    free(u8);
    return parsed ? ok : -1;
}

int main(void)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t dir[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%ssw_samples_test", tmp);
    CreateDirectoryW(dir, NULL);

    CHECK(samples_write_all(dir) == 1, "生成 4 个示范文件");

    wchar_t p[MAX_PATH];
    DWORD attr;

    _snwprintf(p, MAX_PATH, L"%s\\日程示范.ics", dir);
    attr = GetFileAttributesW(p);
    CHECK(attr != INVALID_FILE_ATTRIBUTES, "ics 文件存在");
    g_calls = 0; g_descOk = 0;
    CHECK(parse_file(p, 1, 0) == 3, "ics 示范解析出 3 条");
    CHECK(g_descOk == 3, "ics 每条都带备注");

    _snwprintf(p, MAX_PATH, L"%s\\日程示范.csv", dir);
    CHECK(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES, "csv 文件存在");
    g_calls = 0; g_descOk = 0;
    CHECK(parse_file(p, 0, 0) == 3, "csv 示范解析出 3 条");
    CHECK(g_descOk == 3, "csv 每条都带备注");

    _snwprintf(p, MAX_PATH, L"%s\\日程示范.json", dir);
    CHECK(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES, "json 文件存在");
    g_calls = 0; g_descOk = 0;
    CHECK(parse_file(p, 0, 1) == 3, "json 示范解析出 3 条");
    CHECK(g_descOk == 3, "json 每条都带备注");

    _snwprintf(p, MAX_PATH, L"%s\\日程示范.xlsx", dir);
    attr = GetFileAttributesW(p);
    CHECK(attr != INVALID_FILE_ATTRIBUTES, "xlsx 文件存在");
    WIN32_FILE_ATTRIBUTE_DATA fad;
    int sizeOk = 0;
    if (attr != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesExW(p, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER sz;
        sz.HighPart = fad.nFileSizeHigh; sz.LowPart = fad.nFileSizeLow;
        sizeOk = sz.QuadPart > 300;   /* stored ZIP 含 6 个 XML 成员 */
        printf("  (xlsx size = %lld bytes)\n", sz.QuadPart);
    }
    CHECK(sizeOk, "xlsx 体积合理");
    g_calls = 0; g_descOk = 0;
    CHECK(parse_file(p, 0, 0) == 3, "xlsx 示范解 ZIP 后解析出 3 条");
    CHECK(g_descOk == 3, "xlsx 每条都带备注");

    printf(g_fail ? "\nSOME TESTS FAILED\n" : "\nALL PASS\n");
    return g_fail;
}
