/* test_purge.c — 验证 schedule_purge_expired 自动过期清理：
 * 已结束的定时日程、跨日的全天日程被删；当天全天、进行中/未来定时、
 * 多日全天（结束日含当天）、循环日程一律保留。 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
App g;
void events_save(void) {}   /* 不落盘 schedule.json */

#include "../src/schedule.c"

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS: %s\n", msg); \
         else { printf("FAIL: %s\n", msg); g_fail = 1; } } while (0)

/* 构造一条事件；start/end 为完整 ISO 串（定时带 T、全天纯日期） */
static void add_ev(const char *start, const char *end, int allDay,
                   RecKind rec, const wchar_t *title)
{
    Event ev;
    memset(&ev, 0, sizeof(ev));
    gen_id(ev.id, sizeof(ev.id));
    strcpy(ev.start, start);
    strcpy(ev.end, end);
    ev.allDay = allDay;
    ev.rec = rec;
    ev.interval = 1;
    ev.unit = U_DAY;
    wcscpy(ev.title, title);
    schedule_add_nosave(&ev);
}

static int title_exists(const wchar_t *t)
{
    for (int i = 0; i < schedule_count(); i++)
        if (wcscmp(schedule_at(i)->title, t) == 0) return 1;
    return 0;
}

int main(void)
{
    schedule_init();

    t64 now = tnow();
    t64 today0 = day_start(now), yest0 = today0 - 86400;
    char dY[11], dT[11];
    iso_date_from_t64(yest0, dY, sizeof(dY));
    iso_date_from_t64(today0, dT, sizeof(dT));

    char s[20], e[20];

    /* 1. 昨天的全天（start==end 昨天）→ 应删 */
    add_ev(dY, dY, 1, REC_NONE, L"昨天全天");

    /* 2. 今天全天 → 保留（结束日含当天） */
    add_ev(dT, dT, 1, REC_NONE, L"今天全天");

    /* 3. 昨天起、今天止的多日全天 → 保留 */
    add_ev(dY, dT, 1, REC_NONE, L"跨天全天");

    /* 4. 昨天 09:00–10:00 定时 → 应删 */
    snprintf(s, 20, "%sT09:00", dY);
    snprintf(e, 20, "%sT10:00", dY);
    add_ev(s, e, 0, REC_NONE, L"昨天定时");

    /* 5. 1 小时前开始、半小时前结束的定时 → 应删（相对真实时钟，稳定） */
    iso_from_t64(now - 3600, s, sizeof(s));
    iso_from_t64(now - 1800, e, sizeof(e));
    add_ev(s, e, 0, REC_NONE, L"刚结束定时");

    /* 6. 1 小时后开始、2 小时后结束的定时 → 保留 */
    iso_from_t64(now + 3600, s, sizeof(s));
    iso_from_t64(now + 7200, e, sizeof(e));
    add_ev(s, e, 0, REC_NONE, L"未来定时");

    /* 7. 昨天开始的「每天」循环 → 即使末次发生在过去也永不自动删 */
    snprintf(s, 20, "%sT09:00", dY);
    snprintf(e, 20, "%sT10:00", dY);
    add_ev(s, e, 0, REC_DAILY, L"每日循环");

    CHECK(schedule_count() == 7, "清理前共 7 条");
    int removed = schedule_purge_expired();
    CHECK(removed == 3, "返回删除 3 条（昨天全天/昨天定时/刚结束定时）");
    CHECK(schedule_count() == 4, "清理后剩 4 条");

    CHECK(!title_exists(L"昨天全天"), "昨天全天已删除");
    CHECK(!title_exists(L"昨天定时"), "昨天定时已删除");
    CHECK(!title_exists(L"刚结束定时"), "刚结束的定时已删除");
    CHECK(title_exists(L"今天全天"), "今天全天保留");
    CHECK(title_exists(L"跨天全天"), "结束日为今天的多日全天保留");
    CHECK(title_exists(L"未来定时"), "未来定时保留");
    CHECK(title_exists(L"每日循环"), "循环日程保留");

    /* 幂等：再清一次不应再删，也不应落盘（返回 0） */
    CHECK(schedule_purge_expired() == 0, "再次清理无删除（幂等）");
    CHECK(schedule_count() == 4, "数量保持 4 条");

    printf(g_fail ? "\nSOME TESTS FAILED\n" : "\nALL PASS\n");
    return g_fail;
}
