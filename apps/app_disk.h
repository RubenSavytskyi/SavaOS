#ifndef APP_DISK_H
#define APP_DISK_H

#include "app_registry.h"
#include "types.h"
#include "fs.h"

extern const app_desc_t app_disk_desc;

void open_disk_at_cluster(u32 dir_cluster, const char *title);

void disk_draw_context_menu(void);
int  disk_hit_context_menu(int mx, int my);

#endif

extern int      disk_count[MAX_WIN];
extern int      disk_sel[MAX_WIN];
extern u32      disk_cwd_cluster[MAX_WIN];
extern int      disk_parent_cluster[MAX_WIN];
extern FSDirEnt disk_entries[MAX_WIN][FS_MAX_FILES];
extern int      disk_clip_valid;
extern u32      disk_clip_src_dir;
extern char     disk_clip_name[];
