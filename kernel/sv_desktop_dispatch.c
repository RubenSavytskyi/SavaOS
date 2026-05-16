

#include "sv_desktop_internal.h"
#include "app_registry.h"
#include "mouse.h"
#include "sv_gfx.h"

extern win_t          wins[MAX_WIN];
extern int            desktop_needs_full_blit;
extern Menu          *menus;
extern Menu          *current_menus;
extern int            current_menu_count;
extern app_kind_t     current_menu_app;
extern int            menu_open;
extern int            menu_hover;
extern int            menu_hot;
extern int            SVS_TITLE_H;

int  win_top_id(void);
void layout_menus(void);

static void client_rect(int id, int *cx, int *cy, int *cw, int *ch) {
    *cx = wins[id].x + 3;
    *cy = wins[id].y + SVS_TITLE_H + 1;
    *cw = wins[id].w - 6;
    *ch = wins[id].h - SVS_TITLE_H - 3;
}

static int inside_client(int id, int mx, int my) {
    int cx, cy, cw, ch;
    client_rect(id, &cx, &cy, &cw, &ch);
    return mx >= cx && mx < cx + cw && my >= cy && my < cy + ch;
}

void dispatch_overlay(int mx, int my, int left_edge, int left_up) {
    int i;
    for (i = 0; i < MAX_WIN; i++) {
        const app_desc_t *desc;
        if (!wins[i].used) continue;
        desc = app_registry_get(wins[i].app);
        if (!desc || !desc->overlay_is_open || !desc->overlay_is_open()) continue;

        
        if (desc->hover_overlay)
            desc->hover_overlay(mx, my);

        
        if (left_edge && desc->hit_overlay && desc->hit_overlay(mx, my) >= 0) {
            if (desc->click_overlay)
                desc->click_overlay(mx, my, i);
            desktop_needs_full_blit = 1;
            return; 
        }

        
        if (left_up && desc->hit_overlay && desc->hit_overlay(mx, my) < 0) {
            if (desc->close_overlay)
                desc->close_overlay();
            desktop_needs_full_blit = 1;
        }
    }
}

int dispatch_right_click(int wid, int mx, int my) {
    const app_desc_t *desc;
    int cx, cy, cw, ch;
    app_mouse_t ev;

    if (wid < 0) return 0;
    desc = app_registry_get(wins[wid].app);
    if (!desc || !desc->on_right_click) return 0;

    client_rect(wid, &cx, &cy, &cw, &ch);
    if (!inside_client(wid, mx, my)) return 0;

    ev.mx = mx; ev.my = my;
    ev.cx = cx; ev.cy = cy; ev.cw = cw; ev.ch = ch;
    ev.button = 2;
    desc->on_right_click(wid, &ev);
    desktop_needs_full_blit = 1;
    return 1;
}

void dispatch_mouse_click(int mx, int my) {
    int active = win_top_id();
    const app_desc_t *desc;
    int cx, cy, cw, ch;
    app_mouse_t ev;

    if (active < 0) return;
    desc = app_registry_get(wins[active].app);
    if (!desc || !desc->on_click) return;

    client_rect(active, &cx, &cy, &cw, &ch);
    ev.mx = mx; ev.my = my;
    ev.cx = cx; ev.cy = cy; ev.cw = cw; ev.ch = ch;
    ev.button = 0;
    desc->on_click(active, &ev);
}

void dispatch_mouse_drag(int mx, int my) {
    int active = win_top_id();
    const app_desc_t *desc;
    int cx, cy, cw, ch;
    app_mouse_t ev;

    if (active < 0) return;
    desc = app_registry_get(wins[active].app);
    if (!desc || !desc->on_drag) return;

    client_rect(active, &cx, &cy, &cw, &ch);
    ev.mx = mx; ev.my = my;
    ev.cx = cx; ev.cy = cy; ev.cw = cw; ev.ch = ch;
    ev.button = 0;
    desc->on_drag(active, &ev);
}

void dispatch_mouse_release(int mx, int my) {
    int active = win_top_id();
    const app_desc_t *desc;
    int cx, cy, cw, ch;
    app_mouse_t ev;

    if (active < 0) return;
    desc = app_registry_get(wins[active].app);
    if (!desc || !desc->on_release) return;

    client_rect(active, &cx, &cy, &cw, &ch);
    ev.mx = mx; ev.my = my;
    ev.cx = cx; ev.cy = cy; ev.cw = cw; ev.ch = ch;
    ev.button = 0;
    desc->on_release(active, &ev);
}

void dispatch_switch_menus(app_kind_t app,
                           void *desk_menus_ptr, int desk_menus_count) {
    const app_desc_t *desc;

    if (current_menu_app == app) return;
    current_menu_app = app;
    menu_open  = -1;
    menu_hover = -1;
    menu_hot   = -1;

    desc = app_registry_get(app);
    if (desc && desc->menu_bar_menus && desc->menu_bar_count > 0) {
        current_menus       = (Menu *)desc->menu_bar_menus;
        current_menu_count  = desc->menu_bar_count;
    } else {
        current_menus       = (Menu *)desk_menus_ptr;
        current_menu_count  = desk_menus_count;
    }
    menus = current_menus;
    layout_menus();
    desktop_needs_full_blit = 1;
}

int dispatch_menu_action(const char *label) {
    int active = win_top_id();
    const app_desc_t *desc;

    if (active < 0) return 0;
    desc = app_registry_get(wins[active].app);
    if (!desc || !desc->on_menu_action) return 0;
    return desc->on_menu_action(active, label);
}
