#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

extern volatile s32 mouse_x;
extern volatile s32 mouse_y;
extern volatile u8  mouse_buttons;

void mouse_init(void);

void mouse_poll_packets(void);

void irq_handler_mouse(void);

void mouse_cursor_update(void);

void mouse_cursor_paint_after_full_redraw(void);

void mouse_cursor_draw_to_buffer(void);

#endif
