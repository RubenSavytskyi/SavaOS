OUTPUT_FORMAT(elf32-i386)
ENTRY(kernel_entry)

PHDRS
{
    load PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x00100000;

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

        . = ALIGN(16);
        stack_bottom = .;
        . += 0x4000;
        stack_top = .;

        __bss_end = .;
    } :load

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
