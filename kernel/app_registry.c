

#include "app_registry.h"

#define APP(name) extern const app_desc_t app_##name##_desc;
#include "apps.def"
#undef APP

static const app_desc_t *registry[] = {
#define APP(name) &app_##name##_desc,
#include "apps.def"
#undef APP
};

#define REGISTRY_COUNT (int)(sizeof(registry) / sizeof(registry[0]))

const app_desc_t *app_registry_get(app_kind_t kind) {
    int i;
    if (kind == APP_NONE) return (void *)0;
    for (i = 0; i < REGISTRY_COUNT; i++) {
        if (registry[i]->kind == kind) return registry[i];
    }
    return (void *)0;
}

int app_registry_any_overlay_open(void) {
    int i;
    for (i = 0; i < REGISTRY_COUNT; i++) {
        if (registry[i]->overlay_is_open && registry[i]->overlay_is_open())
            return 1;
    }
    return 0;
}

void app_registry_init_all(void) {
    
}
