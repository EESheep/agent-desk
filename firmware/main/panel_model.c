#include "panel_model.h"
#include <string.h>

const char *panel_source_label(panel_source_t source)
{
    switch (source) {
    case PANEL_CODEX: return "Codex";
    case PANEL_KIMI: return "Kimi";
    case PANEL_DSH: return "DSH";
    default: return "All";
    }
}

bool panel_parse_source(const char *name, panel_source_t *source)
{
    if (!name || !source) return false;
    const char *names[] = {"codex", "kimi", "dsh"};
    for (unsigned i = 0; i < PANEL_SOURCE_COUNT; i++) {
        if (!strcmp(name, names[i])) {
            *source = (panel_source_t)i;
            return true;
        }
    }
    return false;
}

bool panel_task_matches(const panel_task_t *task, panel_source_t filter)
{
    return task && (unsigned)task->source < PANEL_SOURCE_COUNT &&
        (filter == PANEL_SOURCE_COUNT || task->source == filter);
}

const panel_task_t *panel_find_task(const panel_snapshot_t *snapshot, panel_source_t source, const char *id)
{
    if (!snapshot || !id || !id[0]) return NULL;
    for (size_t i = 0; i < snapshot->task_count && i < PANEL_MAX_TASKS; i++) {
        if (snapshot->tasks[i].source == source && !strcmp(snapshot->tasks[i].id, id))
            return &snapshot->tasks[i];
    }
    return NULL;
}

const panel_task_t *panel_completion(const panel_snapshot_t *snapshot)
{
    if (!snapshot) return NULL;
    for (size_t i = 0; i < snapshot->task_count && i < PANEL_MAX_TASKS; i++) {
        if (snapshot->tasks[i].state == PANEL_COMPLETED) return &snapshot->tasks[i];
    }
    return NULL;
}

bool panel_source_available(const panel_snapshot_t *snapshot, panel_source_t source)
{
    if (!snapshot || !snapshot->connected || (unsigned)source >= PANEL_SOURCE_COUNT) return false;
    for (size_t i = 0; i < snapshot->task_count && i < PANEL_MAX_TASKS; i++) {
        if (snapshot->tasks[i].source == source) return true;
    }
    for (size_t i = 0; i < snapshot->card_count && i < PANEL_MAX_CARDS; i++) {
        if (snapshot->cards[i].source == source) return true;
    }
    return false;
}

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
