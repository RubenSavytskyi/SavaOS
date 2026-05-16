#include "../kernel/types.h"
#include "../kernel/gfx_demo.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include "../kernel/fs.h"
#include "../kernel/net.h"
#include "../kernel/irq.h"
#include "../kernel/string.h"
#include "../kernel/sv_desktop.h"

void kernel_main(void) {
    

    vga_init();
    kb_init();
    timer_init();
    irq_system_init();
    fs_init();
    (void)net_init();

#if SAVAOS_GFX_DEMO
    savaos_gfx_irq_demo();
#else
    savaos_desktop_run();
#endif
}
