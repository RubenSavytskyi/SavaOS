#include "idt.h"

static struct idt_entry idt[256];
static struct idt_ptr    idtp;

void idt_set_gate(u8 index, u32 handler_addr, u16 code_selector, u8 flags) {
    idt[index].offset_low  = (u16)(handler_addr & 0xFFFF);
    idt[index].offset_high = (u16)((handler_addr >> 16) & 0xFFFF);
    idt[index].selector    = code_selector;
    idt[index].zero        = 0;
    idt[index].type_attr   = flags;
}

void idt_install(void) {
    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base  = (u32)&idt;
    idt_load(&idtp);
}
