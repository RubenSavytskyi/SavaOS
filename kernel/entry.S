.code32

.global _start
.global kernel_entry

.equ KERNEL_DS, 0x10

_start:
kernel_entry:
    mov     $KERNEL_DS, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss

    mov     $0x200000, %esp

    call    kernel_main

halt:
    cli
    hlt
    jmp     halt
