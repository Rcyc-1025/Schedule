/* schedule.c — 事件数组管理与循环展开 */
#include "app.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Event *s_events;
static int    s_count, s_cap;

/* ================= 数组管理 ================= */

void schedule_init(void)
{
    s_events = NULL; s_count = s_cap = 0;
}

int schedule_count(void) { return s_count; }
Event *schedule_at(int idx) { return (idx >= 0 && idx < s_count) ? &s_events[idx] : NULL; }

Event *schedule_find(const char *id)
{
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_events[i].id, id) == 0) return &s_events[i];
    return NULL;
}

void schedule_add(const Event *ev)
{
    if (s_count == s_cap) {
        s_cap = s_cap ? s_cap * 2 : 16;
        Event *ne = (Event *)realloc(s_events, (size_t)s_cap * sizeof(Event));
        if (!ne) return;
        s_events = ne;
    }
    s_events[s_count++] = *ev;
    events_save();
}

void schedule_update(const Event *ev)
{
    Event *old = schedule_find(ev->id);
    if (!old) { schedule_add(ev); return; }
    *old = *ev;
    events_save();
}

int schedule_delete(const char *id)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_events[i].id, id) == 0) {
            memmove(&s_events[i], &s_events[i + 1],
                    (size_t)(s_count - i - 1) * sizeof(Event));
            s_count--;
            events_save();
            return 1;
        }
    }
    return 0;
}

void schedule_clear(void)
{
    s_count = 0;
    events_save();
}

/* 自动清理已过期日程，返回删除条数（有删除时落盘一次）。
 *  - 定时日程：结束时刻已过即过期；
 *  - 全天日程：end 为含当天的结束日 00:00，今天 00:00 超过它（即次日起）才过期；
 *  - 循环日程（rec != REC_NONE）会不断产生新发生，永不自动删除。 */
int schedule_purge_expired(void)
{
    t64 now = tnow();
    t64 today0 = day_start(now);
    int removed = 0;
    int i = 0;
    while (i < s_count) {
        Event *ev = &s_events[i];
        int expired = 0;
        if (ev->rec == REC_NONE) {
            t64 e = ev_end_t(ev);
            expired = ev->allDay ? (today0 > e)   /* 结束日当天仍保留 */
                                 : (e < now);
        }
        if (expired) {
            memmove(&s_events[i], &s_events[i + 1],
                    (size_t)(s_count - i - 1) * sizeof(Event));
            s_count--;
            removed++;
        } else {
            i++;
        }
    }
    if (removed) events_save();
    return removed;
}

/* ================= 时间存取 ================= */

t64 ev_start_t(const Event *ev) { return iso_to_t64(ev->start); }
/* 全天事件（allDay）的 end 是「含当天」的日期，返回其 00:00；
 * 所有消费方（occurrences 包含判定、日历标记 d<=d1、next_upcoming、
 * fmt_range、编辑器结束日期）一律按含当天语义处理，勿在此 +86400。 */
t64 ev_end_t(const Event *ev)
{
    t64 e = iso_to_t64(ev->end);
    t64 s = iso_to_t64(ev->start);
    if (e < s) e = s;
    return e;
}

/* ================= rec/unit 字符串映射 ================= */

const char *rec_to_str(RecKind r)
{
    switch (r) {
    case REC_DAILY:   return "daily";
    case REC_WEEKLY:  return "weekly";
    case REC_MONTHLY: return "monthly";
    case REC_YEARLY:  return "yearly";
    case REC_CUSTOM:  return "custom";
    default:          return "none";
    }
}

RecKind rec_from_str(const char *s)
{
    if (!s) return REC_NONE;
    if (!strcmp(s, "daily"))   return REC_DAILY;
    if (!strcmp(s, "weekly"))  return REC_WEEKLY;
    if (!strcmp(s, "monthly")) return REC_MONTHLY;
    if (!strcmp(s, "yearly"))  return REC_YEARLY;
    if (!strcmp(s, "custom"))  return REC_CUSTOM;
    return REC_NONE;
}

const char *unit_to_str(TimeUnit u)
{
    switch (u) {
    case U_MIN:   return "minute";
    case U_HOUR:  return "hour";
    case U_DAY:   return "day";
    case U_WEEK:  return "week";
    case U_MONTH: return "month";
    case U_YEAR:  return "year";
    }
    return "day";
}

TimeUnit unit_from_str(const char *s, TimeUnit def)
{
    if (!s) return def;
    if (!strcmp(s, "minute")) return U_MIN;
    if (!strcmp(s, "hour"))   return U_HOUR;
    if (!strcmp(s, "day"))    return U_DAY;
    if (!strcmp(s, "week"))   return U_WEEK;
    if (!strcmp(s, "month"))  return U_MONTH;
    if (!strcmp(s, "year"))   return U_YEAR;
    return def;
}

/* ================= 循环展开 ================= */

static int occ_cmp(const void *a, const void *b)
{
    const Occ *x = (const Occ *)a, *y = (const Occ *)b;
    if (x->s != y->s) return x->s < y->s ? -1 : 1;
    return x->evIdx - y->evIdx;
}

int schedule_occurrences(t64 ws, t64 we, Occ *out, int cap)
{
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        Event *ev = &s_events[i];
        t64 s = ev_start_t(ev);
        t64 e = ev_end_t(ev);
        if (s == 0) continue;

        if (ev->rec == REC_NONE) {
            if (e >= ws && s <= we && n < cap) {
                out[n].s = s; out[n].e = e; out[n].evIdx = i; n++;
            }
            continue;
        }

        TimeUnit u = ev->unit;
        if      (ev->rec == REC_DAILY)   u = U_DAY;
        else if (ev->rec == REC_WEEKLY)  u = U_WEEK;
        else if (ev->rec == REC_MONTHLY) u = U_MONTH;
        else if (ev->rec == REC_YEARLY)  u = U_YEAR;
        long long interval = (ev->rec == REC_CUSTOM) ? (ev->interval > 0 ? ev->interval : 1) : 1;

        t64 un = (ev->until[0]) ? iso_to_t64(ev->until) : 0;

        /* 估算首个可能命中窗口的 k，避免分钟级循环从数年前逐个迭代 */
        long long k = 0;
        long long step = 0;
        switch (u) {
        case U_MIN:  step = 60;     break;
        case U_HOUR: step = 3600;   break;
        case U_DAY:  step = 86400;  break;
        case U_WEEK: step = 604800; break;
        default:     step = 0;      break;
        }
        if (step) {
            long long sp = step * interval;
            k = (ws - s + sp - 1) / sp;
            if (k < 0) k = 0;
        } else {
            int y1, m1, d1, hh, mm;
            tbreak(s, &y1, &m1, &d1, &hh, &mm);
            int y2, m2, d2;
            tbreak(ws, &y2, &m2, &d2, NULL, NULL);
            long long md = (long long)(y2 - y1) * 12 + (m2 - m1);
            long long mult = (u == U_MONTH) ? 1 : 12;
            k = md / (mult * interval) - 1;
            if (k < 0) k = 0;
        }

        long long iter = 0;
        for (; k < 4000000 && iter < 100000; k++, iter++) {
            t64 os = tadd_unit(s, k, u);
            if (os <= s && k > 0) break;          /* 越过 9999 年保护 */
            if (un > 0 && os > un) break;
            if (os > we) break;
            t64 oe = tadd_unit(e, k, u);
            if (oe >= ws && os <= we && n < cap) {
                out[n].s = os; out[n].e = oe; out[n].evIdx = i; n++;
            }
        }
    }
    if (n > 1) qsort(out, (size_t)n, sizeof(Occ), occ_cmp);
    return n;
}

int schedule_next_upcoming(t64 after, Occ *out)
{
    /* 窗口下限取当天 00:00，使「跨今天的全天日程」能被选为进行中。
     * 窗口只取 31 天：配合足够大的缓冲，避免每日/每周循环事件在 366 天
     * 长窗口中耗尽容量（旧实现 cap=32 时，排在数组后面的新日程会被前面
     * 的每日循环挤出，导致 pill 永远选不到它）。 */
    t64 today0 = day_start(after);
    Occ tmp[256];
    int n = schedule_occurrences(today0, after + 31LL * 86400, tmp, 256);
    if (n <= 0) return 0;
    int cur = -1, fut = -1;
    for (int i = 0; i < n; i++) {
        Event *ev = schedule_at(tmp[i].evIdx);
        int active;
        if (ev && ev->allDay)
            active = today0 >= day_start(tmp[i].s) &&
                     today0 <= day_start(tmp[i].e);   /* end 日期含当天 */
        else
            active = tmp[i].s <= after && tmp[i].e > after;
        if (active) {
            /* 多个进行中时取开始最晚者 */
            if (cur < 0 || tmp[i].s > tmp[cur].s) cur = i;
        } else if (tmp[i].s > after) {
            if (fut < 0 || tmp[i].s < tmp[fut].s) fut = i;
        }
    }
    int best = cur >= 0 ? cur : fut;
    if (best < 0) return 0;
    *out = tmp[best];
    return 1;
}

void schedule_add_nosave(const Event *ev)
{
    if (s_count == s_cap) {
        s_cap = s_cap ? s_cap * 2 : 16;
        Event *ne = (Event *)realloc(s_events, (size_t)s_cap * sizeof(Event));
        if (!ne) return;
        s_events = ne;
    }
    s_events[s_count++] = *ev;
}

/* 导入便捷入口：time 为空时生成全天日程（start/end 只存日期）；
 * 否则按给定时刻生成、默认 1 小时长。自动落盘。 */
int schedule_add_raw(const char *date, const char *time,
                     const char *title, const char *desc)
{
    if (!date) return 0;
    int y, m, d;
    if (iso_parse(date, &y, &m, &d, NULL, NULL) != 0) return 0;
    t64 day = tmk(y, m, d, 0, 0);
    if (day == 0) return 0;

    int allDay = (!time || !time[0]);
    int hh = 0, mm = 0;
    if (!allDay) {
        /* time 为裸时刻 "HH:MM"（iso_parse 只收完整日期，不能直接用） */
        if (sscanf(time, "%d:%d", &hh, &mm) != 2 ||
            (unsigned)hh > 23 || (unsigned)mm > 59) return 0;
    }
    t64 st = allDay ? day : tmk(y, m, d, hh, mm);
    if (st == 0) return 0;

    Event ev;
    memset(&ev, 0, sizeof(ev));
    gen_id(ev.id, sizeof(ev.id));
    ev.allDay = allDay;
    if (allDay) {
        iso_date_from_t64(st, ev.start, sizeof(ev.start));
        iso_date_from_t64(st, ev.end, sizeof(ev.end));
    } else {
        iso_from_t64(st, ev.start, sizeof(ev.start));
        iso_from_t64(tadd_unit(st, 1, U_HOUR), ev.end, sizeof(ev.end));
    }
    ev.colorIdx = 0;
    ev.remindMinutes = -1;   /* 导入默认不提醒 */
    ev.rec = REC_NONE;
    ev.interval = 1;
    ev.unit = U_DAY;
    if (title) u8_to_w16(title, ev.title, 128);
    if (desc)  u8_to_w16(desc, ev.note, 512);
    schedule_add(&ev);
    return 1;
}
