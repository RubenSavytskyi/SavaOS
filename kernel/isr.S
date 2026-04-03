/* ISR stubs для IRQ1 (клавіатура, вектор 0x21) та IRQ12 (миша, 0x2C) */
.code32

.global irq1_isr
.global irq12_isr
.global idt_load

.extern irq_handler_keyboard
.extern irq_handler_mouse

/* void idt_load(const struct idt_ptr *ptr); */
idt_load:
    mov     4(%esp), %eax
    lidt    (%eax)
    ret

irq1_isr:
    pusha
    push    %ds
    push    %es
    push    %fs
    push    %gs
    mov     $0x10, %eax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    call    irq_handler_keyboard
    mov     $0x20, %al
    out     %al, $0x20
    pop     %gs
    pop     %fs
    pop     %es
    pop     %ds
    popa
    iret

irq12_isr:
    pusha
    push    %ds
    push    %es
    push    %fs
    push    %gs
    mov     $0x10, %eax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    call    irq_handler_mouse
    /* Slave потім master EOI */
    mov     $0x20, %al
    out     %al, $0xA0
    out     %al, $0x20
    pop     %gs
    pop     %fs
    pop     %es
    pop     %ds
    popa
    iret
