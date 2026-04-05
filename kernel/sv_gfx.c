#include "sv_gfx.h"
#include "types.h"

u8 vga13_back_buffer[VGA13_BUF_SIZE];

static int use_back_buffer = 1;

static void vga_write_regs(const u8 *regs) {
    u32 i;

    outb(0x3C2, regs[0]);
    regs++;

    for (i = 0; i < 5; i++) {
        outb(0x3C4, (u8)i);
        outb(0x3C5, regs[0]);
        regs++;
    }

    outb(0x3D4, 0x03);
    outb(0x3D5, (u8)(inb(0x3D5) | 0x80));
    outb(0x3D4, 0x11);
    outb(0x3D5, (u8)(inb(0x3D5) & (u8)~0x80));

    for (i = 0; i < 25; i++) {
        outb(0x3D4, (u8)i);
        outb(0x3D5, regs[0]);
        regs++;
    }

    for (i = 0; i < 9; i++) {
        outb(0x3CE, (u8)i);
        outb(0x3CF, regs[0]);
        regs++;
    }

    for (i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, (u8)i);
        outb(0x3C0, regs[0]);
        regs++;
    }

    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

void vga_wait_vblank(void) {

    while (inb(0x3DA) & 8)
        ;
    while (!(inb(0x3DA) & 8))
        ;
}

static void vga13_gc_minimal(void) {

    outb(0x3C4, 2);
    outb(0x3C5, 0x0F);
    outb(0x3C4, 4);
    outb(0x3C5, 0x0E);

    outb(0x3CE, 4);
    outb(0x3CF, 0x00);
    outb(0x3CE, 1);
    outb(0x3CF, 0x00);
    outb(0x3CE, 5);
    outb(0x3CF, 0x40);
    outb(0x3CE, 6);
    outb(0x3CF, 0x05);
    outb(0x3CE, 8);
    outb(0x3CF, 0xFF);
}

void vga13_init(void) {
    static const u8 mode_320x200x256[] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,

    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

    vga_write_regs(mode_320x200x256);
    vga13_gc_minimal();
}

void vga13_clear_vram(void) {
    volatile u8 *p = (volatile u8 *)VGA13_FB;
    u32 i;
    for (i = 0; i < 64000u; i++)
        p[i] = 0;
}

void vga13_clear_back_buffer(void) {
    u32 i;
    for (i = 0; i < VGA13_BUF_SIZE; i++)
        vga13_back_buffer[i] = 0;
}

void vga13_flip_buffer(void) {
    volatile u8 *fb = (volatile u8 *)VGA13_FB;
    u32 i;

    vga_wait_vblank();

    for (i = 0; i < VGA13_BUF_SIZE; i++) {
        fb[i] = vga13_back_buffer[i];
    }
}

static void vga_set_palette_rgb(u8 index, u8 r6, u8 g6, u8 b6) {
    outb(0x3C8, index);
    outb(0x3C9, r6);
    outb(0x3C9, g6);
    outb(0x3C9, b6);
}

void vga13_init_palette_sv(void) {

    vga_set_palette_rgb(VGA13_BLACK,    0,  0,  0);
    vga_set_palette_rgb(PAL_DARK_GRAY,  22, 22, 22);
    vga_set_palette_rgb(PAL_LIGHT_GRAY, 44, 44, 44);
    vga_set_palette_rgb(VGA13_WHITE,    63, 63, 63);
    vga_set_palette_rgb(PAL_DESKTOP,    50, 50, 50);
    vga_set_palette_rgb(PAL_DITHER_A,   34, 34, 34);
    vga_set_palette_rgb(PAL_DITHER_B,   30, 30, 30);
}

void vga13_put_pixel(int x, int y, u8 color_index) {
    if (x < 0 || y < 0 || x >= VGA13_WIDTH || y >= VGA13_HEIGHT)
        return;
    if (use_back_buffer) {
        vga13_back_buffer[y * VGA13_WIDTH + x] = color_index;
    } else {
        volatile u8 *fb = (volatile u8 *)VGA13_FB;
        fb[y * VGA13_WIDTH + x] = color_index;
    }
}

u8 vga13_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= VGA13_WIDTH || y >= VGA13_HEIGHT)
        return 0;
    if (use_back_buffer) {
        return vga13_back_buffer[y * VGA13_WIDTH + x];
    } else {
        volatile u8 *fb = (volatile u8 *)VGA13_FB;
        return fb[y * VGA13_WIDTH + x];
    }
}

void vga13_fill_rect(int x, int y, int w, int h, u8 c) {
    int xx, yy;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (w <= 0 || h <= 0)
        return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VGA13_WIDTH) x1 = VGA13_WIDTH;
    if (y1 > VGA13_HEIGHT) y1 = VGA13_HEIGHT;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (yy = y0; yy < y1; yy++)
        for (xx = x0; xx < x1; xx++)
            vga13_put_pixel(xx, yy, c);
}

static void hline(int x1, int x2, int y, u8 c) {
    int x;
    if (y < 0 || y >= VGA13_HEIGHT)
        return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (x1 < 0) x1 = 0;
    if (x2 >= VGA13_WIDTH) x2 = VGA13_WIDTH - 1;
    if (x1 > x2)
        return;
    for (x = x1; x <= x2; x++)
        vga13_put_pixel(x, y, c);
}

static void vline(int x, int y1, int y2, u8 c) {
    int y;
    if (x < 0 || x >= VGA13_WIDTH)
        return;
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if (y1 < 0) y1 = 0;
    if (y2 >= VGA13_HEIGHT) y2 = VGA13_HEIGHT - 1;
    if (y1 > y2)
        return;
    for (y = y1; y <= y2; y++)
        vga13_put_pixel(x, y, c);
}

static __attribute__((unused)) void draw_svs_control_box(int bx, int by) {
    hline(bx, bx + SVS_CTRL_SIZE - 1, by, VGA13_BLACK);
    hline(bx, bx + SVS_CTRL_SIZE - 1, by + SVS_CTRL_SIZE - 1, VGA13_BLACK);
    vline(bx, by, by + SVS_CTRL_SIZE - 1, VGA13_BLACK);
    vline(bx + SVS_CTRL_SIZE - 1, by, by + SVS_CTRL_SIZE - 1, VGA13_BLACK);

    vga13_fill_rect(bx + 1, by + 1, SVS_CTRL_SIZE - 2, SVS_CTRL_SIZE - 2, VGA13_WHITE);

    hline(bx + 3, bx + SVS_CTRL_SIZE - 4, by + 3, VGA13_BLACK);
    hline(bx + 3, bx + SVS_CTRL_SIZE - 4, by + SVS_CTRL_SIZE - 4, VGA13_BLACK);
    vline(bx + 3, by + 3, by + SVS_CTRL_SIZE - 4, VGA13_BLACK);
    vline(bx + SVS_CTRL_SIZE - 4, by + 3, by + SVS_CTRL_SIZE - 4, VGA13_BLACK);
}

static void draw_svs_control_box_sized(int bx, int by, int size) {
    if (size < 7)
        size = 7;
    hline(bx, bx + size - 1, by, VGA13_BLACK);
    hline(bx, bx + size - 1, by + size - 1, VGA13_BLACK);
    vline(bx, by, by + size - 1, VGA13_BLACK);
    vline(bx + size - 1, by, by + size - 1, VGA13_BLACK);

    vga13_fill_rect(bx + 1, by + 1, size - 2, size - 2, VGA13_WHITE);

    if (size > 6) {
        hline(bx + 2, bx + size - 3, by + 2, VGA13_BLACK);
        hline(bx + 2, bx + size - 3, by + size - 3, VGA13_BLACK);
        vline(bx + 2, by + 2, by + size - 3, VGA13_BLACK);
        vline(bx + size - 3, by + 2, by + size - 3, VGA13_BLACK);
    }
}

static void draw_title_stripes(int x, int y, int w, int h) {
    int i;
    vga13_fill_rect(x, y, w, h, VGA13_WHITE);
    for (i = 0; i < 5; i++) {
        int ly = y + i * 2;
        if (ly < y + h)
            hline(x, x + w - 1, ly, VGA13_BLACK);
    }
}

void draw_global_menu_bar(const char *menus) {
    int i;
    int tx = 18;
    vga13_fill_rect(0, 0, VGA13_WIDTH, SVS_MENU_BAR_H, VGA13_WHITE);

    hline(5, 11, 3, VGA13_BLACK);
    hline(5, 11, 9, VGA13_BLACK);
    vline(5, 3, 9, VGA13_BLACK);
    vline(11, 3, 9, VGA13_BLACK);
    vga13_fill_rect(6, 4, 5, 5, VGA13_BLACK);

    for (i = 0; menus && menus[i] && tx < VGA13_WIDTH - 8; i++) {
        sv_draw_glyph(tx, 3, menus[i], VGA13_BLACK, VGA13_WHITE);
        tx += 6;
    }
}

void draw_svs_window(int x, int y, int width, int height, const char *title, int is_active) {
    int i;
    int tlen = 0;
    int max_chars = 0;
    int title_box_w;
    int title_box_x;
    int title_px_w;
    int title_h = SVS_TITLE_H - 4;
    int ctrl_size;
    int ctrl_y;
    int ctrl_left_x;
    int ctrl_right_x;
    int title_y = y + 2;

    if (width < 64 || height < SVS_TITLE_H + 10)
        return;

    vga13_fill_rect(x, y, width, height, VGA13_WHITE);

    ctrl_size = title_h - 1;
    if (ctrl_size > SVS_CTRL_SIZE)
        ctrl_size = SVS_CTRL_SIZE;
    if (ctrl_size < 7)
        ctrl_size = 7;
    ctrl_y = title_y + (title_h - ctrl_size) / 2;
    ctrl_left_x = x + 4;
    ctrl_right_x = x + width - 4 - ctrl_size;

    hline(x, x + width - 1, y, VGA13_BLACK);
    hline(x, x + width - 1, y + height - 1, VGA13_BLACK);
    vline(x, y, y + height - 1, VGA13_BLACK);
    vline(x + width - 1, y, y + height - 1, VGA13_BLACK);

    hline(x + 1, x + width - 2, y + 1, VGA13_WHITE);
    hline(x + 1, x + width - 2, y + height - 2, VGA13_WHITE);
    vline(x + 1, y + 1, y + height - 2, VGA13_WHITE);
    vline(x + width - 2, y + 1, y + height - 2, VGA13_WHITE);

    if (is_active) {
        draw_title_stripes(x + 2, title_y, width - 4, title_h);
    } else {

        vga13_fill_rect(x + 2, title_y, width - 4, title_h, PAL_LIGHT_GRAY);
    }

    for (i = 0; title && title[i] && i < 30; i++)
        tlen++;
    max_chars = (width - (SVS_CTRL_SIZE * 2 + 34)) / 6;
    if (max_chars < 1) max_chars = 1;
    if (tlen > max_chars) tlen = max_chars;
    title_px_w = tlen * 6;
    title_box_w = title_px_w + 8;
    if (title_box_w < 20)
        title_box_w = 20;
    if (title_box_w > width - (ctrl_size * 2 + 24))
        title_box_w = width - (ctrl_size * 2 + 24);
    if (title_box_w < 12)
        title_box_w = 12;
    title_box_x = x + (width - title_box_w) / 2;

    vga13_fill_rect(title_box_x, title_y, title_box_w, title_h, VGA13_WHITE);

    for (i = 0; i < tlen && i < 30; i++) {
        int gx = title_box_x + 4 + i * 6;
        if (gx + 5 >= title_box_x + title_box_w - 2)
            break;
        sv_draw_glyph(gx, title_y + 2, title[i], VGA13_BLACK, VGA13_WHITE);
    }

    draw_svs_control_box_sized(ctrl_left_x, ctrl_y, ctrl_size);
    draw_svs_control_box_sized(ctrl_right_x, ctrl_y, ctrl_size);

    vga13_fill_rect(x + 2, y + SVS_TITLE_H, width - 4, height - SVS_TITLE_H - 2, PAL_LIGHT_GRAY);

    hline(x + 2, x + width - 3, y + SVS_TITLE_H, VGA13_BLACK);
}

void sv_draw_glyph(int px, int py, char ch, u8 fg, u8 bg) {

    static const u8 empty[5]        = {0x00,0x00,0x00,0x00,0x00};

    static const u8 letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},
        {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},
        {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},
        {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A},
        {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},
        {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41},
        {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},
        {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E},
        {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},
        {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},
        {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},
        {0x1F,0x20,0x40,0x20,0x1F},
        {0x7F,0x20,0x18,0x20,0x7F},
        {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},
        {0x61,0x51,0x49,0x45,0x43},
    };

    static const u8 lower[26][5] = {
        {0x20,0x54,0x54,0x54,0x78},
        {0x7F,0x48,0x44,0x44,0x38},
        {0x38,0x44,0x44,0x44,0x20},
        {0x38,0x44,0x44,0x48,0x7F},
        {0x38,0x54,0x54,0x54,0x18},
        {0x08,0x7E,0x09,0x01,0x02},
        {0x0C,0x52,0x52,0x52,0x3E},
        {0x7F,0x08,0x04,0x04,0x78},
        {0x00,0x44,0x7D,0x40,0x00},
        {0x20,0x40,0x44,0x3D,0x00},
        {0x7F,0x10,0x28,0x44,0x00},
        {0x00,0x41,0x7F,0x40,0x00},
        {0x7C,0x04,0x18,0x04,0x78},
        {0x7C,0x08,0x04,0x04,0x78},
        {0x38,0x44,0x44,0x44,0x38},
        {0x7C,0x14,0x14,0x14,0x08},
        {0x08,0x14,0x14,0x18,0x7C},
        {0x7C,0x08,0x04,0x04,0x08},
        {0x48,0x54,0x54,0x54,0x20},
        {0x04,0x3F,0x44,0x40,0x20},
        {0x3C,0x40,0x40,0x20,0x7C},
        {0x1C,0x20,0x40,0x20,0x1C},
        {0x3C,0x40,0x30,0x40,0x3C},
        {0x44,0x28,0x10,0x28,0x44},
        {0x0C,0x50,0x50,0x50,0x3C},
        {0x44,0x64,0x54,0x4C,0x44},
    };

    static const u8 digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},
        {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},
        {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},
        {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},
        {0x06,0x49,0x49,0x29,0x1E},
    };

    static const u8 glyph_space[5]     = {0x00,0x00,0x00,0x00,0x00};
    static const u8 glyph_excl[5]      = {0x00,0x00,0x5F,0x00,0x00};
    static const u8 glyph_dquote[5]    = {0x03,0x00,0x03,0x00,0x00};
    static const u8 glyph_hash[5]      = {0x0A,0x1F,0x0A,0x1F,0x0A};
    static const u8 glyph_dollar[5]    = {0x24,0x2A,0x7F,0x2A,0x12};
    static const u8 glyph_percent[5]   = {0x23,0x13,0x08,0x64,0x62};
    static const u8 glyph_amp[5]       = {0x36,0x49,0x56,0x20,0x50};
    static const u8 glyph_squote[5]    = {0x00,0x00,0x03,0x00,0x00};
    static const u8 glyph_lparen[5]    = {0x1C,0x22,0x41,0x00,0x00};
    static const u8 glyph_rparen[5]    = {0x00,0x00,0x41,0x22,0x1C};
    static const u8 glyph_star[5]      = {0x22,0x14,0x08,0x14,0x22};
    static const u8 glyph_plus[5]      = {0x08,0x08,0x7F,0x08,0x08};
    static const u8 glyph_comma[5]     = {0x00,0x60,0x20,0x00,0x00};
    static const u8 glyph_dash[5]      = {0x08,0x08,0x08,0x08,0x08};
    static const u8 glyph_dot[5]       = {0x00,0x00,0x60,0x60,0x00};
    static const u8 glyph_slash[5]     = {0x20,0x10,0x08,0x04,0x02};
    static const u8 glyph_colon[5]     = {0x00,0x36,0x36,0x00,0x00};
    static const u8 glyph_semi[5]      = {0x00,0x36,0x16,0x00,0x00};
    static const u8 glyph_lt[5]        = {0x08,0x14,0x22,0x41,0x00};
    static const u8 glyph_eq[5]        = {0x14,0x14,0x14,0x14,0x14};
    static const u8 glyph_gt[5]        = {0x41,0x22,0x14,0x08,0x00};
    static const u8 glyph_quest[5]     = {0x02, 0x01, 0x51, 0x09, 0x06};
    static const u8 glyph_at[5]        = {0x3E,0x41,0x4D,0x55,0x0E};
    static const u8 glyph_lbracket[5]  = {0x7F,0x41,0x41,0x00,0x00};
    static const u8 glyph_backslash[5] = {0x02,0x04,0x08,0x10,0x20};
    static const u8 glyph_rbracket[5]  = {0x00,0x00,0x41,0x41,0x7F};
    static const u8 glyph_caret[5]     = {0x04,0x02,0x01,0x02,0x04};
    static const u8 glyph_under[5]     = {0x40,0x40,0x40,0x40,0x40};
    static const u8 glyph_btick[5]     = {0x01,0x02,0x00,0x00,0x00};
    static const u8 glyph_lbrace[5]    = {0x08,0x08,0x77,0x00,0x00};
    static const u8 glyph_pipe[5]      = {0x00,0x00,0x7F,0x00,0x00};
    static const u8 glyph_rbrace[5]    = {0x00,0x00,0x77,0x08,0x08};
    static const u8 glyph_tilde[5]     = {0x00,0x04,0x03,0x04,0x00};

    const u8 *g;
    int c, r;

    switch (ch) {
        case ' ':  g = glyph_space;     break;
        case '!':  g = glyph_excl;      break;
        case '"':  g = glyph_dquote;    break;
        case '#':  g = glyph_hash;      break;
        case '$':  g = glyph_dollar;    break;
        case '%':  g = glyph_percent;   break;
        case '&':  g = glyph_amp;       break;
        case '\'': g = glyph_squote;    break;
        case '(':  g = glyph_lparen;    break;
        case ')':  g = glyph_rparen;    break;
        case '*':  g = glyph_star;      break;
        case '+':  g = glyph_plus;      break;
        case ',':  g = glyph_comma;     break;
        case '-':  g = glyph_dash;      break;
        case '.':  g = glyph_dot;       break;
        case '/':  g = glyph_slash;     break;
        case ':':  g = glyph_colon;     break;
        case ';':  g = glyph_semi;      break;
        case '<':  g = glyph_lt;        break;
        case '=':  g = glyph_eq;        break;
        case '>':  g = glyph_gt;        break;
        case '?':  g = glyph_quest;     break;
        case '@':  g = glyph_at;        break;
        case '[':  g = glyph_lbracket;  break;
        case '\\': g = glyph_backslash; break;
        case ']':  g = glyph_rbracket;  break;
        case '^':  g = glyph_caret;     break;
        case '_':  g = glyph_under;     break;
        case '`':  g = glyph_btick;     break;
        case '{':  g = glyph_lbrace;    break;
        case '|':  g = glyph_pipe;      break;
        case '}':  g = glyph_rbrace;    break;
        case '~':  g = glyph_tilde;     break;
        default:
            if (ch >= 'A' && ch <= 'Z') { g = letters[ch - 'A']; break; }
            if (ch >= 'a' && ch <= 'z') { g = lower[ch - 'a'];   break; }
            if (ch >= '0' && ch <= '9') { g = digits[ch - '0'];   break; }
            g = empty;
            break;
    }

    for (c = 0; c < 5; c++) {
        u8 bits = g[c];
        for (r = 0; r < 7; r++) {
            u8 on = (u8)((bits >> r) & 1);
            vga13_put_pixel(px + c, py + r, on ? fg : bg);
        }
    }
}

void vga13_draw_string(int x, int y, const char *text, u8 fg, u8 bg, int inverted) {
    int i;
    u8 f = inverted ? bg : fg;
    u8 b = inverted ? fg : bg;
    for (i = 0; text && text[i]; i++)
        sv_draw_glyph(x + i * 6, y, text[i], f, b);
}

void draw_text_centered(int x, int y, int w, int h, const char *text, u8 fg, u8 bg) {
    int len = 0;
    const char *p;
    int start_x, ty, i;

    for (p = text; *p; p++)
        len++;
    if (len == 0)
        return;
    start_x = x + (w - len * 6) / 2;
    ty = y + (h - 7) / 2;
    if (start_x < x)
        start_x = x;
    for (i = 0; text[i]; i++)
        sv_draw_glyph(start_x + i * 6, ty, text[i], fg, bg);
}

void draw_svs_button(int x, int y, int width, int height, const char *text, int is_pressed) {
    int yy, xx;
    int shadow_off = is_pressed ? 0 : 1;

    if (width < 12 || height < 10)
        return;

    if (!is_pressed)
        vga13_fill_rect(x + shadow_off, y + height, width, 1, PAL_DARK_GRAY);

    vga13_fill_rect(x + 2, y, width - 4, height, VGA13_WHITE);
    vga13_fill_rect(x, y + 2, width, height - 4, VGA13_WHITE);

    for (yy = y + 2; yy <= y + height - 3; yy++) {
        vga13_put_pixel(x, yy, VGA13_BLACK);
        vga13_put_pixel(x + 1, yy, VGA13_BLACK);
        vga13_put_pixel(x + width - 1, yy, VGA13_BLACK);
        vga13_put_pixel(x + width - 2, yy, VGA13_BLACK);
    }
    for (xx = x + 2; xx <= x + width - 3; xx++) {
        vga13_put_pixel(xx, y, VGA13_BLACK);
        vga13_put_pixel(xx, y + 1, VGA13_BLACK);
        vga13_put_pixel(xx, y + height - 1, VGA13_BLACK);
        vga13_put_pixel(xx, y + height - 2, VGA13_BLACK);
    }

    vga13_put_pixel(x + 1, y + 1, VGA13_BLACK);
    vga13_put_pixel(x + width - 2, y + 1, VGA13_BLACK);
    vga13_put_pixel(x + 1, y + height - 2, VGA13_BLACK);
    vga13_put_pixel(x + width - 2, y + height - 2, VGA13_BLACK);

    draw_text_centered(x, y, width, height, text, VGA13_BLACK, VGA13_WHITE);
}
