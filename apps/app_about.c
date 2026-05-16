#include "app_about.h"
#include "sv_gfx.h"

static const char icon[24][25] = {
    "........................",
    "....BBBBBBBBBBBBBBBB....",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWBBBBBBBBBBBBWWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWBWWWWWWWWWWWWBWB...",
    "...BWWBBBBBBBBBBBBWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWBBBWB...",
    "...BWBBWWWWWWWWWWWWWB...",
    "...BWWWWWWWWWWWWWWWWB...",
    "....BBBBBBBBBBBBBBBB....",
    "....BWWWWWWWWWWWWWWB....",
    "....BWWWWWWWWWWWWWWB....",
    "....BBBBBBBBBBBBBBBB....",
    "........................",
    "........................"
};

extern void hline_px(int x1, int x2, int y, u8 c);
extern void draw_text_centered(int x, int y, int w, int h, const char *s,
                               u8 fg, u8 bg);

static void draw(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id;
    int mid = cx + cw / 2;
    int rx  = cx + cw - 1;

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);

    int title_x = mid - 18;
    vga13_draw_string(title_x,     cy + 8,  "SavaOS", VGA13_BLACK, VGA13_WHITE, 0);

    draw_text_centered(cx, cy + 24, cw, 11, "Version 0.2 32-bit",
                       VGA13_BLACK, VGA13_WHITE);

    hline_px(cx, rx, cy + 39, VGA13_BLACK);

    int lx = cx + 20, vx = cx + 70;
    vga13_draw_string(lx, cy + 49, "RAM:",        VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(vx, cy + 49, "32768 KB",    VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(lx, cy + 63, "HD:",         VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(vx, cy + 63, "System HD",   VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(lx, cy + 77, "Mode:",       VGA13_BLACK, VGA13_WHITE, 0);
    vga13_draw_string(vx, cy + 77, "Development", VGA13_BLACK, VGA13_WHITE, 0);
}

const app_desc_t app_about_desc = {
    .kind          = APP_about,
    .default_title = "About SavaOS",
    .def_x = 50, .def_y = 30, .def_w = 210, .def_h = 110,
    .icon_bmp      = icon,
    .draw          = draw,
};
