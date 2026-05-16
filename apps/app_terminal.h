#ifndef APP_TERMINAL_H
#define APP_TERMINAL_H

#include "app_registry.h"

extern const app_desc_t app_terminal_desc;

void draw_term_context_menu(void);
int  hit_term_context_menu(int mx, int my);
void term_context_menu_click(int mx, int my, int win_id);

#endif
extern int term_ctx_menu_x, term_ctx_menu_y, term_ctx_menu_open, term_ctx_menu_hover;
void term_ctx_open(int x, int y);
void term_ctx_close(void);
