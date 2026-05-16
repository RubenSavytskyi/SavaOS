

#pragma once

#include "types.h"
#include "app_registry.h"
#include "fs.h"

#define SV_MENU_SAV_COUNT 5

typedef struct {
    int  used, x, y, w, h, z;
    char title[40];
    app_kind_t app;
    int  animating;
    int  anim_frame;
    int  anim_dir;
    int  anim_sx, anim_sy, anim_sw, anim_sh;
    int  anim_ex, anim_ey, anim_ew, anim_eh;
} win_t;

typedef struct {
    const char *label;
    app_kind_t  app;
} MenuItem;

typedef struct {
    const char       *title;
    int               x, w;
    const MenuItem   *items;
    int               item_count;
} Menu;

void dispatch_overlay      (int mx, int my, int left_edge, int left_up);
int  dispatch_right_click  (int wid, int mx, int my);
void dispatch_mouse_click  (int mx, int my);
void dispatch_mouse_drag   (int mx, int my);
void dispatch_mouse_release(int mx, int my);
void dispatch_switch_menus (app_kind_t app,
                            void *desk_menus_ptr, int desk_menus_count);
int  dispatch_menu_action  (const char *label);

extern win_t wins[MAX_WIN];
extern int   desktop_needs_full_blit;
extern int   desktop_icons_dirty;

int  win_top_id  (void);
void bring_to_front(int win_idx);
void win_close   (int id);
void app_open    (app_kind_t app);
void layout_menus(void);

void hline_px(int x1, int x2, int y, u8 c);
void vline_px(int x,  int y1, int y2, u8 c);
int  ksnprintf(char *buf, int cap, const char *fmt, ...);
void str_cpy   (char *d, const char *s, int max);
int  str_len   (const char *s);
int  str_eq    (const char *a, const char *b);

void desktop_mark_icons_dirty(void);
