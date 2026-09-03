/* Display initialization adapted from Waveshare's LVGL 9 example.
 * Original initialization: 2023-2024 Espressif Systems, SPDX-License-Identifier: CC0-1.0
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "waveshare_rgb_lcd_port.h"
#include "panel_ui.h"

#define BRIDGE_UART UART_NUM_0
#define BRIDGE_LINE_MAX 4096

static const char *TAG = "desk_panel";
static panel_snapshot_t snapshot;
static char bridge_line[BRIDGE_LINE_MAX];

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_hex(char *dst, size_t size, const char *hex)
{
    size_t length = strlen(hex);
    if ((length & 1) || length / 2 >= size) return false;
    for (size_t i = 0; i < length / 2; i++) {
        int high = hex_nibble(hex[i * 2]);
        int low = hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        dst[i] = (char)((high << 4) | low);
    }
    dst[length / 2] = '\0';
    return true;
}

static void start_snapshot(panel_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->connected = true;
}

static bool append_task(char *line, panel_snapshot_t *out)
{
    if (out->task_count == PANEL_MAX_TASKS || strncmp(line, "TASK\t", 5)) return false;
    char *save = NULL;
    char *id = strtok_r(line + 5, "\t", &save);
    char *title = strtok_r(NULL, "\t", &save);
    char *state = strtok_r(NULL, "\t", &save);
    char *approval = strtok_r(NULL, "\t", &save);
    char *source_name = strtok_r(NULL, "\t", &save);
    panel_source_t source = PANEL_CODEX; /* Old bridges omit the optional source field. */
    if (!id || !title || !state || !approval || strtok_r(NULL, "\t", &save) ||
        (source_name && !panel_parse_source(source_name, &source)) ||
        (strcmp(approval, "-1") && strcmp(approval, "0") && strcmp(approval, "1"))) return false;
    panel_task_t *task = &out->tasks[out->task_count];
    task->source = source;
    if (!decode_hex(task->id, sizeof(task->id), id) || !task->id[0] ||
        !decode_hex(task->title, sizeof(task->title), title)) return false;
    task->state = !strcmp(state, "1") ? PANEL_IDLE : !strcmp(state, "2") ? PANEL_RUNNING :
                  !strcmp(state, "3") ? PANEL_WAITING_INPUT :
                  !strcmp(state, "4") ? PANEL_COMPLETED :
                  !strcmp(state, "5") ? PANEL_FAILED : PANEL_UNKNOWN;
    task->approval_known = strcmp(approval, "-1") != 0;
    task->needs_approval = !strcmp(approval, "1");
    const char *detail = task->state == PANEL_RUNNING ? "Local rollout indicates active work" :
                         task->state == PANEL_WAITING_INPUT ? "Waiting for your input on the computer" :
                         task->state == PANEL_COMPLETED ? "Task completed recently" :
                         task->state == PANEL_IDLE ? "No active turn detected" :
                         task->state == PANEL_FAILED ? "Source reported a system error" :
                         "No activity state available";
    snprintf(task->detail, sizeof(task->detail), "%s", detail);
    out->task_count++;
    return true;
}

static bool append_card(char *line, panel_snapshot_t *out)
{
    if (out->card_count == PANEL_MAX_CARDS || strncmp(line, "CARD\t", 5)) return false;
    char *save = NULL;
    char *title = strtok_r(line + 5, "\t", &save);
    char *value = strtok_r(NULL, "\t", &save);
    char *detail = strtok_r(NULL, "\t", &save);
    char *source_name = strtok_r(NULL, "\t", &save);
    panel_source_t source = PANEL_CODEX;
    if (!title || !value || !detail || strtok_r(NULL, "\t", &save) ||
        (source_name && !panel_parse_source(source_name, &source))) return false;
    panel_card_t *card = &out->cards[out->card_count];
    card->source = source;
    if (!decode_hex(card->title, sizeof(card->title), title) ||
        !decode_hex(card->value, sizeof(card->value), value) ||
        !decode_hex(card->detail, sizeof(card->detail), detail)) return false;
    out->card_count++;
    return true;
}

static void parser_self_test(void)
{
    static panel_snapshot_t parsed;
    start_snapshot(&parsed);
    char first[] = "TASK\t6f6e65\t5461736b\t0\t-1";
    char second[] = "TASK\t74776f\t416374697665\t2\t1";
    char card[] = "CARD\t4c454654\t373825\t5765656b6c79";
    assert(append_task(first, &parsed) && append_task(second, &parsed) && append_card(card, &parsed));
    assert(parsed.task_count == 2 && !strcmp(parsed.tasks[0].title, "Task") &&
           !parsed.tasks[0].approval_known && parsed.tasks[1].needs_approval &&
           parsed.card_count == 1 && !strcmp(parsed.cards[0].value, "78%"));
    assert(parsed.tasks[0].source == PANEL_CODEX && parsed.cards[0].source == PANEL_CODEX);
    char kimi[] = "TASK\t6f6e65\t4b696d69\t4\t0\tkimi";
    char dsh[] = "CARD\t445348\t4f4b\t54657374\tdsh";
    char invalid[] = "TASK\t78\t78\t2\t0\tother";
    assert(append_task(kimi, &parsed) && append_card(dsh, &parsed));
    assert(!append_task(invalid, &parsed));
    assert(panel_find_task(&parsed, PANEL_CODEX, "one") == &parsed.tasks[0]);
    assert(panel_find_task(&parsed, PANEL_KIMI, "one") == &parsed.tasks[2]);
    assert(panel_task_matches(&parsed.tasks[2], PANEL_SOURCE_COUNT));
    assert(!panel_task_matches(&parsed.tasks[2], PANEL_CODEX));
    assert(panel_task_needs_attention(&parsed.tasks[2]));
    assert(panel_completion(&parsed) == &parsed.tasks[2]);
    assert(panel_source_available(&parsed, PANEL_DSH));
    ESP_LOGI(TAG, "Parser/source/filter self-test OK");
}

static void bridge_task(void *argument)
{
    (void)argument;
    size_t used = 0;
    bool overflow = false;
    bool receiving = false;
    panel_snapshot_t pending;
    uint8_t data[128];
    while (true) {
        int length = uart_read_bytes(BRIDGE_UART, data, sizeof(data), pdMS_TO_TICKS(100));
        for (int i = 0; i < length; i++) {
            if (data[i] == '\n') {
                if (!overflow && used) {
                    bridge_line[used] = '\0';
                    if (!strcmp(bridge_line, "BEGIN")) {
                        start_snapshot(&pending);
                        receiving = true;
                    } else if (!strcmp(bridge_line, "END") && receiving) {
                        ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
                        snapshot = pending;
                        panel_ui_set_snapshot(&snapshot);
                        esp_lv_adapter_unlock();
                        ESP_LOGI(TAG, "REAL snapshot tasks=%u cards=%u", (unsigned)snapshot.task_count,
                                 (unsigned)snapshot.card_count);
                        receiving = false;
                    } else if (receiving && !append_task(bridge_line, &pending) &&
                               !append_card(bridge_line, &pending)) {
                        ESP_LOGW(TAG, "Ignored invalid bridge snapshot");
                        receiving = false;
                    }
                } else if (overflow) {
                    ESP_LOGW(TAG, "Discarded oversized bridge line");
                    receiving = false;
                }
                used = 0;
                overflow = false;
            } else if (data[i] != '\r' && !overflow) {
                if (used + 1 < sizeof(bridge_line)) bridge_line[used++] = (char)data[i];
                else overflow = true;
            }
        }
    }
}

static void age_tick(lv_timer_t *timer)
{
    (void)timer;
    if (snapshot.connected && snapshot.age_seconds < UINT32_MAX) {
        snapshot.age_seconds++;
        panel_ui_set_snapshot(&snapshot);
    }
}

void app_main(void)
{
    parser_self_test();
    ESP_LOGI(TAG, "Starting Desk Panel - waiting for real Codex tasks on UART");
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB, rotation, &panel, &touch));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    config.task_stack_size = 12 * 1024;
    config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&config));
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation);
    display_config.profile.use_psram = true;
    lv_display_t *display = esp_lv_adapter_register_display(&display_config);
    assert(display != NULL);
    if (touch) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);
        lv_indev_t *input = esp_lv_adapter_register_touch(&touch_config);
        if (!input) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    panel_ui_create();
    panel_ui_set_snapshot(&snapshot);
    if (!lv_timer_create(age_tick, 1000, NULL)) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    esp_lv_adapter_unlock();

    uart_config_t uart_config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BRIDGE_UART, BRIDGE_LINE_MAX, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BRIDGE_UART, &uart_config));
    if (xTaskCreate(bridge_task, "codex_uart", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
