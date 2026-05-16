#ifndef APP_CALC_H
#define APP_CALC_H

#include "app_registry.h"

extern const app_desc_t app_calc_desc;

extern int calc_default_scientific;

void calc_set_scientific_all(int enable);

void  calc_draw_context_menu(void);
int   calc_hit_context_menu(int mx, int my);

void calc_ctx_open(int x, int y);
void calc_ctx_close(void);

#endif
