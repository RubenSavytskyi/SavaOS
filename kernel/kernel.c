#include "../kernel/types.h"
#include "../kernel/gfx_demo.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include "../kernel/gui.h"
#include "../kernel/fs.h"
#include "../kernel/string.h"
#include "../kernel/sv_desktop.h"
#include "../apps/apps.h"


static void boot_splash(void) {
    u8 bg  = MAKE_COLOR(COLOR_CYAN, COLOR_BLUE);
    u8 txt = MAKE_COLOR(COLOR_WHITE, COLOR_BLUE);
    u8 bar = MAKE_COLOR(COLOR_BLUE,  COLOR_CYAN); (void)bar;

    vga_clear(bg);

    
    int lx = 25, ly = 7;
    vga_fill_rect(lx, ly, 30, 7, ' ', MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));

    
    for (int i = lx; i < lx+30; i++) vga_putchar_at(i, ly,   '\xDC', MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_BLUE));
    for (int i = lx; i < lx+30; i++) vga_putchar_at(i, ly+6, '\xDF', MAKE_COLOR(COLOR_DARK_GREY,  COLOR_BLUE));
    for (int i = ly; i < ly+7;  i++) vga_putchar_at(lx,    i, '\xDD', MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_BLUE));
    for (int i = ly; i < ly+7;  i++) vga_putchar_at(lx+29, i, '\xDE', MAKE_COLOR(COLOR_DARK_GREY,  COLOR_BLUE));

    
    u8 title_c = MAKE_COLOR(COLOR_YELLOW, COLOR_BLUE);
    vga_putstr_at(lx+6,  ly+1, " _   _                  ", txt);
    vga_putstr_at(lx+5,  ly+2, "| \\ | | __ _ _ __   ___ ", txt);
    vga_putstr_at(lx+5,  ly+3, "|  \\| |/ _` | '_ \\ / _ \\", txt);
    vga_putstr_at(lx+5,  ly+4, "| |\\  | (_| | | | | (_) |", txt);
    vga_putstr_at(lx+5,  ly+5, "|_| \\_|\\__,_|_| |_|\\___/ ", title_c);

    vga_putstr_at(lx+8, ly+1, "SavaOS", MAKE_COLOR(COLOR_YELLOW, COLOR_BLUE));
    vga_putstr_at(lx+6, ly+2, "Version 0.1.0", txt);
    vga_putstr_at(lx+4, ly+3, "32-bit Protected Mode OS", txt);
    vga_putstr_at(lx+6, ly+4, "Windows 95/98 style", MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_BLUE));
    vga_putstr_at(lx+3, ly+5, "Copyright (C) 2024 SavaOS", txt);

    
    vga_putstr_at(28, 16, "Starting SavaOS...", txt);
    int bx = 22, bw = 36;
    vga_putchar_at(bx-1, 17, '[', MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));
    vga_putchar_at(bx+bw, 17, ']', MAKE_COLOR(COLOR_WHITE, COLOR_BLUE));

    
    for (int i = 0; i < bw; i++) {
        vga_putchar_at(bx+i, 17, '\xDB', MAKE_COLOR(COLOR_CYAN, COLOR_BLUE));
        
        for (volatile int d = 0; d < 200000; d++) {}
    }

    vga_putstr_at(30, 19, "Please wait...", MAKE_COLOR(COLOR_LIGHT_GREY, COLOR_BLUE));

    
    for (volatile int d = 0; d < 5000000; d++) {}
}


static void about_draw(int wid) {
    u8 t = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    u8 b = MAKE_COLOR(COLOR_BLUE,  COLOR_WHITE);
    win_putstr(wid, 2, 0, "SavaOS", b);
    win_putstr(wid, 2, 1, "Version 0.1.0 (Build 001)", t);
    win_putstr(wid, 2, 2, "32-bit Protected Mode OS", t);
    win_putstr(wid, 2, 3, "Inspired by Windows 95/98", t);
    win_putstr(wid, 2, 5, "CPU:  Intel i386+", t);
    win_putstr(wid, 2, 6, "RAM:  4 MB", t);
    win_putstr(wid, 2, 7, "Video: VGA 80x25 text mode", t);
    win_putstr(wid, 2, 9, "Press ESC to close", MAKE_COLOR(COLOR_DARK_GREY, COLOR_WHITE));
}

static void about_key(int wid, int key) {
    if (key == KEY_ESC) gui_close_window(wid);
}

static void open_about(void) {
    gui_open_window(20, 5, 42, 14, "About SavaOS",
                    about_draw, about_key, (void*)0);
}


void kernel_main(void) {
    
#if SAVAOS_GFX_DEMO
    savaos_gfx_irq_demo();
#endif

    
    vga_init();
    kb_init();
    timer_init();

    
    boot_splash();

    
    fs_init();
    gui_init();

    
    app_terminal_init();
    app_notepad_init();
    app_calc_init();
    app_filemanager_init();

    
    gui_add_icon(1, "Terminal",  '\x10', app_terminal_open);   
    gui_add_icon(1, "Notepad",   '\xFE', app_notepad_open);    
    gui_add_icon(1, "Calc",      '\xF0', app_calc_open);       
    gui_add_icon(1, "Files",     '\x1A', app_filemanager_open);
    gui_add_icon(1, "About",     '\x01', open_about);          

    
    
    int wfd = gui_open_window(14, 3, 52, 14, "Welcome to SavaOS",
        (void (*)(int))0, (void (*)(int,int))0, (void*)0);
    
    if (wfd >= 0) {
        u8 t = MAKE_COLOR(COLOR_BLACK,      COLOR_WHITE);
        u8 b = MAKE_COLOR(COLOR_DARK_GREY,  COLOR_WHITE);
        u8 h = MAKE_COLOR(COLOR_BLUE,       COLOR_WHITE);
        win_putstr(wfd, 2,  0, "Welcome to SavaOS v0.1", h);
        win_putstr(wfd, 2,  2, "Getting Started:", t);
        win_putstr(wfd, 4,  3, "F1        - Cycle desktop icons", b);
        win_putstr(wfd, 4,  4, "ENTER     - Open selected icon", b);
        win_putstr(wfd, 4,  5, "TAB       - Switch windows", b);
        win_putstr(wfd, 4,  6, "ESC       - Start menu", b);
        win_putstr(wfd, 4,  7, "Ctrl+D    - Close window", b);
        win_putstr(wfd, 2,  9, "Click on taskbar buttons to", t);
        win_putstr(wfd, 2, 10, "switch between open windows.", t);
        win_putstr(wfd, 2, 12, "Press ESC to open Start menu", MAKE_COLOR(COLOR_DARK_GREY, COLOR_WHITE));
    }

    
    gui_main_loop();
}
