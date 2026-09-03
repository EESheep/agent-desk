#include "panel_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    panel_snapshot_t s;
    panel_demo_snapshot(&s, 0);
    assert(s.demo && !s.connected);
    assert(s.task_count == 4 && s.card_count == 4);
    assert(panel_approval_count(&s) == 1);
    assert(!panel_task_needs_attention(&s.tasks[1]));
    assert(panel_task_needs_attention(&s.tasks[2]));
    assert(panel_task_needs_attention(&s.tasks[3]));
    panel_demo_snapshot(&s, 1);
    assert(panel_approval_count(&s) == 0);
    assert(s.tasks[1].state == PANEL_COMPLETED);
    assert(!panel_task_needs_attention(NULL));
    assert(panel_approval_count(NULL) == 0);
    assert(strcmp(panel_state_label((panel_state_t)999), "UNKNOWN") == 0);
    /* Invalid count is bounded by the fixed array. */
    s.task_count = PANEL_MAX_TASKS + 100;
    assert(panel_approval_count(&s) == 0);
    s.tasks[0].state = PANEL_FAILED;
    assert(panel_task_needs_attention(&s.tasks[0]));
    memset(&s, 0, sizeof(s));
    s.connected = true;
    s.task_count = 2;
    s.tasks[0] = (panel_task_t){.id = "same-id", .source = PANEL_CODEX, .state = PANEL_RUNNING};
    s.tasks[1] = (panel_task_t){.id = "same-id", .source = PANEL_KIMI, .state = PANEL_COMPLETED};
    assert(panel_task_matches(&s.tasks[0], PANEL_SOURCE_COUNT));
    assert(panel_task_matches(&s.tasks[1], PANEL_KIMI));
    assert(!panel_task_matches(&s.tasks[1], PANEL_CODEX));
    assert(!panel_task_matches(NULL, PANEL_SOURCE_COUNT));
    assert(panel_find_task(&s, PANEL_CODEX, "same-id") == &s.tasks[0]);
    assert(panel_find_task(&s, PANEL_KIMI, "same-id") == &s.tasks[1]);
    assert(!panel_find_task(&s, PANEL_DSH, "same-id"));
    assert(!panel_find_task(NULL, PANEL_CODEX, "same-id"));
    assert(!panel_find_task(&s, PANEL_CODEX, ""));
    assert(panel_completion(&s) == &s.tasks[1]);
    s.tasks[1].state = PANEL_IDLE;
    assert(!panel_completion(&s) && !panel_completion(NULL));
    assert(panel_source_available(&s, PANEL_CODEX));
    assert(!panel_source_available(&s, PANEL_DSH));
    s.card_count = 1;
    s.cards[0].source = PANEL_DSH;
    assert(panel_source_available(&s, PANEL_DSH));
    s.connected = false;
    assert(!panel_source_available(&s, PANEL_DSH));
    panel_source_t source = PANEL_CODEX;
    assert(panel_parse_source("kimi", &source) && source == PANEL_KIMI);
    assert(panel_parse_source("dsh", &source) && source == PANEL_DSH);
    assert(!panel_parse_source("other", &source));
    assert(!panel_parse_source(NULL, &source));
    assert(!strcmp(panel_source_label(PANEL_CODEX), "Codex"));
    puts("panel_model source/filter/notice tests passed");
    return 0;
}
