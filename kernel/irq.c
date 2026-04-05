#include "irq.h"
#include "idt.h"
#include "pic.h"
#include "types.h"

extern void irq1_isr(void);
extern void irq12_isr(void);

void irq_system_init(void) {
    __asm__ volatile ("cli");

    pic_remap(0x20, 0x28);

    pic_mask_master(0xFF);
    pic_mask_slave(0xFF);

    idt_set_gate(0x21, (u32)irq1_isr,  0x08, IDT_GATE_INT32);
    idt_set_gate(0x2C, (u32)irq12_isr, 0x08, IDT_GATE_INT32);

    idt_install();

    pic_mask_master((u8)(0xFF & ~(1u << 1) & ~(1u << 2)));
    pic_mask_slave((u8)(0xFF & ~(1u << 4)));

    __asm__ volatile ("sti");
}
