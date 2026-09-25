/* schedule_import.h — 标准日程文件本地直解析（不经过 AI、不联网）
 *
 * 支持格式：
 *   .ics  iCalendar（VEVENT：DTSTART 全天/本地时间、SUMMARY、DESCRIPTION）
 *   .csv  表头自动识别（日期/时间/标题/备注 中英别名）或按列序 date,time,title,desc
 *   .xlsx 经 text_extract 转成 CSV 文本后走同一 CSV 解析
 *   .json [{ "date":"YYYY-MM-DD", "time":"HH:MM", "title":"…", "desc":"…" }]
 *
 * 日期/时间容忍多种写法（- / . 分隔、YYYYMMDD、中文「年月日」、缺省时间=09:00）。 */
#ifndef SCHEDULE_IMPORT_H
#define SCHEDULE_IMPORT_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 直接解析并导入文件。ok、fail 分别统计成功与跳过条数。
 * 返回 1：格式受支持（即使 0 条）；返回 0：不支持的扩展名或结构错误（errMsg 写原因）。 */
int schedule_import_file(const wchar_t *path, int *ok, int *fail,
                         wchar_t *errMsg, int errCap);

/* —— 文本入口（便于测试）：输入为 UTF-8 文本 —— */
int import_parse_ics_text(const char *s, int *ok, int *fail);
int import_parse_csv_text(const char *s, int *ok, int *fail);
int import_parse_json_text(const char *s, int *ok, int *fail);

#ifdef __cplusplus
}
#endif

#endif
