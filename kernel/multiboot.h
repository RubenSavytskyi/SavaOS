#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

extern u32 multiboot_magic;
extern u32 multiboot_info_ptr;

#endif
