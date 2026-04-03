.code32

/*
 * Multiboot1 header for GRUB + 32-bit protected-mode entry.
 *
 * GRUB scans the first 8KiB of the kernel *image* for this header.
 */

/*
 * Тільки bit0 (align modules on page boundaries). Bit2 (video) не використовуємо:
 * GRUB тоді вимагає графічний режим із заголовка і часто падає з
 * "unsupported graphical mode type ...". Режим 320x200x256 вмикаємо в ядрі (VGA 13h).
 */
.set MULTIBOOT_MAGIC,     0x1BADB002
.set MULTIBOOT_FLAGS,     0x00000001
/*
 * Multiboot 1.0: лише magic + flags + checksum = 0 (mod 2^32).
 */
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .multiboot, "a", @progbits
.align 4
.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM

.section .text, "ax", @progbits
.global kernel_entry
.extern kernel_main
.extern stack_top
.extern __bss_start
.extern __bss_end
.extern multiboot_magic
.extern multiboot_info_ptr

kernel_entry:
    /*
     * GRUB: EAX = 0x2BADB002, EBX = multiboot_info.
     * Не писати в multiboot_* до очищення BSS — ці змінні в .bss і обнуляться.
     */
    cli

    /* Load our own flat GDT to avoid depending on GRUB selectors. */
    lgdt    gdt_descriptor
    ljmp    $0x08, $protected_entry

protected_entry:
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss

    /* Initialize stack. */
    mov     $stack_top, %esp
    xor     %ebp, %ebp

    /* Зберегти multiboot EAX/EBX до xor %eax у циклі BSS. */
    pushl   %eax
    pushl   %ebx

    /* Clear .bss (multiboot does not guarantee it). */
    mov     $__bss_start, %edi
    mov     $__bss_end, %esi
    xor     %eax, %eax
.bss_clear_loop:
    /* Stop when edi >= esi (i.e. start pointer reached end). */
    cmp     %esi, %edi
    jae     .bss_clear_done
    movb    %al, (%edi)
    inc     %edi
    jmp     .bss_clear_loop
.bss_clear_done:
    popl    %ebx
    popl    %eax
    movl    %eax, multiboot_magic
    movl    %ebx, multiboot_info_ptr

    call    kernel_main

.hang:
    cli
    hlt
    jmp     .hang

.align 8
gdt_start:
    .long 0x00000000
    .long 0x00000000

    /* Code segment: base=0, limit=4GiB, 32-bit */
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x9A
    .byte 0xCF
    .byte 0x00

    /* Data segment: base=0, limit=4GiB, 32-bit */
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x92
    .byte 0xCF
    .byte 0x00
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start
