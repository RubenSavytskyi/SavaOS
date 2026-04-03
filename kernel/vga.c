#include "vga.h"
#include "types.h"

static volatile u16* const vga_buf = (u16*)VGA_MEM;

void vga_init(void) {
    
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}


void vga_clear(u8 color) {
    u16 entry = VGA_ENTRY(' ', color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buf[i] = entry;
}

void vga_putchar_at(int x, int y, char c, u8 color) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    vga_buf[y * VGA_WIDTH + x] = VGA_ENTRY(c, color);
}

void vga_putstr_at(int x, int y, const char* s, u8 color) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { y++; cx = x; s++; continue; }
        if (cx >= VGA_WIDTH) { cx = x; y++; }
        if (y >= VGA_HEIGHT) break;
        vga_putchar_at(cx++, y, *s++, color);
    }
}

void vga_fill_rect(int x, int y, int w, int h, char c, u8 color) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            vga_putchar_at(col, row, c, color);
}

void vga_draw_hline(int x, int y, int w, u8 color) {
    for (int i = x; i < x + w; i++)
        vga_putchar_at(i, y, '\xC4', color); 
}

void vga_draw_vline(int x, int y, int h, u8 color) {
    for (int i = y; i < y + h; i++)
        vga_putchar_at(x, i, '\xB3', color); 
}

void vga_scroll_region(int x, int y, int w, int h, int lines) {
    (void)x; (void)w;
    for (int row = y; row < y + h - lines; row++)
        for (int col = x; col < x + w; col++)
            vga_buf[row * VGA_WIDTH + col] = vga_buf[(row + lines) * VGA_WIDTH + col];
    for (int row = y + h - lines; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            vga_buf[row * VGA_WIDTH + col] = VGA_ENTRY(' ', MAKE_COLOR(COLOR_BLACK, COLOR_WHITE));
}

u16 vga_get_char(int x, int y) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return 0;
    return vga_buf[y * VGA_WIDTH + x];
}
