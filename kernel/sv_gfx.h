#ifndef SV_GFX_H
#define SV_GFX_H

#include "types.h"

#define VGA13_WIDTH  320
#define VGA13_HEIGHT 200
#define VGA13_FB     0xA0000

#define VGA13_BLACK      0
#define PAL_DARK_GRAY    16
#define PAL_LIGHT_GRAY   17
#define VGA13_WHITE      18
#define PAL_DESKTOP      19
#define PAL_DITHER_A     20
#define PAL_DITHER_B     21

#define SVS_MENU_BAR_H   13
#define SVS_TITLE_H      14
#define SVS_CTRL_SIZE    11

void vga13_init(void);
void vga13_init_palette_sv(void);
void vga13_clear_vram(void);

#define VGA13_BUF_SIZE 64000
extern u8 vga13_back_buffer[VGA13_BUF_SIZE];
void vga13_flip_buffer(void);
void vga13_clear_back_buffer(void);

void vga_wait_vblank(void);

void vga13_put_pixel(int x, int y, u8 color_index);
u8   vga13_get_pixel(int x, int y);

void vga13_fill_rect(int x, int y, int w, int h, u8 c);

void sv_draw_glyph(int px, int py, char ch, u8 fg, u8 bg);
void vga13_draw_string(int x, int y, const char *text, u8 fg, u8 bg, int inverted);

void draw_global_menu_bar(const char *menus);
void draw_svs_window(int x, int y, int width, int height, const char *title, int is_active);
void draw_svs_button(int x, int y, int width, int height, const char *text, int is_pressed);
void draw_text_centered(int x, int y, int w, int h, const char *text, u8 fg, u8 bg);

#endif
