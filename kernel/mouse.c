#include "mouse.h"
#include "types.h"
#include "sv_gfx.h"

#define PS2_DATA    0x60
#define PS2_CMD     0x64
#define PS2_STATUS  0x64

#define MOUSE_BUF 256
static volatile u8 mouse_packet[3];
static volatile int mouse_idx;
static volatile u8 mouse_buf[MOUSE_BUF];
static volatile int mouse_bh, mouse_bt;

volatile s32 mouse_x = 160;
volatile s32 mouse_y = 100;
volatile u8  mouse_buttons = 0;

static void mouse_process_bytes(void);

static void mouse_buf_put(u8 b) {
    int n = (mouse_bh + 1) % MOUSE_BUF;
    if (n != mouse_bt) {
        mouse_buf[mouse_bh] = b;
        mouse_bh = n;
    }
}

static int ps2_wait_write(void) {
    u32 t = 100000;
    while (t-- && (inb(PS2_STATUS) & 2))
        ;
    return t > 0;
}

static int ps2_wait_read(void) {
    u32 t = 100000;
    while (t-- && !(inb(PS2_STATUS) & 1))
        ;
    return t > 0;
}

static void ps2_write(u8 port, u8 val) {
    ps2_wait_write();
    outb(port, val);
}

static u8 ps2_read(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

static void ps2_cmd(u8 cmd) {
    ps2_write(PS2_CMD, cmd);
}

static void mouse_cmd(u8 b) {
    ps2_write(PS2_CMD, 0xD4);
    ps2_wait_write();
    outb(PS2_DATA, b);

    ps2_read();
}

void mouse_init(void) {
    mouse_idx = 0;
    mouse_bh = mouse_bt = 0;

    ps2_cmd(0xA8);

    ps2_cmd(0x20);
    u8 status = ps2_read();
    status |= 2;
    ps2_cmd(0x60);
    ps2_write(PS2_DATA, status);

    mouse_cmd(0xF4);
}

void irq_handler_mouse(void) {
    u8 st = inb(PS2_STATUS);

    if ((st & 0x21) == 0x21) {
        u8 b = inb(PS2_DATA);
        mouse_buf_put(b);
    }
}

void mouse_poll_packets(void) {
    mouse_process_bytes();
}

static void mouse_process_bytes(void) {
    while (mouse_bt != mouse_bh) {
        u8 b = mouse_buf[mouse_bt];
        mouse_bt = (mouse_bt + 1) % MOUSE_BUF;

        if (mouse_idx == 0) {
            if ((b & 8) == 0)
                continue;
            mouse_packet[0] = b;
            mouse_idx = 1;
        } else {
            mouse_packet[mouse_idx++] = b;
            if (mouse_idx == 3) {
                mouse_idx = 0;
                u8 st = mouse_packet[0];
                s8 dx = (s8)mouse_packet[1];
                s8 dy = (s8)mouse_packet[2];
                mouse_buttons = st & 7;

                if (st & 0xC0)
                    continue;
                mouse_x += dx;

                mouse_y -= dy;
                if (mouse_x < 0)
                    mouse_x = 0;
                if (mouse_y < 0)
                    mouse_y = 0;
                if (mouse_x >= VGA13_WIDTH)
                    mouse_x = VGA13_WIDTH - 1;
                if (mouse_y >= VGA13_HEIGHT)
                    mouse_y = VGA13_HEIGHT - 1;
            }
        }
    }
}

#define CUR_W 8
#define CUR_H 11
#define MOUSE_TRANSPARENT 0xFF

static u8 cursor_save[CUR_W * CUR_H];
static int save_valid;
static int last_cx = -1, last_cy = -1;

static const u8 svsos_cursor[CUR_H][CUR_W] = {
    {2,2,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0},
    {2,1,1,2,0,0,0,0},
    {2,1,1,1,2,0,0,0},
    {2,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,0},
    {2,1,1,1,2,2,0,0},
    {2,1,2,1,1,2,0,0},
    {2,2,0,2,1,2,0,0},
    {2,0,0,2,1,1,2,0},
    {0,0,0,0,2,2,0,0},
};

static void blit_save(int x, int y) {
    int i, j, k = 0;
    for (j = 0; j < CUR_H; j++) {
        for (i = 0; i < CUR_W; i++) {
            int px = x + i, py = y + j;
            if (px >= 0 && px < VGA13_WIDTH && py >= 0 && py < VGA13_HEIGHT)
                cursor_save[k++] = vga13_get_pixel(px, py);
            else
                cursor_save[k++] = MOUSE_TRANSPARENT;
        }
    }
}

static void blit_restore(int x, int y) {
    int i, j, k = 0;
    for (j = 0; j < CUR_H; j++) {
        for (i = 0; i < CUR_W; i++) {
            int px = x + i, py = y + j;
            u8 c = cursor_save[k++];
            if (c == MOUSE_TRANSPARENT)
                continue;
            if (px >= 0 && px < VGA13_WIDTH && py >= 0 && py < VGA13_HEIGHT)
                vga13_put_pixel(px, py, c);
        }
    }
}

static void blit_draw(int x, int y) {
    int i, j;

    for (j = 0; j < CUR_H; j++) {
        for (i = 0; i < CUR_W; i++) {
            int px = x + i, py = y + j;
            if (px < 0 || px >= VGA13_WIDTH || py < 0 || py >= VGA13_HEIGHT) continue;
            if (svsos_cursor[j][i] == 2)
                vga13_put_pixel(px, py, VGA13_WHITE);
        }
    }

    for (j = 0; j < CUR_H; j++) {
        for (i = 0; i < CUR_W; i++) {
            int px = x + i, py = y + j;
            if (px < 0 || px >= VGA13_WIDTH || py < 0 || py >= VGA13_HEIGHT) continue;
            if (svsos_cursor[j][i] == 1)
                vga13_put_pixel(px, py, VGA13_BLACK);
        }
    }
}

void mouse_cursor_update(void) {
    mouse_process_bytes();

    int cx = (int)mouse_x;
    int cy = (int)mouse_y;

    if (cx > VGA13_WIDTH - 1)
        cx = VGA13_WIDTH - 1;
    if (cy > VGA13_HEIGHT - 1)
        cy = VGA13_HEIGHT - 1;

    if (save_valid && last_cx >= 0) {
        blit_restore(last_cx, last_cy);
        save_valid = 0;
    }

    blit_save(cx, cy);
    blit_draw(cx, cy);
    save_valid = 1;
    last_cx = cx;
    last_cy = cy;
}

void mouse_cursor_paint_after_full_redraw(void) {
    int cx, cy;
    mouse_process_bytes();

    save_valid = 0;
    last_cx = -1;

    cx = (int)mouse_x;
    cy = (int)mouse_y;
    if (cx > VGA13_WIDTH - 1)
        cx = VGA13_WIDTH - 1;
    if (cy > VGA13_HEIGHT - 1)
        cy = VGA13_HEIGHT - 1;

    blit_save(cx, cy);
    blit_draw(cx, cy);
    save_valid = 1;
    last_cx = cx;
    last_cy = cy;
}

void mouse_cursor_draw_to_buffer(void) {
    int cx = (int)mouse_x;
    int cy = (int)mouse_y;
    int i, j;

    if (cx > VGA13_WIDTH - 1)
        cx = VGA13_WIDTH - 1;
    if (cy > VGA13_HEIGHT - 1)
        cy = VGA13_HEIGHT - 1;

    for (j = 0; j < CUR_H; j++) {
        for (i = 0; i < CUR_W; i++) {
            int px = cx + i, py = cy + j;
            if (px < 0 || px >= VGA13_WIDTH || py < 0 || py >= VGA13_HEIGHT) continue;
            if (svsos_cursor[j][i] == 2)
                vga13_put_pixel(px, py, VGA13_WHITE);
        }
    }

    for (j = 0; j < CUR_H; j++) {
        for (i = 0; i < CUR_W; i++) {
            int px = cx + i, py = cy + j;
            if (px < 0 || px >= VGA13_WIDTH || py < 0 || py >= VGA13_HEIGHT) continue;
            if (svsos_cursor[j][i] == 1)
                vga13_put_pixel(px, py, VGA13_BLACK);
        }
    }
}
