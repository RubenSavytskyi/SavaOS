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

static void open_about(void) {
}

void kernel_main(void) {

#if SAVAOS_GFX_DEMO
    savaos_gfx_irq_demo();
#endif

    vga_init();
    kb_init();
    timer_init();

    gui_init();

    fs_init();

    app_terminal_init();
    app_notepad_init();
    app_calc_init();
    app_filemanager_init();

    gui_add_icon(1, "Terminal",  '\x10', app_terminal_open);
    gui_add_icon(1, "Notepad",   '\xFE', app_notepad_open);
    gui_add_icon(1, "Calc",      '\xF0', app_calc_open);
    gui_add_icon(1, "Files",     '\x1A', app_filemanager_open);
    gui_add_icon(1, "About",     '\x01', open_about);

    gui_main_loop();
}
