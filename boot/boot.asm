; SavaOS Bootloader - NASM syntax
; Real mode 16-bit -> Protected mode 32-bit

BITS 16
ORG 0x7C00

KERNEL_SEG  equ 0x0000
KERNEL_OFF  equ 0x1000
KERNEL_SECS equ 63

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Set video mode 80x25 text
    mov ax, 0x0003
    int 0x10

    ; Hide cursor
    mov ax, 0x0100
    mov cx, 0x2607
    int 0x10

    ; Print loading message
    mov si, msg_loading
    call print_str

    ; Load kernel: AH=02 read sectors
    mov ah, 0x02
    mov al, KERNEL_SECS
    mov ch, 0           ; cylinder 0
    mov cl, 2           ; start sector 2
    mov dh, 0           ; head 0
    mov dl, [boot_drive]
    mov bx, KERNEL_OFF
    int 0x13
    jc  disk_error

    mov si, msg_ok
    call print_str

    ; Enable A20
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Switch to protected mode
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump -> flush pipeline, load CS with code segment
    jmp 0x08:0x00001000

disk_error:
    mov si, msg_err
    call print_str
    cli
    hlt

print_str:
    lodsb
    test al, al
    jz   .done
    mov  ah, 0x0E
    mov  bx, 0x0007
    int  0x10
    jmp  print_str
.done:
    ret

; ---- GDT ----
align 8
gdt_start:
    dq 0                        ; null
    ; code: base=0 limit=4GB exec/read 32-bit
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    ; data: base=0 limit=4GB read/write 32-bit
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_drive: db 0x80
msg_loading: db "SavaOS v0.1 - Booting...", 13, 10, 0
msg_ok:      db "Kernel loaded OK", 13, 10, 0
msg_err:     db "DISK ERROR!", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
