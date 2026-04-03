#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_BUFFER_SIZE  64


#define KEY_NONE    0x00
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_F1      0x84
#define KEY_F2      0x85
#define KEY_F3      0x86
#define KEY_F4      0x87
#define KEY_ESC     0x1B
#define KEY_ENTER   0x0D
#define KEY_BACKSP  0x08
#define KEY_TAB     0x09
#define KEY_DELETE  0x7F
#define KEY_HOME    0x90
#define KEY_END     0x91
#define KEY_PGUP    0x92
#define KEY_PGDN    0x93
#define KEY_SHIFT   0x94

void kb_init(void);
void kb_set_irq_mode(int on); 
void irq_handler_keyboard(void);
char kb_getchar(void);   
int  kb_poll(void);      
int  kb_available(void);

#endif
