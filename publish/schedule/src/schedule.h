/* schedule.h — 事件模型、CRUD 与循环展开 */
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "util.h"

typedef enum {
    REC_NONE = 0, REC_DAILY, REC_WEEKLY, REC_MONTHLY, REC_YEARLY, REC_CUSTOM
} RecKind;

typedef struct Event {
    char    id[40];
    wchar_t title[128];
    wchar_t note[512];
    char    start[20];       /* 定时："YYYY-MM-DDTHH:MM"；全天："YYYY-MM-DD" */
    char    end[20];         /* 同上；全天时结束日期为含当天的最后一天 */
    int     allDay;          /* 1 = 全天日程（无时刻，最细到天） */
    int     colorIdx;        /* 调色板 0..7 */
    int     remindMinutes;   /* <0 不提醒；0 准点（全天为当天 9:00）；>0 提前 N 分钟 */
    RecKind rec;
    int     interval;        /* REC_CUSTOM 间隔数 */
    TimeUnit unit;           /* REC_CUSTOM 单位 */
    char    until[20];       /* 循环截止（发生起始时间上限），空 = 永久 */
    long long lastNotified;  /* 已提醒的 occurrence 起始秒（去重） */
} Event;

typedef struct Occ { t64 s, e; int evIdx; } Occ;

/* 事件数组管理（变更后自动落盘并通知 UI 刷新） */
void   schedule_init(void);
void   schedule_add(const Event *ev);
void   schedule_add_nosave(const Event *ev);  /* 批量载入用，不落盘 */
void   schedule_update(const Event *ev);   /* 按 id 覆盖 */
int    schedule_delete(const char *id);    /* 返回 1 表示删除 */
void   schedule_clear(void);               /* 清除所有日程并落盘 */
int    schedule_purge_expired(void);       /* 自动删除已过期非循环日程，返回删除条数 */
Event *schedule_find(const char *id);
Event *schedule_at(int idx);
int    schedule_count(void);

/* 导入便捷函数：date="YYYY-MM-DD"、time="HH:MM"（time 为 NULL/空串时
 * 生成全天日程）。默认时长 1 小时、不提醒。
 * title/desc 为 UTF-8 字符串（可为 NULL）。返回 1 成功，0 失败。 */
int    schedule_add_raw(const char *date, const char *time,
                        const char *title, const char *desc);

t64  ev_start_t(const Event *ev);
t64  ev_end_t(const Event *ev);

/* 循环展开：枚举 [ws, we] 内的发生（按开始时间排序），返回条数 */
int  schedule_occurrences(t64 ws, t64 we, Occ *out, int cap);

/* 指定时刻之后第一个发生（用于 pill 倒计时），无则返回 0 */
int  schedule_next_upcoming(t64 after, Occ *out);

/* AI 导入协议中的 rec/unit 字符串映射 */
const char *rec_to_str(RecKind r);
RecKind     rec_from_str(const char *s);
const char *unit_to_str(TimeUnit u);
TimeUnit    unit_from_str(const char *s, TimeUnit def);

#endif
