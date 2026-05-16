

#pragma once

#include "types.h"

struct MenuItem;
struct Menu;

typedef struct {
    int mx, my;          
    int cx, cy, cw, ch;  
    int button;          
} app_mouse_t;

typedef struct {
    int  dragging;           
    int  drag_start_y;
    int  drag_start_scroll;
    int  manual_timer;       
    
    void (*on_click )(int id, int mx, int my);   
    void (*on_drag  )(int id, int my,             
                      int scroll_start, int delta_y,
                      int content_h, int total_lines);
    void (*end_drag )(void);
} app_scrollbar_t;

typedef struct sv_menu_table {
    const void   *menus;     
    int           count;
} sv_menu_table_t;

typedef struct app_desc {
    
    int         kind;           
    const char *default_title;
    int         def_x, def_y, def_w, def_h;

    

    
    void (*on_open )(int id);

    
    void (*on_close)(int id);

    

    
    void (*draw)(int id, int cx, int cy, int cw, int ch);

    

    void (*draw_overlay)(void);

    

    
    void (*on_key)(int id, int key);

    

    
    void (*on_click)(int id, app_mouse_t *ev);

    
    void (*on_drag )(int id, app_mouse_t *ev);

    
    void (*on_release)(int id, app_mouse_t *ev);

    
    void (*on_right_click)(int id, app_mouse_t *ev);

    

    
    int  (*hit_overlay   )(int mx, int my);

    
    void (*hover_overlay )(int mx, int my);

    
    int  (*overlay_is_open)(void);

    
    void (*close_overlay )(void);

    
    void (*click_overlay )(int mx, int my, int win_id);

    

    

    const char (*icon_bmp)[25];

    

    

    const void  *menu_bar_menus;   
    int          menu_bar_count;

    

    int  (*on_menu_action)(int id, const char *label);

} app_desc_t;
