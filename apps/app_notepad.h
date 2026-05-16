#ifndef APP_NOTEPAD_H
#define APP_NOTEPAD_H

#include "app_registry.h"
#include "types.h"

extern const app_desc_t app_notepad_desc;

#define NOTEPAD_BUF_SIZE 4096
#define NOTEPAD_LINE_LEN 80
#define NOTEPAD_UNDO_SIZE 1024
#define NOTEPAD_CLIPBOARD_SIZE 1024

void notepad_set_file_association(int id, u32 dir_cluster, const char *name);
void notepad_write_buffer_to_file(int id, const char *name83);

void notepad_draw_context_menu(void);
int  notepad_hit_context_menu(int mx, int my);
void notepad_context_menu_click(int mx, int my, int id);
int  notepad_overlay_is_open(void);
void notepad_ctx_open(int x, int y);
void notepad_ctx_close(void);

#endif
