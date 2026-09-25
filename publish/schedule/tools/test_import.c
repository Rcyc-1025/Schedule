/* test_import.c — schedule_import 本地解析自检（ics/csv/json）。
 * 通过 #include 整个 .c 并打桩 schedule_add_raw / extract_text_from_file，
 * 只验证解析逻辑，不触磁盘数据。 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int g_calls = 0;
static char g_last[4][600];

int schedule_add_raw(const char *date, const char *time,
                     const char *title, const char *desc)
{
    if (g_calls < 4) {
        _snprintf(g_last[g_calls], 600, "%s|%s|%s|%s",
                  date, time, title ? title : "", desc ? desc : "");
    }
    g_calls++;
    return 1;
}

int extract_text_from_file(const wchar_t *path, char **out, long *outLen,
                           wchar_t *err, int errCap)
{
    (void)path; (void)out; (void)outLen;
    if (err && errCap) err[0] = 0;
    return 0;
}

#include "../src/schedule_import.c"

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS: %s\n", msg); \
         else { printf("FAIL: %s\n", msg); g_fail = 1; } } while (0)

static void reset(void)
{
    g_calls = 0;
    for (int i = 0; i < 4; i++) g_last[i][0] = 0;
}

int main(void)
{
    int ok = 0, fail = 0;

    /* ---- JSON：标准数组 ---- */
    reset(); ok = fail = 0;
    import_parse_json_text(
        "[{\"date\":\"2026-09-21\",\"time\":\"10:30\",\"title\":\"例会\",\"desc\":\"3F\"},"
        "{\"date\":\"2026/10/1\",\"title\":\"国庆\"}]", &ok, &fail);
    CHECK(ok == 2 && fail == 0, "json 标准数组导入 2 条");
    CHECK(strstr(g_last[0], "2026-09-21|10:30|例会|3F") != NULL, "json 第1条字段正确");
    CHECK(strstr(g_last[1], "2026-10-01||国庆|") != NULL, "json 缺时间=全天（空时刻）");

    /* ---- JSON：events 包裹 + start 拆分 + 别名 ---- */
    reset(); ok = fail = 0;
    import_parse_json_text(
        "{\"events\":[{\"start\":\"2026-12-25T08:00\",\"summary\":\"圣诞\",\"description\":\"礼物\"},"
        "{\"date\":\"bad\"}]}", &ok, &fail);
    CHECK(ok == 1 && fail == 1, "json events 包裹+start 拆分，坏日期计入 fail");
    CHECK(strstr(g_last[0], "2026-12-25|08:00|圣诞|礼物") != NULL, "json ISO start 拆分正确");

    /* ---- ICS：定时事件 + 全天事件 + 折叠行 + 转义 ---- */
    reset(); ok = fail = 0;
    const char *ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART:20260921T093000\r\n"
        "SUMMARY:晨会\r\n"
        "DESCRIPTION:议题一\\,议题二\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20261001\r\n"
        "SUMMARY:很长的标题被折\r\n"
        " 叠到下一行\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    import_parse_ics_text(ics, &ok, &fail);
    CHECK(ok == 2 && fail == 0, "ics 导入 2 个 VEVENT");
    CHECK(strstr(g_last[0], "2026-09-21|09:30|晨会|议题一,议题二") != NULL,
          "ics 定时事件+转义逗号");
    CHECK(strstr(g_last[1], "2026-10-01||很长的标题被折叠到下一行|") != NULL,
          "ics VALUE=DATE 全天事件（空时刻）+ 折叠行拼接");

    /* ---- CSV：中文表头 ---- */
    reset(); ok = fail = 0;
    const char *csv1 =
        "日期,时间,标题,备注\r\n"
        "2026-09-21,14:00,评审,会议室A\r\n"
        "2026.10.5,9:00,出差,\r\n";
    import_parse_csv_text(csv1, &ok, &fail);
    CHECK(ok == 2 && fail == 0, "csv 中文表头导入 2 条");
    CHECK(strstr(g_last[0], "2026-09-21|14:00|评审|会议室A") != NULL, "csv 第1条");
    CHECK(strstr(g_last[1], "2026-10-05|09:00|出差|") != NULL, "csv 点号日期+空备注");

    /* ---- CSV：无表头列序 + 引号内逗号 + 空行 ---- */
    reset(); ok = fail = 0;
    const char *csv2 =
        "2026/9/1,9:00,\"开会,讨论\",无引号备注\n"
        "\n"
        "20260925,,周末活动,\n";
    import_parse_csv_text(csv2, &ok, &fail);
    CHECK(ok == 2 && fail == 0, "csv 无表头/引号逗号/空行/8位日期");
    CHECK(strstr(g_last[0], "2026-09-01|09:00|开会,讨论|无引号备注") != NULL,
          "csv 引号包裹逗号字段");
    CHECK(strstr(g_last[1], "2026-09-25||周末活动|") != NULL,
          "csv YYYYMMDD + 空时间=全天");

    /* ---- CSV：英文表头乱序 + 无效行 ---- */
    reset(); ok = fail = 0;
    const char *csv3 =
        "Subject,Start Date,Start Time,Description\r\n"
        "牙医,2026-11-11,16:45,别迟到\r\n"
        "无日期行,,,,\r\n";
    import_parse_csv_text(csv3, &ok, &fail);
    CHECK(ok == 1 && fail == 1, "csv 英文表头乱序识别，无效行 fail");
    CHECK(strstr(g_last[0], "2026-11-11|16:45|牙医|别迟到") != NULL,
          "csv 英文别名 Subject/Start Date");

    printf(g_fail ? "\nSOME TESTS FAILED\n" : "\nALL PASS\n");
    return g_fail;
}
