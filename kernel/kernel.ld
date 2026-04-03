/* SavaOS Kernel Linker Script (Multiboot1 / GRUB)
 *
 * GRUB Multiboot1 typically loads the kernel at physical 1MiB.
 * We link at 0x00100000 and place the Multiboot header early.
 *
 * Заголовок Multiboot має бути в перших 8192 байтах *файлу* ELF (див. spec).
 * Якщо ld ставить PT_LOAD з великим p_offset, GRUB не знаходить магію.
 * Прапор -n у Makefile + один LOAD-сегмент з низьким зміщенням у файлі.
 */

OUTPUT_FORMAT(elf32-i386)
ENTRY(kernel_entry)

PHDRS
{
    /* Один сегмент: простий multiboot ELF, низький file offset для .multiboot */
    load PT_LOAD FLAGS(7); /* R+W+X — типово для навчальних ядер */
}

SECTIONS
{
    . = 0x00100000;

    /* Must be within the first 8KiB of the OS image (file). */
    .multiboot : ALIGN(4)
    {
        KEEP(*(.multiboot))
    } :load

    .text : ALIGN(4)
    {
        *(.text .text.*)
    } :load

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
    } :load

    .data : ALIGN(4)
    {
        *(.data .data.*)
    } :load

    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)

        /* Fixed stack reserved in BSS. */
        . = ALIGN(16);
        stack_bottom = .;
        . += 0x4000; /* 16KiB stack */
        stack_top = .;

        __bss_end = .;
    } :load

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
