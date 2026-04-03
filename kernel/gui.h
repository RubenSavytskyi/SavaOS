#ifndef GUI_H
#define GUI_H

#include "types.h"
#include "vga.h"


#define WF_VISIBLE    0x01
#define WF_ACTIVE     0x02
#define WF_MOVABLE    0x04
#define WF_RESIZABLE  0x08
#define WF_MODAL      0x10
#define WF_NOBORDER   0x20

#define MAX_WINDOWS   8
#define MAX_TITLE     30
#define MAX_ICONS     8


#define BOX_TL      '\xDA'   
#define BOX_TR      '\xBF'   
#define BOX_BL      '\xC0'   
#define BOX_BR      '\xD9'   
#define BOX_H       '\xC4'   
#define BOX_V       '\xB3'   
#define BOX_HTOP    '\xCD'   
#define BOX_VTOP    '\xBA'   
#define BOX_FILL    '\xB2'   
#define BOX_HALF    '\xB1'   
#define BOX_LIGHT   '\xB0'   

typedef struct {
    int x, y, w, h;
    u8  flags;
    char title[MAX_TITLE];
    
    void (*draw)(int wid);
    void (*keydown)(int wid, int key);
    void (*on_close)(int wid);
    int  scroll_y;
    int  cursor_x, cursor_y;
    
    void* data;
} Window;

typedef struct {
    int x;
    char label[12];
    char icon;             
    void (*open)(void);
} DesktopIcon;


void gui_init(void);
void gui_main_loop(void);

int  gui_open_window(int x, int y, int w, int h, const char* title,
                     void (*draw)(int), void (*key)(int, int),
                     void (*on_close)(int));
void gui_close_window(int wid);
void gui_set_active(int wid);
void gui_redraw(void);
void gui_redraw_window(int wid);


void win_putchar(int wid, int x, int y, char c, u8 color);
void win_putstr(int wid, int x, int y, const char* s, u8 color);
void win_fill(int wid, int x, int y, int w, int h, char c, u8 color);
void win_hline(int wid, int x, int y, int w, u8 color);
void win_scroll(int wid);
void win_get_size(int wid, int* w, int* h);
int  win_get_scroll(int wid);
void win_set_scroll(int wid, int scroll_y);


void gui_add_icon(int x, const char* label, char icon, void (*open)(void));

#endif
