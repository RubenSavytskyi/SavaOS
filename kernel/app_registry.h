

#pragma once

#include "app_desc.h"

#define MAX_WIN 8

typedef enum {
    APP_NONE = 0,
#define APP(name) APP_##name,
#include "apps.def"
#undef APP
    APP_COUNT
} app_kind_t;

const app_desc_t *app_registry_get(app_kind_t kind);

int app_registry_any_overlay_open(void);

void app_registry_init_all(void);
