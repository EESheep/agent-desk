#include "panel_model.h"
#include <string.h>

const char *panel_state_label(panel_state_t state)
{
    switch (state) {
    case PANEL_IDLE: return "IDLE";
    case PANEL_RUNNING: return "RUNNING";
    case PANEL_WAITING_INPUT: return "WAITING INPUT";
    case PANEL_COMPLETED: return "COMPLETED";
    case PANEL_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

bool panel_task_needs_attention(const panel_task_t *task)
{
    return task && (task->state == PANEL_WAITING_INPUT || task->state == PANEL_COMPLETED ||
                    task->state == PANEL_FAILED);
}

size_t panel_approval_count(const panel_snapshot_t *snapshot)
{
    if (!snapshot) return 0;
    size_t count = 0;
    for (size_t i = 0; i < snapshot->task_count && i < PANEL_MAX_TASKS; i++) {
        if (snapshot->tasks[i].needs_approval) count++;
    }
    return count;
}

void panel_demo_snapshot(panel_snapshot_t *snapshot, unsigned phase)
{
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->demo = true;
    /* Demo mode must not claim a connection to Codex. */
    snapshot->connected = false;
    snapshot->task_count = 4;
    snapshot->tasks[0] = (panel_task_t){
        .id = "demo-lvgl", .title = "LVGL display development",
        .detail = "Building the device dashboard", .state = PANEL_RUNNING,
        .approval_known = true};
    snapshot->tasks[1] = (panel_task_t){
        .id = "demo-bridge", .title = "Codex status bridge",
        .detail = "Example: permission required on the computer",
        .state = PANEL_RUNNING, .approval_known = true, .needs_approval = true};
    snapshot->tasks[2] = (panel_task_t){
        .id = "demo-review", .title = "Review changes",
        .detail = "Example: waiting for your input", .state = PANEL_WAITING_INPUT,
        .approval_known = true};
    snapshot->tasks[3] = (panel_task_t){
        .id = "demo-docs", .title = "Device documentation",
        .detail = "Example task finished", .state = PANEL_COMPLETED,
        .approval_known = true};
    if (phase % 2) {
        snapshot->tasks[1].needs_approval = false;
        snapshot->tasks[1].state = PANEL_COMPLETED;
        strcpy(snapshot->tasks[1].detail, "Simulated approval resolved (no real action)");
    }
    snapshot->card_count = 4;
    snapshot->cards[0] = (panel_card_t){
        .title = "DATA SOURCE", .value = "DEMO / LOCAL",
        .detail = "No Codex account connected"};
    snapshot->cards[1] = (panel_card_t){
        .title = "DISPLAY", .value = "800 x 480",
        .detail = "RGB565 / GT911 touch"};
    snapshot->cards[2] = (panel_card_t){
        .title = "NETWORK", .value = "NOT CONFIGURED",
        .detail = "Wi-Fi will be added in stage 2"};
    snapshot->cards[3] = (panel_card_t){
        .title = "EXTENSION SLOT", .value = "YOUR CONTENT",
        .detail = "Later: clock, build, server or sensor"};
}
