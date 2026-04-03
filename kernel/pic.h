#ifndef PIC_H
#define PIC_H

#include "types.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01


void pic_remap(u8 master_vector_base, u8 slave_vector_base);

void pic_mask_master(u8 mask);
void pic_mask_slave(u8 mask);


void pic_eoi_master(void);
void pic_eoi_slave(void);

#endif
