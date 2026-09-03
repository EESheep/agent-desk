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
static lv_obj_t *tab_labels[3], *filters[PANEL_SOURCE_COUNT + 1];
static lv_obj_t *filter_labels[PANEL_SOURCE_COUNT + 1], *notice, *notice_text;
static unsigned page;
static panel_source_t filter = PANEL_SOURCE_COUNT, selected_source;
/* Source + ID is stable across sorting and same-ID sessions from different apps. */
static char selected_id[sizeof(((panel_task_t *)0)->id)];
static int32_t list_scroll[PANEL_SOURCE_COUNT + 1][3];
static lv_obj_t *detail_body, *detail_title, *detail_id;
static lv_obj_t *detail_state, *detail_summary, *detail_notice;

static void render(void);
static void open_session(lv_event_t *event);
static void back_to_list(lv_event_t *event);

static uint32_t state_color(panel_state_t state)
{
    return state == PANEL_COMPLETED ? DONE : state == PANEL_WAITING_INPUT ? WARN :
        state == PANEL_FAILED ? 0xFF8190 : state == PANEL_RUNNING ? ACCENT : MUTED;
}

static uint32_t state_surface(panel_state_t state)
{
    return state == PANEL_COMPLETED ? 0x153A2B : state == PANEL_WAITING_INPUT ? 0x382A1F :
        state == PANEL_FAILED ? 0x3B2027 : SURFACE;
}

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
        detail_body = box(content, 0, 52, 760, 152, SURFACE);
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

    const panel_task_t *task = panel_find_task(&current, selected_source, selected_id);
    int32_t scroll = lv_obj_get_scroll_y(detail_body);
    lv_label_set_text_fmt(detail_id, "%s / Session ID: %s", panel_source_label(selected_source), selected_id);
    if (!task) {
        lv_label_set_text(detail_title, "Session unavailable");
        lv_label_set_text(detail_state, "NOT IN LATEST SNAPSHOT");
        lv_obj_set_style_text_color(detail_state, lv_color_hex(WARN), 0);
        lv_label_set_text(detail_summary, "This session is no longer in the received list. Go back to choose another session.");
        lv_label_set_text(detail_notice, "Its status is unknown; absence does not mean it completed.");
    } else {
        lv_label_set_text(detail_title, task->title);
        lv_label_set_text(detail_state, panel_state_label(task->state));
        lv_obj_set_style_text_color(detail_state, lv_color_hex(state_color(task->state)), 0);
        lv_label_set_text(detail_summary, task->detail[0] ? task->detail : "No summary available.");
        const char *notice = task->state == PANEL_WAITING_INPUT ? "Waiting for your reply. Continue this session on your computer." :
            task->state == PANEL_COMPLETED ? "Recent turn end reported. This is not a guarantee of success." :
            task->state == PANEL_FAILED ? "Task failed. Check the error details on your computer." :
            "Read-only activity reminder. Full conversation stays on your computer.";
        lv_label_set_text_fmt(detail_notice, "%s%s", current.demo ? "SIMULATED SESSION / No real action.\n" : "", notice);
    }
    /* Update existing labels instead of rebuilding the page on every snapshot. */
    lv_obj_update_layout(detail_body);
    lv_obj_scroll_to_y(detail_body, scroll, LV_ANIM_OFF);
}

static void render_chrome(void)
{
    size_t count = 0, running = 0, completed = 0, attention = 0;
    for (size_t i = 0; i < current.task_count; i++) {
        const panel_task_t *task = &current.tasks[i];
        if (!panel_task_matches(task, filter)) continue;
        count++;
        if (task->state == PANEL_RUNNING) running++;
        if (task->state == PANEL_COMPLETED) completed++;
        if (panel_task_needs_attention(task)) attention++;
    }
    lv_label_set_text_fmt(summary, "%s / %u sessions / %u running / %u completed",
                         panel_source_label(filter), (unsigned)count, (unsigned)running, (unsigned)completed);
    lv_obj_set_style_text_color(summary, lv_color_hex(completed ? DONE : MUTED), 0);
    lv_label_set_text_fmt(tab_labels[1], "Attention (%u)", (unsigned)attention);
    bool stale = current.age_seconds > 15;
    lv_label_set_text(source, current.demo ? "DEMO DATA" : !current.connected ? "WAITING" :
                     stale ? "STALE DATA" : "SYNCED");
    lv_obj_set_style_text_color(source, lv_color_hex(current.demo || !current.connected || stale ? WARN : ACCENT), 0);
    lv_label_set_text_fmt(footer, "%s / Read-only reminders / USB UART / snapshot %u s ago",
                         current.demo ? "SIMULATED DATA" : "Local activity + quota", (unsigned)current.age_seconds);
    for (unsigned i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(tabs[i], lv_color_hex(page == i ? 0x2B4857 : SURFACE), 0);
        lv_obj_set_style_border_width(tabs[i], page == i ? 2 : 0, 0);
        lv_obj_set_style_border_side(tabs[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(tabs[i], lv_color_hex(ACCENT), 0);
    }
    for (unsigned i = 0; i <= PANEL_SOURCE_COUNT; i++) {
        panel_source_t option = i == 0 ? PANEL_SOURCE_COUNT : (panel_source_t)(i - 1);
        lv_obj_set_style_bg_color(filters[i], lv_color_hex(option == filter ? 0x2B4857 : SURFACE), 0);
        lv_obj_set_style_border_width(filters[i], option == filter ? 2 : 0, 0);
        lv_obj_set_style_border_side(filters[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(filters[i], lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_color(filter_labels[i], lv_color_hex(option == PANEL_SOURCE_COUNT ||
                                   panel_source_available(&current, option) ? FG : MUTED), 0);
    }

    /* Global: source filters and page navigation never hide a recent completion. */
    const panel_task_t *alert = panel_completion(&current);
    if (!alert) {
        for (size_t i = 0; i < current.task_count; i++) {
            if (panel_task_needs_attention(&current.tasks[i])) {
                alert = &current.tasks[i];
                break;
            }
        }
    }
    uint32_t color = MUTED, surface = SURFACE;
    if (!current.connected) {
        lv_label_set_text(notice_text, "Waiting for the computer bridge on USB UART");
    } else if (stale) {
        color = WARN;
        surface = 0x382A1F;
        lv_label_set_text(notice_text, LV_SYMBOL_WARNING "  Data stale - check the computer bridge");
    } else if (alert) {
        color = state_color(alert->state);
        surface = state_surface(alert->state);
        lv_label_set_text_fmt(notice_text, "%s  %s %s / %s",
            alert->state == PANEL_COMPLETED ? LV_SYMBOL_OK : LV_SYMBOL_WARNING,
            panel_source_label(alert->source),
            alert->state == PANEL_COMPLETED ? "completed" :
            alert->state == PANEL_FAILED ? "reported an error" : "needs input",
            alert->title);
    } else {
        lv_label_set_text(notice_text, "No recent completions or attention requests");
    }
    lv_obj_set_style_bg_color(notice, lv_color_hex(surface), 0);
    lv_obj_set_style_text_color(notice_text, lv_color_hex(color), 0);
}

static const panel_card_t *find_card(panel_source_t app, const char *title)
{
    for (size_t i = 0; i < current.card_count; i++) {
        if (current.cards[i].source == app && !strcmp(current.cards[i].title, title))
            return &current.cards[i];
    }
    return NULL;
}

static void render_information(void)
{
    if (filter == PANEL_SOURCE_COUNT) {
        for (unsigned app = 0; app < PANEL_SOURCE_COUNT; app++) {
            bool available = panel_source_available(&current, (panel_source_t)app);
            lv_obj_t *tile = box(content, 0, app * 70, 760, 62, SURFACE);
            const char *name = app == PANEL_DSH ? "DeepSeek Harness" : panel_source_label((panel_source_t)app);
            label(tile, name, 16, 8, 236, &lv_font_montserrat_20, FG);
            lv_obj_t *value = label(tile, available ? "DATA RECEIVED" : "NOT CONNECTED",
                                    252, 10, 492, &lv_font_montserrat_16, available ? ACCENT : MUTED);
            const panel_card_t *left = find_card((panel_source_t)app, "CODEX LEFT");
            const panel_card_t *reset = find_card((panel_source_t)app, "RESET IN");
            if (available && left && reset)
                lv_label_set_text_fmt(value, "%s left / reset in %s", left->value, reset->value);
            label(tile, available ? "Select this software above for its information cards" :
                  "No source data received. No simulated sessions or quota.", 16, 39, 728,
                  &lv_font_montserrat_12, MUTED);
        }
        return;
    }
    unsigned index = 0;
    for (size_t i = 0; i < current.card_count; i++) {
        const panel_card_t *card = &current.cards[i];
        if (card->source != filter) continue;
        lv_obj_t *tile = box(content, (index % 2) * 386, (index / 2) * 106, 374, 98, SURFACE);
        index++;
        label(tile, card->title, 16, 10, 342, &lv_font_montserrat_12, MUTED);
        label(tile, card->value, 16, 32, 342, &lv_font_montserrat_20, ACCENT);
        label(tile, card->detail, 16, 70, 342, &lv_font_montserrat_12, MUTED);
    }
    if (!index) label(content, "No information cards reported", 16, 24, 728, &lv_font_montserrat_20, MUTED);
}

static void render(void)
{
    render_chrome();
    if (selected_id[0]) {
        render_detail();
        return;
    }
    lv_obj_clean(content);
    detail_body = NULL;
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);
    if (filter != PANEL_SOURCE_COUNT && !panel_source_available(&current, filter)) {
        label(content, panel_source_label(filter), 16, 18, 728, &lv_font_montserrat_24, FG);
        label(content, "NOT CONNECTED", 16, 62, 728, &lv_font_montserrat_20, MUTED);
        label(content, filter == PANEL_CODEX ? "Start the computer bridge to receive Codex data." :
              "This software adapter is not connected. No sample data is shown.", 16, 104, 728,
              &lv_font_montserrat_16, MUTED);
    } else if (page == 2) {
        render_information();
    } else {
        unsigned row = 0;
        for (unsigned priority = 0; priority < 2; priority++) {
            for (size_t i = 0; i < current.task_count; i++) {
                const panel_task_t *task = &current.tasks[i];
                if (!panel_task_matches(task, filter)) continue;
                if (panel_task_needs_attention(task) != (priority == 0)) continue;
                if (page == 1 && !panel_task_needs_attention(task)) continue;
                lv_obj_t *tile = box(content, 0, row++ * 70, 760, 62, state_surface(task->state));
                if (task->id[0]) make_clickable(tile, open_session, (void *)(uintptr_t)i);
                uint32_t color = state_color(task->state);
                box(tile, 0, 9, 4, 44, color);
                label(tile, task->title, 16, 8, 480, &lv_font_montserrat_20, FG);
                lv_obj_t *meta = label(tile, "", 16, 37, 480, &lv_font_montserrat_12, MUTED);
                lv_label_set_text_fmt(meta, "%s / %s", panel_source_label(task->source), task->detail);
                const char *badge_text = task->state == PANEL_COMPLETED ? LV_SYMBOL_OK " COMPLETED" :
                    task->state == PANEL_WAITING_INPUT ? LV_SYMBOL_WARNING " WAITING INPUT" :
                    panel_state_label(task->state);
                lv_obj_t *badge = label(tile, badge_text, 504, 23, 214, &lv_font_montserrat_16, color);
                lv_obj_set_style_text_align(badge, LV_TEXT_ALIGN_RIGHT, 0);
                if (task->id[0]) label(tile, LV_SYMBOL_RIGHT, 732, 23, 20, &lv_font_montserrat_16, MUTED);
            }
        }
        if (!row) label(content, page == 1 ? "Nothing needs your attention" : "No sessions",
                        16, 24, 728, &lv_font_montserrat_20, MUTED);
    }
    lv_obj_update_layout(content);
    lv_obj_scroll_to_y(content, list_scroll[filter][page], LV_ANIM_OFF);
}

static void open_session(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= current.task_count || !current.tasks[index].id[0]) return;
    list_scroll[filter][page] = lv_obj_get_scroll_y(content);
    selected_source = current.tasks[index].source;
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
    if (!selected_id[0]) list_scroll[filter][page] = lv_obj_get_scroll_y(content);
    selected_id[0] = '\0';
    page = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    render();
}

static void change_filter(lv_event_t *event)
{
    if (!selected_id[0]) list_scroll[filter][page] = lv_obj_get_scroll_y(content);
    selected_id[0] = '\0';
    filter = (panel_source_t)(uintptr_t)lv_event_get_user_data(event);
    render();
}

void panel_ui_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    label(screen, "DESK / AGENTS", 20, 16, 520, &lv_font_montserrat_24, FG);
    source = label(screen, "STARTING", 580, 22, 200, &lv_font_montserrat_16, WARN);
    lv_obj_set_style_text_align(source, LV_TEXT_ALIGN_RIGHT, 0);
    for (unsigned i = 0; i <= PANEL_SOURCE_COUNT; i++) {
        panel_source_t option = i == 0 ? PANEL_SOURCE_COUNT : (panel_source_t)(i - 1);
        filters[i] = box(screen, 20 + i * 192, 54, 184, 44, SURFACE);
        make_clickable(filters[i], change_filter, (void *)(uintptr_t)option);
        filter_labels[i] = label(filters[i], panel_source_label(option), 0, 12, 184,
                                 &lv_font_montserrat_16, FG);
        lv_obj_set_style_text_align(filter_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    notice = box(screen, 20, 110, 760, 44, SURFACE);
    notice_text = label(notice, "Waiting for bridge", 16, 12, 728, &lv_font_montserrat_16, MUTED);
    summary = label(screen, "Waiting for data", 20, 168, 750, &lv_font_montserrat_16, MUTED);
    const char *names[] = {"Sessions", "Attention", "Information"};
    for (unsigned i = 0; i < 3; i++) {
        tabs[i] = box(screen, 20 + i * 256, 412, 248, 44, SURFACE);
        make_clickable(tabs[i], change_page, (void *)(uintptr_t)i);
        tab_labels[i] = label(tabs[i], names[i], 0, 12, 248, &lv_font_montserrat_16, FG);
        lv_obj_set_style_text_align(tab_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    content = box(screen, 20, 196, 760, 204, BG);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    footer = label(screen, "", 20, 462, 760, &lv_font_montserrat_12, MUTED);
    render();
}

void panel_ui_set_snapshot(const panel_snapshot_t *snapshot)
{
    if (!snapshot || !content) return;
    bool changed = current.demo != snapshot->demo || current.connected != snapshot->connected ||
        current.task_count != snapshot->task_count || current.card_count != snapshot->card_count ||
        memcmp(current.tasks, snapshot->tasks, sizeof(current.tasks)) ||
        memcmp(current.cards, snapshot->cards, sizeof(current.cards));
    if (changed && !selected_id[0]) list_scroll[filter][page] = lv_obj_get_scroll_y(content);
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
    /* Age-only ticks must not destroy touch targets or interrupt a scroll gesture. */
    if (changed) render();
    else render_chrome();
}
