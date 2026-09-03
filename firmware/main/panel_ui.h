#pragma once
#include "panel_model.h"

/* Call once after registering/starting the display, with the LVGL adapter locked. */
void panel_ui_create(void);
/* Call only from an LVGL callback or while holding esp_lv_adapter_lock().
 * Snapshot is copied, not retained. All strings must be UTF-8 and NUL terminated.
 * Network workers should enqueue snapshots for the UI task, never mutate LVGL.
 */
void panel_ui_set_snapshot(const panel_snapshot_t *snapshot);

