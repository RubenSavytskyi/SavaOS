#include "gui.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "types.h"
#include "string.h"

static Window   wins[MAX_WINDOWS];
static int      n_wins = 0;
static int      active_win = -1;
static DesktopIcon icons[MAX_ICONS];
static int      n_icons = 0;

static int      start_menu_open = 0;
static int      focus_icon = -1;

static void draw_desktop(void);
static void draw_taskbar(void);
static void draw_start_menu(void);
static void draw_window_chrome(int wid);
static void handle_key(int key);

static int slen(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}
static void scopy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max-1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int sncmp(const char* a, const char* b, int n) __attribute__((unused));
static int sncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

void gui_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) wins[i].flags = 0;
    n_wins = 0; active_win = -1;
    start_menu_open = 0; focus_icon = -1;
}

int gui_open_window(int x, int y, int w, int h, const char* title,
                    void (*draw)(int), void (*key)(int, int),
                    void (*on_close)(int)) {
    int wid = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!(wins[i].flags & WF_VISIBLE)) { wid = i; break; }
    }
    if (wid < 0) return -1;

    Window* win = &wins[wid];
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->flags = WF_VISIBLE | WF_MOVABLE;
    scopy(win->title, title, MAX_TITLE);
    win->draw = draw;
    win->keydown = key;
    win->on_close = on_close;
    win->scroll_y = 0;
    win->cursor_x = 0; win->cursor_y = 0;
    win->data = (void*)0;
    if (n_wins < wid + 1) n_wins = wid + 1;

    gui_set_active(wid);
    gui_redraw();
    return wid;
}

void gui_close_window(int wid) {
    if (wid < 0 || wid >= MAX_WINDOWS) return;
    if (wins[wid].on_close) wins[wid].on_close(wid);
    wins[wid].flags = 0;

    active_win = -1;
    for (int i = MAX_WINDOWS-1; i >= 0; i--) {
        if (wins[i].flags & WF_VISIBLE) { active_win = i; break; }
    }
    gui_redraw();
}

void gui_set_active(int wid) {
    if (active_win >= 0) wins[active_win].flags &= ~WF_ACTIVE;
    active_win = wid;
    if (wid >= 0) wins[wid].flags |= WF_ACTIVE;
}

static void draw_raised_box(int x, int y, int w, int h, u8 face_color) __attribute__((unused));
static void draw_raised_box(int x, int y, int w, int h, u8 face_color) {
    u8 hi  = MAKE_COLOR(COLOR_WHITE,      face_color >> 4);
    u8 lo  = MAKE_COLOR(COLOR_DARK_GREY,  face_color >> 4);
    u8 mid = face_color;

    for (int i = x; i < x+w; i++) vga_putchar_at(i, y,   '\xDF', hi);
    for (int i = y; i < y+h; i++) vga_putchar_at(x, i,   '\xDD', hi);

    for (int i = x; i < x+w; i++) vga_putchar_at(i, y+h-1, '\xDC', lo);
    for (int i = y; i < y+h; i++) vga_putchar_at(x+w-1, i, '\xDE', lo);

    vga_fill_rect(x+1, y+1, w-2, h-2, ' ', mid);
}

static void draw_titlebar(int x, int y, int w, int active) {
    u8 c1 = active ? MAKE_COLOR(COLOR_WHITE, COLOR_BLUE)
                   : MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_DARK_GREY);
    u8 c2 = active ? MAKE_COLOR(COLOR_LIGHT_BLUE, COLOR_BLUE)
                   : MAKE_COLOR(COLOR_DARK_GREY,  COLOR_DARK_GREY);

    for (int i = x+1; i < x+w-1; i++) {
        u8 c = ((i - x) % 3 == 0) ? c2 : c1;
        vga_putchar_at(i, y, ' ', c);
    }
}

static void draw_window_chrome(int wid) {
    Window* w = &wins[wid];
    if (!(w->flags & WF_VISIBLE)) return;

    int x = w->x, y = w->y, wd = w->w, ht = w->h;
    int active = (wid == active_win);

    u8 border = MAKE_COLOR(COLOR_DARK_GREY, COLOR_LIGHT_GREY);
    u8 border2 = MAKE_COLOR(COLOR_WHITE, COLOR_LIGHT_GREY);

    vga_fill_rect(x, y, wd, ht, ' ', MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY));

    for (int i = x; i < x+wd; i++) vga_putchar_at(i, y, '\xDF', border2);

    for (int i = y+1; i < y+ht-1; i++) vga_putchar_at(x, i, '\xDD', border2);

    for (int i = x; i < x+wd; i++) vga_putchar_at(i, y+ht-1, '\xDC', border);

    for (int i = y+1; i < y+ht-1; i++) vga_putchar_at(x+wd-1, i, '\xDE', border);

    vga_putchar_at(x+1, y+1, '\xDA', border);
    vga_putchar_at(x+wd-2, y+1, '\xBF', border);
    vga_putchar_at(x+1, y+ht-2, '\xC0', border);
    vga_putchar_at(x+wd-2, y+ht-2, '\xD9', border);
    for (int i = x+2; i < x+wd-2; i++) {
        vga_putchar_at(i, y+1, '\xC4', border);
        vga_putchar_at(i, y+ht-2, '\xC4', border);
    }
    for (int i = y+2; i < y+ht-2; i++) {
        vga_putchar_at(x+1, i, '\xB3', border);
        vga_putchar_at(x+wd-2, i, '\xB3', border);
    }

    draw_titlebar(x+2, y+2, wd-4, active);

    int tlen = slen(w->title);
    int tx = x + 3;
    u8 tc = active ? MAKE_COLOR(COLOR_WHITE, COLOR_BLUE)
                   : MAKE_COLOR(COLOR_WHITE, COLOR_DARK_GREY);
    vga_putchar_at(tx, y+2, ' ', tc);
    vga_putstr_at(tx+1, y+2, w->title, tc);
    int tend = tx + 1 + tlen;

    for (int i = tend; i < x+wd-6; i++) vga_putchar_at(i, y+2, ' ', tc);

    u8 btn = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    vga_putchar_at(x+wd-5, y+2, '[', btn);
    vga_putchar_at(x+wd-4, y+2, 'x', MAKE_COLOR(COLOR_RED, COLOR_LIGHT_GREY));
    vga_putchar_at(x+wd-3, y+2, ']', btn);

    vga_fill_rect(x+2, y+3, wd-4, ht-5, ' ',
                  MAKE_COLOR(COLOR_BLACK, COLOR_WHITE));

    if (w->draw) w->draw(wid);
}

static void draw_desktop(void) {

    u8 desk = MAKE_COLOR(COLOR_CYAN, COLOR_CYAN);
    vga_fill_rect(0, 0, VGA_WIDTH, VGA_HEIGHT - 2, '\xB2', desk);

    vga_putstr_at(VGA_WIDTH/2 - 4, 0, "SavaOS", MAKE_COLOR(COLOR_WHITE, COLOR_CYAN));

    for (int i = 0; i < n_icons; i++) {
        int iy = 2 + i * 3;
        u8 ib = MAKE_COLOR(COLOR_YELLOW, COLOR_CYAN);
        vga_putchar_at(icons[i].x, iy, icons[i].icon, ib);

        u8 il = (focus_icon == i)
            ? MAKE_COLOR(COLOR_WHITE, COLOR_BLUE)
            : MAKE_COLOR(COLOR_WHITE, COLOR_CYAN);
        int llen = slen(icons[i].label);
        int lx = icons[i].x - (llen - 1)/2;
        if (lx < 0) lx = 0;
        vga_putstr_at(lx, iy+1, icons[i].label, il);
    }
}

static const char* start_items[] = {
    " Programs    ",
    " Documents   ",
    " Settings    ",
    " ----------- ",
    " Shutdown    ",
};
#define START_ITEMS 5
static int start_sel = 0;

static void draw_start_menu(void) {
    if (!start_menu_open) return;
    int sx = 0, sy = VGA_HEIGHT - 2 - START_ITEMS - 2;
    int sw = 15, sh = START_ITEMS + 2;

    vga_fill_rect(sx, sy, sw, sh, ' ', MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY));

    u8 hi = MAKE_COLOR(COLOR_WHITE, COLOR_LIGHT_GREY);
    u8 lo = MAKE_COLOR(COLOR_DARK_GREY, COLOR_LIGHT_GREY);
    for (int i = sx; i < sx+sw; i++) vga_putchar_at(i, sy, '\xDF', hi);
    for (int i = sy; i < sy+sh; i++) vga_putchar_at(sx, i, '\xDD', hi);
    for (int i = sx; i < sx+sw; i++) vga_putchar_at(i, sy+sh-1, '\xDC', lo);
    for (int i = sy; i < sy+sh; i++) vga_putchar_at(sx+sw-1, i, '\xDE', lo);

    vga_fill_rect(sx+1, sy+1, 1, sh-2, ' ',
                  MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    vga_putstr_at(sx+1, sy + sh/2, "N", MAKE_COLOR(COLOR_YELLOW, COLOR_BLUE));

    for (int i = 0; i < START_ITEMS; i++) {
        u8 c = (i == start_sel)
            ? MAKE_COLOR(COLOR_WHITE, COLOR_BLUE)
            : MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
        vga_putstr_at(sx+2, sy+1+i, start_items[i], c);
    }
}

static u32 clock_ticks = 0;
static int clock_h = 9, clock_m = 0, clock_s = 0;

static void update_clock(void) {
    u32 t = timer_ticks();
    if (t != clock_ticks) {
        clock_ticks = t;

        u32 total_s = t / 100;
        clock_s = total_s % 60;
        clock_m = (total_s / 60) % 60;
        clock_h = (9 + total_s / 3600) % 24;
    }
}

static void draw_clock(void) {
    update_clock();
    char buf[10];

    buf[0] = '0' + clock_h / 10; buf[1] = '0' + clock_h % 10;
    buf[2] = ':';
    buf[3] = '0' + clock_m / 10; buf[4] = '0' + clock_m % 10;
    buf[5] = ':';
    buf[6] = '0' + clock_s / 10; buf[7] = '0' + clock_s % 10;
    buf[8] = 0;
    u8 cc = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    vga_fill_rect(VGA_WIDTH - 10, VGA_HEIGHT-2, 10, 1, ' ', cc);
    vga_putstr_at(VGA_WIDTH - 9, VGA_HEIGHT-2, buf, cc);
}

static void draw_taskbar(void) {
    u8 tb = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    u8 tb2 = MAKE_COLOR(COLOR_WHITE, COLOR_LIGHT_GREY);

    vga_fill_rect(0, VGA_HEIGHT-2, VGA_WIDTH, 1, ' ', tb);

    for (int i = 0; i < VGA_WIDTH; i++) vga_putchar_at(i, VGA_HEIGHT-2, '\xDF', tb2);
    vga_fill_rect(0, VGA_HEIGHT-2, VGA_WIDTH, 1, ' ', tb);

    u8 sb = MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);
    vga_putchar_at(0, VGA_HEIGHT-1, '[', sb);
    vga_putstr_at(1, VGA_HEIGHT-1, "\x10Start", MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY));
    vga_putchar_at(7, VGA_HEIGHT-1, ']', sb);

    int bx = 9;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].flags & WF_VISIBLE) {
            u8 wc = (i == active_win)
                ? MAKE_COLOR(COLOR_WHITE, COLOR_DARK_GREY)
                : MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_GREY);

            vga_putchar_at(bx, VGA_HEIGHT-1, '[', wc);
            char tbuf[11]; int tlen = slen(wins[i].title);
            for (int j = 0; j < 9; j++) tbuf[j] = j < tlen ? wins[i].title[j] : ' ';
            tbuf[9] = 0;
            vga_putstr_at(bx+1, VGA_HEIGHT-1, tbuf, wc);
            vga_putchar_at(bx+10, VGA_HEIGHT-1, ']', wc);
            bx += 12;
            if (bx > VGA_WIDTH - 12) break;
        }
    }

    draw_clock();
}

void gui_redraw(void) {
    draw_desktop();
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].flags & WF_VISIBLE)
            draw_window_chrome(i);
    }
    if (start_menu_open) draw_start_menu();
    draw_taskbar();
}

void gui_redraw_window(int wid) {
    if (wid < 0 || wid >= MAX_WINDOWS) return;
    if (!(wins[wid].flags & WF_VISIBLE)) return;
    draw_window_chrome(wid);
    if (start_menu_open) draw_start_menu();
    draw_taskbar();
}

static int win_inner_x(int wid) { return wins[wid].x + 2; }
static int win_inner_y(int wid) { return wins[wid].y + 3; }
static int win_inner_w(int wid) { return wins[wid].w - 4; }
static int win_inner_h(int wid) { return wins[wid].h - 5; }

void win_putchar(int wid, int x, int y, char c, u8 color) {
    vga_putchar_at(win_inner_x(wid)+x, win_inner_y(wid)+y, c, color);
}
void win_putstr(int wid, int x, int y, const char* s, u8 color) {
    vga_putstr_at(win_inner_x(wid)+x, win_inner_y(wid)+y, s, color);
}
void win_fill(int wid, int x, int y, int w, int h, char c, u8 color) {
    vga_fill_rect(win_inner_x(wid)+x, win_inner_y(wid)+y, w, h, c, color);
}
void win_hline(int wid, int x, int y, int w, u8 color) {
    vga_draw_hline(win_inner_x(wid)+x, win_inner_y(wid)+y, w, color);
}
void win_get_size(int wid, int* w, int* h) {
    *w = win_inner_w(wid);
    *h = win_inner_h(wid);
}
int win_get_scroll(int wid) { return wins[wid].scroll_y; }
void win_set_scroll(int wid, int s) { wins[wid].scroll_y = s; }
void win_scroll(int wid) {
    vga_scroll_region(win_inner_x(wid), win_inner_y(wid),
                      win_inner_w(wid), win_inner_h(wid), 1);
}

void gui_add_icon(int x, const char* label, char icon, void (*open)(void)) {
    if (n_icons >= MAX_ICONS) return;
    icons[n_icons].x = x;
    icons[n_icons].icon = icon;
    icons[n_icons].open = open;
    scopy(icons[n_icons].label, label, 12);
    n_icons++;
}

static void start_menu_key(int key) {
    if (key == KEY_UP) { if (start_sel > 0) start_sel--; }
    else if (key == KEY_DOWN) { if (start_sel < START_ITEMS-1) start_sel++; }
    else if (key == KEY_ENTER) {
        start_menu_open = 0;
        if (start_sel == START_ITEMS-1) {

            vga_clear(MAKE_COLOR(COLOR_BLACK, COLOR_BLACK));
            vga_putstr_at(30, 12, "It is now safe to turn off", MAKE_COLOR(COLOR_WHITE, COLOR_BLACK));
            vga_putstr_at(35, 13, "your computer.", MAKE_COLOR(COLOR_WHITE, COLOR_BLACK));
            __asm__ volatile ("cli; hlt");
        }
    }
    else if (key == KEY_ESC) { start_menu_open = 0; }
}

static void handle_key(int key) {
    if (start_menu_open) {
        start_menu_key(key);
        gui_redraw();
        return;
    }

    if (key == KEY_F1) {

        if (n_icons > 0) {
            focus_icon = (focus_icon + 1) % n_icons;
            gui_redraw();
        }
        return;
    }
    if (key == KEY_ENTER && focus_icon >= 0 && active_win < 0) {
        if (icons[focus_icon].open) icons[focus_icon].open();
        return;
    }

    if (key == KEY_TAB) {
        int next = (active_win + 1) % MAX_WINDOWS;
        int start = next;
        do {
            if (wins[next].flags & WF_VISIBLE) { gui_set_active(next); break; }
            next = (next + 1) % MAX_WINDOWS;
        } while (next != start);
        gui_redraw();
        return;
    }

    if (key == 4 && active_win >= 0) {
        gui_close_window(active_win);
        return;
    }

    if (active_win >= 0 && wins[active_win].keydown)
        wins[active_win].keydown(active_win, (int)(unsigned char)key);
}

void gui_main_loop(void) {
    u32 last_draw = 0;
    gui_redraw();

    while (1) {

        int key = kb_poll();
        if (key) {

            if (key == 0x1D || (key == KEY_ESC)) {
                start_menu_open = !start_menu_open;
                gui_redraw();
            } else {
                handle_key((char)key);
            }
        }

        u32 t = timer_ticks();
        if (t - last_draw >= 50) {
            last_draw = t;
            draw_taskbar();
            if (start_menu_open) draw_start_menu();
        }

        __asm__ volatile ("pause");
    }
}
