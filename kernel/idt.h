#ifndef IDT_H
#define IDT_H

#include "types.h"


struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  type_attr; 
    u16 offset_high;
} __attribute__((packed));


struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

#define IDT_GATE_INT32 0x8E

void idt_set_gate(u8 index, u32 handler_addr, u16 code_selector, u8 flags);
void idt_install(void);


void idt_load(const struct idt_ptr *ptr);

#endif
