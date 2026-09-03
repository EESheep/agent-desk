#include "panel_ui.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BG 0x0C1422
#define SURFACE 0x172336
#define FG 0xEDF3FA
#define MUTED 0x99A9C0
#define ACCENT 0x62DDD0
#define DONE 0x71E68C
#define WARN 0xFFC16C

static panel_snapshot_t current;
static lv_obj_t *content, *summary, *source, *footer;
static lv_obj_t *tabs[3];
static unsigned page;
/* Keep identity, not row position: approval sorting and snapshots can reorder rows. */
static char selected_id[sizeof(((panel_task_t *)0)->id)];
static int32_t list_scroll[3];
static lv_obj_t *detail_body, *detail_title, *detail_id;
static lv_obj_t *detail_state, *detail_summary, *detail_notice;

static void render(void);
static void open_session(lv_event_t *event);
static void back_to_list(lv_event_t *event);

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    /* Decorative children must not intercept a card's touch target. */
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *paragraph(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = label(parent, "", 0, 0, 712, font, color);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    return obj;
}

static void make_clickable(lv_obj_t *obj, lv_event_cb_t callback, void *data)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x2B4857), LV_STATE_PRESSED);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
}

static void render_detail(void)
{
    if (!detail_body) {
        lv_obj_clean(content);
        lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);
        lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *back = box(content, 0, 0, 140, 46, SURFACE);
        make_clickable(back, back_to_list, NULL);
        label(back, LV_SYMBOL_LEFT "  Back", 16, 13, 112, &lv_font_montserrat_16, FG);
        label(content, "SESSION DETAILS", 160, 14, 400, &lv_font_montserrat_16, MUTED);

        /* Only the body scrolls; Back stays visible even for long summaries. */
        detail_body = box(content, 0, 56, 760, 222, SURFACE);
        lv_obj_add_flag(detail_body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_scroll_dir(detail_body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(detail_body, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_all(detail_body, 16, 0);
        lv_obj_set_style_pad_row(detail_body, 10, 0);
        lv_obj_set_flex_flow(detail_body, LV_FLEX_FLOW_COLUMN);
        detail_title = paragraph(detail_body, &lv_font_montserrat_20, FG);
        detail_id = paragraph(detail_body, &lv_font_montserrat_12, MUTED);
        detail_state = paragraph(detail_body, &lv_font_montserrat_16, ACCENT);
        detail_summary = paragraph(detail_body, &lv_font_montserrat_16, FG);
        detail_notice = paragraph(detail_body, &lv_font_montserrat_12, MUTED);
    }

    const panel_task_t *task = NULL;
    for (size_t i = 0; i < current.task_count; i++) {
        if (strcmp(current.tasks[i].id, selected_id) == 0) {
            task = &current.tasks[i];
            break;
        }
    }
    int32_t scroll = lv_obj_get_scroll_y(detail_body);
    lv_label_set_text_fmt(detail_id, "Session ID: %s", selected_id);
    if (!task) {
        lv_label_set_text(detail_title, "Session unavailable");
        lv_label_set_text(detail_state, "NOT IN LATEST SNAPSHOT");
        lv_obj_set_style_text_color(detail_state, lv_color_hex(WARN), 0);
        lv_label_set_text(detail_summary, "This session is no longer in the received list. Go back to choose another session.");
        lv_label_set_text(detail_notice, "Its status is unknown; absence does not mean it completed.");
    } else {
        lv_label_set_text(detail_title, task->title);
        lv_label_set_text(detail_state, panel_state_label(task->state));
        lv_obj_set_style_text_color(detail_state, lv_color_hex(task->state == PANEL_WAITING_INPUT ? WARN :
                                   task->state == PANEL_COMPLETED ? DONE :
                                   task->state == PANEL_FAILED ? 0xFF8190 : ACCENT), 0);
        lv_label_set_text(detail_summary, task->detail[0] ? task->detail : "No summary available.");
        const char *notice = task->state == PANEL_WAITING_INPUT ? "Waiting for your reply. Continue this session on your computer." :
            task->state == PANEL_COMPLETED ? "Recent completion detected from the local Codex rollout." :
            task->state == PANEL_FAILED ? "Task failed. Check the error details on your computer." :
            "Read-only activity reminder. Full conversation stays on your computer.";
        lv_label_set_text_fmt(detail_notice, "%s%s", current.demo ? "SIMULATED SESSION / No real action.\n" : "", notice);
    }
    /* Update existing labels instead of rebuilding the page on every snapshot. */
    lv_obj_update_layout(detail_body);
    lv_obj_scroll_to_y(detail_body, scroll, LV_ANIM_OFF);
}

static void render(void)
{
    char text[96];
    size_t running = 0, completed = 0, action = 0;
    for (size_t i = 0; i < current.task_count; i++) {
        if (current.tasks[i].state == PANEL_RUNNING) running++;
        if (current.tasks[i].state == PANEL_COMPLETED) completed++;
        if (current.tasks[i].state == PANEL_WAITING_INPUT || current.tasks[i].state == PANEL_FAILED) action++;
    }
    snprintf(text, sizeof(text), "%u sessions  /  %u running  /  %u completed  /  %u action",
             (unsigned)current.task_count, (unsigned)running, (unsigned)completed, (unsigned)action);
    lv_label_set_text(summary, text);
    lv_obj_set_style_text_color(summary, lv_color_hex(action ? WARN : completed ? DONE : MUTED), 0);
    const char *status = current.demo ? "DEMO DATA" :
        !current.connected ? "WAITING" : current.age_seconds > 15 ? "STALE DATA" : "SYNCED";
    lv_label_set_text(source, status);
    lv_obj_set_style_text_color(source,
        lv_color_hex(current.demo || !current.connected || current.age_seconds > 15 ? WARN : ACCENT), 0);
    if (current.demo) {
        snprintf(text, sizeof(text), "SIMULATION ONLY  |  No connection to Codex  |  Read-only panel");
    } else {
        snprintf(text, sizeof(text), "Local activity + Codex quota via UART: %u s ago",
                 (unsigned)current.age_seconds);
    }
    lv_label_set_text(footer, text);
    for (unsigned i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(tabs[i], lv_color_hex(page == i ? 0x2B4857 : SURFACE), 0);
        lv_obj_set_style_border_width(tabs[i], page == i ? 1 : 0, 0);
        lv_obj_set_style_border_color(tabs[i], lv_color_hex(ACCENT), 0);
    }
    if (selected_id[0]) {
        render_detail();
        return;
    }
    lv_obj_clean(content);
    detail_body = NULL;
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);
    if (page == 2) {
        for (size_t i = 0; i < current.card_count; i++) {
            const panel_card_t *card = &current.cards[i];
            lv_obj_t *tile = box(content, (i % 2) * 386, (i / 2) * 138, 374, 126, SURFACE);
            label(tile, card->title, 16, 14, 340, &lv_font_montserrat_12, MUTED);
            label(tile, card->value, 16, 40, 340, &lv_font_montserrat_20, ACCENT);
            label(tile, card->detail, 16, 85, 340, &lv_font_montserrat_12, MUTED);
        }
        if (!current.card_count) label(content, "No information cards", 16, 24, 700, &lv_font_montserrat_20, MUTED);
        return;
    }
    unsigned row = 0;
    /* Attention rows are always first, without rewriting the data model order. */
    for (unsigned priority = 0; priority < 2; priority++) {
        for (size_t i = 0; i < current.task_count; i++) {
            const panel_task_t *task = &current.tasks[i];
            if (panel_task_needs_attention(task) != (priority == 0)) continue;
            if (page == 1 && !panel_task_needs_attention(task)) continue;
            uint32_t tile_color = task->state == PANEL_COMPLETED ? 0x153A2B :
                task->state == PANEL_FAILED ? 0x3B2027 :
                task->state == PANEL_WAITING_INPUT ? 0x382A1F : SURFACE;
            lv_obj_t *tile = box(content, 0, row++ * 70, 760, 62, tile_color);
            if (task->id[0]) make_clickable(tile, open_session, (void *)(uintptr_t)i);
            uint32_t color = task->state == PANEL_WAITING_INPUT ? WARN :
                task->state == PANEL_FAILED ? 0xFF8190 :
                task->state == PANEL_COMPLETED ? DONE :
                task->state == PANEL_RUNNING ? ACCENT : MUTED;
            box(tile, 0, 9, 4, 44, color);
            label(tile, task->title, 16, 8, 492, &lv_font_montserrat_20, FG);
            label(tile, task->detail, 16, 37, 520, &lv_font_montserrat_12, MUTED);
            const char *badge_text = task->state == PANEL_COMPLETED ? LV_SYMBOL_OK "  COMPLETED" :
                panel_state_label(task->state);
            lv_obj_t *badge = label(tile, badge_text, 504, 23, 214,
                                    &lv_font_montserrat_16, color);
            lv_obj_set_style_text_align(badge, LV_TEXT_ALIGN_RIGHT, 0);
            if (task->id[0]) label(tile, LV_SYMBOL_RIGHT, 732, 23, 20, &lv_font_montserrat_16, MUTED);
        }
    }
    if (!row) label(content, page == 1 ? "Nothing needs your attention" : "No sessions",
                    16, 24, 700, &lv_font_montserrat_20, MUTED);
    lv_obj_update_layout(content);
    lv_obj_scroll_to_y(content, list_scroll[page], LV_ANIM_OFF);
}

static void open_session(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= current.task_count || !current.tasks[index].id[0]) return;
    list_scroll[page] = lv_obj_get_scroll_y(content);
    snprintf(selected_id, sizeof(selected_id), "%s", current.tasks[index].id);
    render();
}

static void back_to_list(lv_event_t *event)
{
    (void)event;
    selected_id[0] = '\0';
    render();
}

static void change_page(lv_event_t *event)
{
    if (!selected_id[0]) list_scroll[page] = lv_obj_get_scroll_y(content);
    selected_id[0] = '\0';
    page = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    render();
}

void panel_ui_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    label(screen, "DESK / STATUS", 20, 17, 520, &lv_font_montserrat_24, FG);
    source = label(screen, "STARTING", 580, 22, 200, &lv_font_montserrat_16, WARN);
    lv_obj_set_style_text_align(source, LV_TEXT_ALIGN_RIGHT, 0);
    summary = label(screen, "Waiting for data", 20, 54, 750, &lv_font_montserrat_16, MUTED);
    const char *names[] = {"Sessions", "Attention", "Information"};
    for (unsigned i = 0; i < 3; i++) {
        tabs[i] = box(screen, 20 + i * 256, 86, 248, 46, SURFACE);
        make_clickable(tabs[i], change_page, (void *)(uintptr_t)i);
        lv_obj_t *caption = label(tabs[i], names[i], 0, 13, 248, &lv_font_montserrat_16, FG);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    }
    content = box(screen, 20, 148, 760, 278, BG);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    footer = label(screen, "", 20, 448, 760, &lv_font_montserrat_12, MUTED);
    render();
}

void panel_ui_set_snapshot(const panel_snapshot_t *snapshot)
{
    if (!snapshot || !content) return;
    if (!selected_id[0]) list_scroll[page] = lv_obj_get_scroll_y(content);
    current = *snapshot;
    if (current.task_count > PANEL_MAX_TASKS) current.task_count = PANEL_MAX_TASKS;
    if (current.card_count > PANEL_MAX_CARDS) current.card_count = PANEL_MAX_CARDS;
    /* Guard against missing terminators before handing text to LVGL. */
    for (size_t i = 0; i < current.task_count; i++) {
        current.tasks[i].id[sizeof(current.tasks[i].id)-1] = '\0';
        current.tasks[i].title[sizeof(current.tasks[i].title)-1] = '\0';
        current.tasks[i].detail[sizeof(current.tasks[i].detail)-1] = '\0';
    }
    for (size_t i = 0; i < current.card_count; i++) {
        current.cards[i].title[sizeof(current.cards[i].title)-1] = '\0';
        current.cards[i].value[sizeof(current.cards[i].value)-1] = '\0';
        current.cards[i].detail[sizeof(current.cards[i].detail)-1] = '\0';
    }
    render();
}
