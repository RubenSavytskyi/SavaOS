#include "app_control_panel.h"
#include "sv_desktop_internal.h"
#include "sv_gfx.h"
#include "rtc.h"
#include "timer.h"
#include "string.h"
#include "app_calc.h"

static void draw_radio(int cx, int cy, int filled) {
    int i;
    static const int pts[][2] = {
        {2,0},{3,0},{4,0},{5,0},{6,0},
        {1,1},{7,1},
        {0,2},{8,2},{0,3},{8,3},{0,4},{8,4},
        {0,5},{8,5},{0,6},{8,6},
        {1,7},{7,7},
        {2,8},{3,8},{4,8},{5,8},{6,8},
    };
    int n = (int)(sizeof(pts)/sizeof(pts[0]));
    for (i = 0; i < n; i++)
        vga13_put_pixel(cx + pts[i][0], cy + pts[i][1], VGA13_BLACK);
    if (filled) {
        vga13_fill_rect(cx+2, cy+2, 5, 5, VGA13_BLACK);
        vga13_put_pixel(cx+2, cy+2, VGA13_WHITE);
        vga13_put_pixel(cx+6, cy+2, VGA13_WHITE);
        vga13_put_pixel(cx+2, cy+6, VGA13_WHITE);
        vga13_put_pixel(cx+6, cy+6, VGA13_WHITE);
    }
}

static void draw_checkbox(int x, int y, int checked) {
    int i;
    vga13_fill_rect(x, y, 11, 11, VGA13_WHITE);
    for (i = 0; i < 11; i++) {
        vga13_put_pixel(x+i, y,    VGA13_BLACK);
        vga13_put_pixel(x+i, y+10, VGA13_BLACK);
        vga13_put_pixel(x,   y+i,  VGA13_BLACK);
        vga13_put_pixel(x+10,y+i,  VGA13_BLACK);
    }
    if (checked) {
        vga13_put_pixel(x+2, y+5, VGA13_BLACK);
        vga13_put_pixel(x+2, y+6, VGA13_BLACK);
        vga13_put_pixel(x+3, y+6, VGA13_BLACK);
        vga13_put_pixel(x+3, y+7, VGA13_BLACK);
        vga13_put_pixel(x+4, y+7, VGA13_BLACK);
        vga13_put_pixel(x+4, y+8, VGA13_BLACK);
        vga13_put_pixel(x+5, y+7, VGA13_BLACK);
        vga13_put_pixel(x+6, y+6, VGA13_BLACK);
        vga13_put_pixel(x+7, y+5, VGA13_BLACK);
        vga13_put_pixel(x+8, y+4, VGA13_BLACK);
        vga13_put_pixel(x+8, y+3, VGA13_BLACK);
    }
}

static void hline_cp(int x1, int x2, int y) {
    int x; for (x = x1; x <= x2; x++) vga13_put_pixel(x, y, VGA13_BLACK);
}
static void vline_cp(int x, int y1, int y2) {
    int y; for (y = y1; y <= y2; y++) vga13_put_pixel(x, y, VGA13_BLACK);
}
static void rect_cp(int x, int y, int w, int h) {
    hline_cp(x, x+w-1, y); hline_cp(x, x+w-1, y+h-1);
    vline_cp(x, y, y+h-1); vline_cp(x+w-1, y, y+h-1);
}

static void draw_checker(int x, int y, int w, int h) {
    int px, py;
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++)
            vga13_put_pixel(x+px, y+py, ((px+py)%2==0) ? VGA13_BLACK : VGA13_WHITE);
}

static void draw_gray_pat(int x, int y, int w, int h) {
    int px, py;
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++)
            vga13_put_pixel(x+px, y+py, ((px+py)%2==0) ? PAL_LIGHT_GRAY : VGA13_WHITE);
}

static void draw_clock_icon(int x, int y) {

    static const int pts[][2] = {
        {2,0},{3,0},{4,0},{5,0},{6,0},
        {1,1},{7,1},{0,2},{8,2},{0,3},{8,3},{0,4},{8,4},
        {0,5},{8,5},{0,6},{8,6},{1,7},{7,7},
        {2,8},{3,8},{4,8},{5,8},{6,8},
    };
    int i, n = (int)(sizeof(pts)/sizeof(pts[0]));
    for (i = 0; i < n; i++)
        vga13_put_pixel(x+pts[i][0], y+pts[i][1], VGA13_BLACK);

    vga13_put_pixel(x+4, y+2, VGA13_BLACK);
    vga13_put_pixel(x+4, y+3, VGA13_BLACK);
    vga13_put_pixel(x+4, y+4, VGA13_BLACK);
    vga13_put_pixel(x+5, y+4, VGA13_BLACK);
    vga13_put_pixel(x+6, y+4, VGA13_BLACK);
}

static void draw_cal_icon(int x, int y) {
    int i;

    rect_cp(x, y, 13, 13);

    for (i = x+1; i < x+12; i++) vga13_put_pixel(i, y+1, VGA13_BLACK);

    vga13_put_pixel(x+3, y+4, VGA13_BLACK);
    vga13_put_pixel(x+3, y+5, VGA13_BLACK);
    vga13_put_pixel(x+3, y+6, VGA13_BLACK);
    vga13_put_pixel(x+3, y+7, VGA13_BLACK);
    vga13_put_pixel(x+3, y+8, VGA13_BLACK);

    vga13_put_pixel(x+6, y+4, VGA13_BLACK);
    vga13_put_pixel(x+7, y+4, VGA13_BLACK);
    vga13_put_pixel(x+8, y+4, VGA13_BLACK);
    vga13_put_pixel(x+8, y+5, VGA13_BLACK);
    vga13_put_pixel(x+6, y+6, VGA13_BLACK);
    vga13_put_pixel(x+7, y+6, VGA13_BLACK);
    vga13_put_pixel(x+8, y+6, VGA13_BLACK);
    vga13_put_pixel(x+8, y+7, VGA13_BLACK);
    vga13_put_pixel(x+6, y+8, VGA13_BLACK);
    vga13_put_pixel(x+7, y+8, VGA13_BLACK);
    vga13_put_pixel(x+8, y+8, VGA13_BLACK);
}

void app_control_panel_draw_client(int id, int cx, int cy, int cw, int ch) {
    (void)id;
    int i;

    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);

    int pad   = 5;
    int col1w = (cw * 55) / 100;
    int col2x = cx + col1w;
    int col2w = cw - col1w;

    vline_cp(col2x, cy, cy+ch-1);

    int lx = cx + pad;
    int ly = cy + pad;

    vga13_draw_string(lx, ly, "Desktop Pattern", VGA13_BLACK, VGA13_WHITE, 0);
    ly += 12;

    int pw = (col1w - pad*2 - 8) / 2;
    int ph = 18;
    if (pw < 20) pw = 20;

    rect_cp(lx-1,    ly-1, pw+2, ph+2); 
    draw_checker(lx, ly, pw, ph);

    rect_cp(lx+pw+6-1, ly-1, pw+2, ph+2);
    draw_gray_pat(lx+pw+6, ly, pw, ph);

if (ui_desktop_pattern == 2) {
    rect_cp(lx - 3, ly - 3, pw + 6, ph + 6);
} else if (ui_desktop_pattern == 1) {
    rect_cp(lx + pw + 6 - 3, ly - 3, pw + 6, ph + 6);
} else {
    

}

    ly += ph + 6;

    int lblw = 0;
    (void)lblw;
    {
        int total = 15 * 6;
        int off   = (col1w - pad - total) / 2;
        if (off < 0) off = 0;
        vga13_draw_string(cx + off, ly, "Colour", PAL_DARK_GRAY, VGA13_WHITE, 0);
    }
    ly += 10;

    int sq = (col1w - pad*2) / 8;
    if (sq < 8) sq = 8;
    for (i = 0; i < 8; i++) {
        u8 c;
        if      (i == 0) c = VGA13_BLACK;
        else if (i == 7) c = VGA13_WHITE;
        else if (i < 4)  c = PAL_DARK_GRAY;
        else             c = PAL_LIGHT_GRAY;
        vga13_fill_rect(lx + i*(sq+1), ly, sq, sq-1, c);
        rect_cp(lx + i*(sq+1), ly, sq, sq-1);
    }
if (ui_desktop_color >= 0 && ui_desktop_color < 8) {
        int hx = lx + ui_desktop_color * (sq + 1);
        rect_cp(hx - 2, ly - 2, sq + 4, sq + 3);
    }
    ly += sq + 4;

    hline_cp(cx+1, col2x-1, ly);
    ly += 5;

    vga13_draw_string(lx, ly, "System Settings", VGA13_BLACK, VGA13_WHITE, 0);
    ly += 10;

    int sec_w = col1w - pad*2;
    int sec_h = 46;
    rect_cp(lx-1, ly-1, sec_w+2, sec_h+2);

    draw_checkbox(lx+4, ly+4, ui_show_scrollbar_grid);
    vga13_draw_string(lx+20, ly+5, "Scroll Grid", VGA13_BLACK, VGA13_WHITE, 0);

    draw_checkbox(lx+4, ly+18, calc_default_scientific);
    vga13_draw_string(lx+20, ly+19, "Sci. Calc", VGA13_BLACK, VGA13_WHITE, 0);

    draw_checkbox(lx+4, ly+32, ui_clock_show_seconds);
    vga13_draw_string(lx+20, ly+33, "Clock Sec.", VGA13_BLACK, VGA13_WHITE, 0);

    ly += sec_h + 6;

    int rx2 = col2x + pad;
    int ry  = cy + pad;

    vga13_draw_string(rx2, ry, "Point Blinking", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 12;

    {
        int mid = col2w / 2 - 4;
        int sx  = rx2 + 2;
        int fx  = col2x + col2w - 25;
        draw_radio(sx, ry, !ui_caret_blink_fast);
        draw_radio(fx, ry, ui_caret_blink_fast);
        hline_cp(sx+9, fx-1, ry+4);
        ry += 11;
        vga13_draw_string(sx,   ry, "Slow", VGA13_BLACK, VGA13_WHITE, 0);
        vga13_draw_string(fx-2, ry, "Fast", VGA13_BLACK, VGA13_WHITE, 0);
        ry += 12;
        (void)mid;
    }

    hline_cp(col2x+1, col2x+col2w-1, ry);
    ry += 8;

    draw_clock_icon(rx2, ry);
    vga13_draw_string(rx2+12, ry+1, "Time", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 12;

    {
        char tbuf[16];
        rtc_get_time_string(tbuf, (int)sizeof(tbuf), ui_clock_show_seconds, ui_clock_12hr);
        vga13_draw_string(rx2, ry, tbuf, VGA13_BLACK, VGA13_WHITE, 0);
        ry += 11;
    }

    draw_radio(rx2,    ry, ui_clock_12hr); vga13_draw_string(rx2+11,    ry+1, "12hr", VGA13_BLACK, VGA13_WHITE, 0);
    draw_radio(rx2+40, ry, !ui_clock_12hr); vga13_draw_string(rx2+40+11, ry+1, "24hr", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 15;

    hline_cp(col2x+1, col2x+col2w-1, ry);
    ry += 8;

    draw_cal_icon(rx2, ry);
    vga13_draw_string(rx2+16, ry+3, "Date", VGA13_BLACK, VGA13_WHITE, 0);
    ry += 17;

    {
        char dbuf[12];
        rtc_format_short_date(dbuf, (int)sizeof(dbuf));
        vga13_draw_string(rx2, ry, dbuf, VGA13_BLACK, VGA13_WHITE, 0);
    }
    ry += 10;
}

int ui_show_scrollbar_grid = 0;
int ui_clock_show_seconds  = 0;
int ui_desktop_pattern     = 2;
int ui_desktop_color       = 3;
int ui_clock_12hr          = 0;
int ui_caret_blink_fast    = 0;

static void control_panel_on_click(int id, app_mouse_t *ev) {
    int cx = ev->cx, cy = ev->cy, cw = ev->cw, ch = ev->ch;
    int mx = ev->mx, my = ev->my;
    int pad = 5;
    int col1w = (cw * 55) / 100;
    int col2x = cx + col1w;
    int col2w = cw - col1w;
    int lx = cx + pad;
    int ly_pat = cy + pad + 12;
    int pw = (col1w - pad * 2 - 8) / 2;
    int ph = 18;
    int sq, ly_col, ly_sys;
    int rx2, rblink_y, r12_y;
    (void)ch;
    (void)id;

    if (pw < 20) pw = 20;

    if (mx >= lx && mx < lx + pw && my >= ly_pat && my < ly_pat + ph) {
        ui_desktop_pattern = 2;
        desktop_needs_full_blit = 1;
        return;
    }
    if (mx >= lx + pw + 6 && mx < lx + pw + 6 + pw && my >= ly_pat && my < ly_pat + ph) {
        ui_desktop_pattern = 1;
        desktop_needs_full_blit = 1;
        return;
    }

    ly_col = ly_pat + ph + 6 + 10;
    sq = (col1w - pad * 2) / 8;
    if (sq < 8) sq = 8;
    {
        int i;
        for (i = 0; i < 8; i++) {
            int sx = lx + i * (sq + 1);

if (mx >= sx && mx < sx + sq && my >= ly_col && my < ly_col + sq - 1) {
    ui_desktop_color = i;           

    desktop_needs_full_blit = 1;
    return;
}
        }
    }

    ly_sys = ly_col + sq + 4 + 5 + 10; 

    if (mx >= lx + 4 && mx < lx + 15 && my >= ly_sys + 4 && my < ly_sys + 15) {
        ui_show_scrollbar_grid ^= 1;
        desktop_needs_full_blit = 1;
        return;
    }
    if (mx >= lx + 4 && mx < lx + 15 && my >= ly_sys + 18 && my < ly_sys + 29) {
        calc_default_scientific ^= 1;
        calc_set_scientific_all(calc_default_scientific);
        return;
    }
    if (mx >= lx + 4 && mx < lx + 15 && my >= ly_sys + 32 && my < ly_sys + 43) {
        ui_clock_show_seconds ^= 1;
        desktop_needs_full_blit = 1;
        return;
    }

    rx2 = col2x + pad;
    rblink_y = cy + pad + 13;
    {
        int sx = rx2 + 2;
        int fx = col2x + col2w - 25;
        if (mx >= sx - 2 && mx < sx + 12 && my >= rblink_y - 2 && my < rblink_y + 10) {
            ui_caret_blink_fast = 0;
            desktop_needs_full_blit = 1;
            return;
        }
        if (mx >= fx - 2 && mx < fx + 12 && my >= rblink_y - 2 && my < rblink_y + 10) {
            ui_caret_blink_fast = 1;
            desktop_needs_full_blit = 1;
            return;
        }
    }

    r12_y = cy + pad + 44 + 12 + 11;
    if (mx >= rx2 - 2 && mx < rx2 + 10 && my >= r12_y - 2 && my < r12_y + 10) {
        ui_clock_12hr = 1;
        desktop_needs_full_blit = 1;
        return;
    }
    if (mx >= rx2 + 38 && mx < rx2 + 52 && my >= r12_y - 2 && my < r12_y + 10) {
        ui_clock_12hr = 0;
        desktop_needs_full_blit = 1;
        return;
    }
}

static void draw(int id, int cx, int cy, int cw, int ch) {
    app_control_panel_draw_client(id, cx, cy, cw, ch);
}

const app_desc_t app_control_panel_desc = {
    .kind          = APP_control_panel,
    .default_title = "Control Panel",
    .def_x = 55, .def_y = 30, .def_w = 213, .def_h = 150,
    .draw          = draw,
    .on_click      = control_panel_on_click,
};
