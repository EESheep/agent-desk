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
    assert(panel_task_needs_attention(&s.tasks[1]));
    assert(panel_task_needs_attention(&s.tasks[2]));
    assert(!panel_task_needs_attention(&s.tasks[3]));
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
    puts("panel_model tests passed");
    return 0;
}

