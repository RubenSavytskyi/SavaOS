
#ifndef VGA_H
#define VGA_H

#include "types.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEM     0xB8000

#define COLOR_BLACK         0x0
#define COLOR_BLUE          0x1
#define COLOR_GREEN         0x2
#define COLOR_CYAN          0x3
#define COLOR_RED           0x4
#define COLOR_MAGENTA       0x5
#define COLOR_BROWN         0x6
#define COLOR_LIGHT_GREY    0x7
#define COLOR_DARK_GREY     0x8
#define COLOR_LIGHT_BLUE    0x9
#define COLOR_LIGHT_GREEN   0xA
#define COLOR_LIGHT_CYAN    0xB
#define COLOR_LIGHT_RED     0xC
#define COLOR_LIGHT_MAGENTA 0xD
#define COLOR_YELLOW        0xE
#define COLOR_WHITE         0xF

#define WIN_DESKTOP_BG      COLOR_CYAN
#define WIN_TASKBAR_BG      COLOR_LIGHT_GREY
#define WIN_TASKBAR_FG      COLOR_BLACK
#define WIN_TITLE_ACTIVE    COLOR_BLUE
#define WIN_TITLE_TEXT      COLOR_WHITE
#define WIN_TITLE_INACTIVE  COLOR_DARK_GREY
#define WIN_BORDER          COLOR_LIGHT_GREY
#define WIN_BODY_BG         COLOR_WHITE
#define WIN_BODY_FG         COLOR_BLACK
#define WIN_BUTTON_BG       COLOR_LIGHT_GREY
#define WIN_BUTTON_FG       COLOR_BLACK
#define WIN_MENU_BG         COLOR_LIGHT_GREY
#define WIN_MENU_FG         COLOR_BLACK
#define WIN_MENU_SEL_BG     COLOR_BLUE
#define WIN_MENU_SEL_FG     COLOR_WHITE
#define WIN_START_BG        COLOR_LIGHT_GREY
#define WIN_START_FG        COLOR_BLACK

#define MAKE_COLOR(fg, bg) ((u8)(((bg) << 4) | (fg)))
#define VGA_ENTRY(ch, color) ((u16)((u16)(color) << 8 | (u8)(ch)))

void vga_init(void);
void vga_clear(u8 color);
void vga_putchar_at(int x, int y, char c, u8 color);
void vga_putstr_at(int x, int y, const char* s, u8 color);
void vga_fill_rect(int x, int y, int w, int h, char c, u8 color);
void vga_draw_hline(int x, int y, int w, u8 color);
void vga_draw_vline(int x, int y, int h, u8 color);
void vga_scroll_region(int x, int y, int w, int h, int lines);
u16  vga_get_char(int x, int y);

#endif
