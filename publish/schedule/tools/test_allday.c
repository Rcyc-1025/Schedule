/* test_allday.c — 验证全天日程端到端语义：
 * schedule_add_raw 空时刻 → allDay/纯日期存储 → 当日窗口命中 →
 * next_upcoming 把跨今天的全天事件判为进行中。 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
App g;                 /* 链接占位，本测试不碰全局状态 */
void events_save(void) {}   /* 不落盘 schedule.json */

#include "../src/schedule.c"

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS: %s\n", msg); \
         else { printf("FAIL: %s\n", msg); g_fail = 1; } } while (0)

int main(void)
{
    schedule_init();

    /* 空时刻 → 全天 */
    CHECK(schedule_add_raw("2026-09-21", NULL, "全天A", "备注") == 1,
          "空时刻导入全天日程成功");
    Event *a = schedule_at(0);
    CHECK(a && a->allDay == 1, "全天标记置位");
    CHECK(a && strcmp(a->start, "2026-09-21") == 0,
          "start 为纯日期（无 T 时刻）");
    CHECK(a && strcmp(a->end, "2026-09-21") == 0,
          "end 为纯日期（单天）");

    /* 显式时刻 → 定时 */
    CHECK(schedule_add_raw("2026-09-22", "14:00", "定时B", NULL) == 1,
          "定时日程导入成功");
    Event *b = schedule_at(1);
    CHECK(b && b->allDay == 0 && strcmp(b->start, "2026-09-22T14:00") == 0,
          "定时日程带 T 时刻");

    /* 全天事件在当日整天窗口内被枚举到 */
    t64 d0 = tmk(2026, 9, 21, 0, 0);
    Occ occs[16];
    int n = schedule_occurrences(d0, d0 + 86399, occs, 16);
    CHECK(n == 1 && occs[0].evIdx == 0, "全天事件命中当日窗口");

    /* next_upcoming：今天的全天事件优先判为「进行中」 */
    Occ up;
    int has = schedule_next_upcoming(tmk(2026, 9, 21, 15, 0), &up);
    CHECK(has && up.evIdx == 0, "今天的全天事件被选为进行中");

    /* 次日查询：全天已过，应选未来的定时事件 */
    has = schedule_next_upcoming(tmk(2026, 9, 22, 9, 0), &up);
    CHECK(has && up.evIdx == 1, "次日选中未来的定时事件");

    /* 多日全天（end 含当天）：25 ~ 27 */
    Event c;
    memset(&c, 0, sizeof(c));
    gen_id(c.id, sizeof(c.id));
    c.allDay = 1;
    strcpy(c.start, "2026-09-25");
    strcpy(c.end, "2026-09-27");
    wcscpy(c.title, L"多日全天");
    schedule_add_nosave(&c);

    for (int dd = 24; dd <= 28; dd++) {
        t64 w0 = tmk(2026, 9, dd, 0, 0);
        int hit = schedule_occurrences(w0, w0 + 86399, occs, 16);
        int expect = (dd >= 25 && dd <= 27);
        char msg[64];
        _snprintf(msg, 64, "多日全天 9/%d %s", dd, expect ? "应命中" : "不应命中");
        CHECK((hit == 1) == expect, msg);
    }
    /* 最后一天（27 日）仍判进行中；28 日已结束且无其他未来事件 */
    CHECK(schedule_next_upcoming(tmk(2026, 9, 27, 10, 0), &up) && up.evIdx == 2,
          "多日全天最后一天仍为进行中");
    CHECK(!schedule_next_upcoming(tmk(2026, 9, 28, 10, 0), &up),
          "多日全天结束次日不再出现");

    printf(g_fail ? "\nSOME TESTS FAILED\n" : "\nALL PASS\n");
    return g_fail;
}
