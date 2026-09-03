#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PANEL_MAX_TASKS 8
#define PANEL_MAX_CARDS 4

typedef enum {
    PANEL_UNKNOWN, PANEL_IDLE, PANEL_RUNNING, PANEL_WAITING_INPUT,
    PANEL_COMPLETED, PANEL_FAILED
} panel_state_t;

typedef struct {
    char id[64];
    char title[96];
    char detail[128];
    panel_state_t state;
    bool approval_known;
    bool needs_approval; /* Independent of running/idle state. */
} panel_task_t;

typedef struct {
    char title[32];
    char value[48];
    char detail[96];
} panel_card_t;

typedef struct {
    bool demo;
    bool connected;
    uint32_t age_seconds; /* Time since last real snapshot; NOT wall clock. */
    size_t task_count;
    panel_task_t tasks[PANEL_MAX_TASKS];
    size_t card_count;
    panel_card_t cards[PANEL_MAX_CARDS];
} panel_snapshot_t;

const char *panel_state_label(panel_state_t state);
bool panel_task_needs_attention(const panel_task_t *task);
size_t panel_approval_count(const panel_snapshot_t *snapshot);
void panel_demo_snapshot(panel_snapshot_t *snapshot, unsigned phase);
