#include "keyboard.h"
#include "types.h"

#define IRQ_RAW_SZ 128

static const char sc_ascii[128] = {
  0,
  0,
  '1',
  '2',
  '3',
  '4',
  '5',
  '6',
  '7',
  '8',
  '9',
  '0',
  '-',
  '=',
  '\b',
  '\t',
  'q',
  'w',
  'e',
  'r',
  't',
  'y',
  'u',
  'i',
  'o',
  'p',
  '[',
  ']',
  '\r',
  0,
  'a',
  's',
  'd',
  'f',
  'g',
  'h',
  'j',
  'k',
  'l',
  ';',
  '\'',
  '`',
  0,
  '\\',
  'z',
  'x',
  'c',
  'v',
  'b',
  'n',
  'm',
  ',',
  '.',
  '/',
  0,
  '*',
  0,
  ' ',
  0,

 0,0,0,0,0,0,0,0,0,0,
  0,
  0,
  '7',
  '8',
  '9',
  '-',
  '4',
  '5',
  '6',
  '+',
  '1',
  '2',
  '3',
  '0',
  '.',
  0,0,0,
  0,
  0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char sc_ascii_shift[128] = {
  0,
  0,
  '!',
  '@',
  '#',
  '$',
  '%',
  '^',
  '&',
  '*',
  '(',
  ')',
  '_',
  '+',
  '\b',
  '\t',
  'Q',
  'W',
  'E',
  'R',
  'T',
  'Y',
  'U',
  'I',
  'O',
  'P',
  '{',
  '}',
  '\r',
  0,
  'A',
  'S',
  'D',
  'F',
  'G',
  'H',
  'J',
  'K',
  'L',
  ':',
  '"',
  '~',
  0,
  '|',
  'Z',
  'X',
  'C',
  'V',
  'B',
  'N',
  'M',
  '<',
  '>',
  '?',
  0,
  '*',
  0,
  ' ',
  0,
  0,0,0,0,0,0,0,0,0,0,
  0,
  0,
  0,
  0,
  0,
  '-',
  0,
  0,
  0,
  '+',
  0,
  0,
  0,
  0,
  0,
  0,0,0,
  0,
  0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static u8  kb_buf[KB_BUFFER_SIZE];
static int kb_head = 0, kb_tail = 0;
static int shift = 0, ctrl = 0, alt = 0, caps = 0, extended = 0;

static volatile u8  irq_raw[IRQ_RAW_SZ];
static volatile int irq_raw_h = 0, irq_raw_t = 0;

static int kb_use_irq;

static void kb_enqueue(u8 c) {
    int next = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next != kb_tail) { kb_buf[kb_head] = c; kb_head = next; }
}

void irq_handler_keyboard(void) {
    u8 sc = inb(KB_DATA_PORT);
    int n = (irq_raw_h + 1) % IRQ_RAW_SZ;
    if (n != irq_raw_t) {
        irq_raw[irq_raw_h] = sc;
        irq_raw_h = n;
    }
}

static void kb_handle_scancode(u8 sc) {
    if (sc == 0xE0) { extended = 1; return; }

    int released = sc & 0x80;
    sc &= 0x7F;

    if (extended) {
        extended = 0;
        if (!released) {
            switch (sc) {
                case 0x48: kb_enqueue(KEY_UP);    break;
                case 0x50: kb_enqueue(KEY_DOWN);  break;
                case 0x4B: kb_enqueue(KEY_LEFT);  break;
                case 0x4D: kb_enqueue(KEY_RIGHT); break;
                case 0x47: kb_enqueue(KEY_HOME);  break;
                case 0x4F: kb_enqueue(KEY_END);   break;
                case 0x49: kb_enqueue(KEY_PGUP);  break;
                case 0x51: kb_enqueue(KEY_PGDN);  break;
                case 0x53: kb_enqueue(KEY_DELETE);break;
            }
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift = !released; return; }
    if (sc == 0x1D) { ctrl  = !released; return; }
    if (sc == 0x38) { alt   = !released; return; }
    if (sc == 0x3A && !released) { caps ^= 1; return; }

    if (!released) {
        if (sc >= 0x3B && sc <= 0x44) {
            kb_enqueue((u8)(KEY_F1 + (sc - 0x3B)));
            return;
        }
        if (sc == 0x57) { kb_enqueue(KEY_F11); return; }
        if (sc == 0x58) { kb_enqueue(KEY_F12); return; }
        if (sc == 0x01) { kb_enqueue(KEY_ESC); return; }

        if (sc < 128) {
            char c = shift ? sc_ascii_shift[sc] : sc_ascii[sc];
            if (c) {

                if (caps) {
                    if (c >= 'a' && c <= 'z') c -= 32;
                    else if (c >= 'A' && c <= 'Z') c += 32;
                }

                if (ctrl && c >= 'a' && c <= 'z') c -= 96;
                if (ctrl && c >= 'A' && c <= 'Z') c -= 64;
                kb_enqueue((u8)c);
            }
        }
    }
}

static void kb_drain_irq_raw(void) {
    while (irq_raw_t != irq_raw_h) {
        u8 sc = irq_raw[irq_raw_t];
        irq_raw_t = (irq_raw_t + 1) % IRQ_RAW_SZ;
        kb_handle_scancode(sc);
    }
}

void kb_init(void) {
    kb_head = kb_tail = 0;
    shift = ctrl = alt = caps = extended = 0;
    irq_raw_h = irq_raw_t = 0;
    kb_use_irq = 0;
}

void kb_set_irq_mode(int on) {
    kb_use_irq = on ? 1 : 0;
    irq_raw_h = irq_raw_t = 0;
}

static void kb_process(void) {
    if (kb_use_irq) {
        kb_drain_irq_raw();
        return;
    }
    while (inb(KB_STATUS_PORT) & 0x01) {
        u8 sc = inb(KB_DATA_PORT);
        kb_handle_scancode(sc);
    }
}

int kb_available(void) {
    kb_process();
    return kb_head != kb_tail;
}

int kb_poll(void) {
    kb_process();
    if (kb_head == kb_tail) return 0;
    u8 c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}

char kb_getchar(void) {
    int c;
    while (!(c = kb_poll())) {
        __asm__ volatile ("pause");
    }
    return (char)c;
}
