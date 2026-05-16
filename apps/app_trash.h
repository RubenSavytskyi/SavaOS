#ifndef APP_trash_H
#define APP_trash_H

#include "app_registry.h"
#include "types.h"

extern const app_desc_t app_trash_desc;

void trash_move_file(u32 dir_cluster, const char *filename);
void trash_request_desktop_refresh(void);

#endif
