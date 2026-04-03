/* SavaOS Kernel Entry - Protected Mode 32-bit
 * Loaded at 0x1000, sets up segments and calls kernel_main
 */
.code32

.global _start
.global kernel_entry

.equ KERNEL_DS, 0x10    /* data segment selector */

_start:
kernel_entry:
    /* Set up data segments */
    mov     $KERNEL_DS, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss

    /* Set up stack at 2MB */
    mov     $0x200000, %esp

    /* Call C kernel */
    call    kernel_main

    /* Halt if kernel returns */
halt:
    cli
    hlt
    jmp     halt
