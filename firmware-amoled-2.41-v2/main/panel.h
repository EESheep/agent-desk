#pragma once
#include <stdbool.h>
void panel_init(void);
bool panel_accept(const char *json);
void panel_network(bool wifi, bool http, bool configured);
void panel_debug_page(int target);
