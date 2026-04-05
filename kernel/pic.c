#include "pic.h"

void pic_remap(u8 master_vector_base, u8 slave_vector_base) {
    u8 a1 = inb(PIC1_DATA);
    u8 a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, master_vector_base);
    io_wait();
    outb(PIC2_DATA, slave_vector_base);
    io_wait();

    outb(PIC1_DATA, 4);
    io_wait();

    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void pic_mask_master(u8 mask) {
    outb(PIC1_DATA, mask);
}

void pic_mask_slave(u8 mask) {
    outb(PIC2_DATA, mask);
}

void pic_eoi_master(void) {
    outb(PIC1_CMD, 0x20);
}

void pic_eoi_slave(void) {
    outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
