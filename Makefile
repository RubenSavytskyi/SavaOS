CC      = gcc
AS      = as
LD      = ld
OBJCOPY = objcopy
PYTHON  = python3

CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
          -nostdlib -nostdinc -O2 -Wall -Wextra \
          -Ikernel -Iapps
CFLAGS += -DSAVAOS_GFX_DEMO=1

LDFLAGS = -m elf_i386 --oformat binary -T kernel/kernel.ld -nostdlib

LDFLAGS_ELF = -m elf_i386 -T kernel/kernel.ld -nostdlib -n

IMG     = savaos.img
BOOT    = build/boot.bin
KERNEL  = build/kernel.bin

ISO     = savaos.iso
ISO_DIR = build/iso
KERNEL_ELF = build/kernel.elf

KERNEL_OBJS = \
    build/entry.o    \
    build/vga.o      \
    build/keyboard.o \
    build/timer.o    \
    build/gui.o      \
    build/string.o   \
    build/fs.o       \
    build/kernel.o   \
    build/terminal.o \
    build/apps.o     \
    build/notepad.o

KERNEL_ELF_OBJS = \
    build/boot.o       \
    build/multiboot.o  \
    build/isr.o        \
    build/pic.o        \
    build/idt.o        \
    build/irq.o        \
    build/vga.o        \
    build/keyboard.o   \
    build/mouse.o      \
    build/ata.o        \
    build/fat32.o      \
    build/sv_gfx.o  \
    build/sv_desktop.o \
    build/gfx_demo.o   \
    build/rtc.o        \
    build/timer.o      \
    build/gui.o        \
    build/string.o     \
    build/fs.o         \
    build/kernel.o     \
    build/terminal.o   \
    build/apps.o       \
    build/notepad.o

.PHONY: all clean run run-grub run-fs run-gtk

all: $(ISO)
	@echo ""
	@echo "  ╔══════════════════════════════════╗"
	@echo "  ║   SavaOS built successfully!     ║"
	@echo "  ║                                  ║"
	@echo "  ║  Run:  make run                  ║"
	@echo "  ║                                  ║"
	@echo "  ╚══════════════════════════════════╝"
	@echo ""
	@ls -lh $(ISO)

# ── Bootloader ──────────────────────────────────────
$(BOOT): tools/mkboot.py | build
	$(PYTHON) tools/mkboot.py $(BOOT)

# ── Kernel objects ──────────────────────────────────
build/entry.o: kernel/entry.S | build
	$(AS) --32 -o $@ $<

build/vga.o: kernel/vga.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/gfx_demo.o: kernel/gfx_demo.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/rtc.o: kernel/rtc.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/timer.o: kernel/timer.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/gui.o: kernel/gui.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/string.o: kernel/string.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/fs.o: kernel/fs.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/kernel.o: kernel/kernel.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/terminal.o: kernel/terminal.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/apps.o: apps/apps.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/notepad.o: apps/notepad.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

# ── Multiboot entry + header (kernel_entry) ───────────
build/boot.o: boot/boot.S | build
	$(AS) --32 -o $@ $<

build/multiboot.o: kernel/multiboot.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/isr.o: kernel/isr.S | build
	$(AS) --32 -o $@ $<

build/pic.o: kernel/pic.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/idt.o: kernel/idt.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/irq.o: kernel/irq.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/mouse.o: kernel/mouse.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/ata.o: kernel/ata.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/fat32.o: kernel/fat32.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/sv_gfx.o: kernel/sv_gfx.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/sv_desktop.o: kernel/sv_desktop.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/keyboard.o: kernel/keyboard.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Link kernel.elf (ELF for GRUB Multiboot1) ───────
$(KERNEL_ELF): $(KERNEL_ELF_OBJS) kernel/kernel.ld | build
	$(LD) $(LDFLAGS_ELF) -o $(KERNEL_ELF) $(KERNEL_ELF_OBJS)
	@echo "Kernel(ELF) size: $$(wc -c < $(KERNEL_ELF)) bytes"

# ── Build GRUB ISO ───────────────────────────────────
$(ISO): $(KERNEL_ELF)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/savaos.elf
	$(PYTHON) -c "open(r'$(ISO_DIR)/boot/grub/grub.cfg','w',encoding='utf-8').write('set timeout=0\\nset default=0\\nmenuentry \"SavaOS\" {\\n  multiboot /boot/savaos.elf\\n  boot\\n}\\n')"
	grub-mkrescue -o $(ISO) $(ISO_DIR)

# ── Link kernel ─────────────────────────────────────
$(KERNEL): $(KERNEL_OBJS) kernel/kernel.ld | build
	$(LD) $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)
	@echo "Kernel size: $$(wc -c < $(KERNEL)) bytes"

# ── Assemble disk image ─────────────────────────────
$(IMG): $(BOOT) $(KERNEL)
	$(PYTHON) tools/mkimg.py $(BOOT) $(KERNEL) $(IMG)

build:
	mkdir -p build

clean:
	rm -rf build $(IMG) $(ISO)

# ── Run in QEMU ─────────────────────────────────────
run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -drive if=ide,unit=0,file=disk.img,format=raw,media=disk -boot d \
	    -m 32M \
	    -vga std \
	    -no-reboot \
	    -display gtk 2>/dev/null || \
	qemu-system-i386 -cdrom $(ISO) -drive if=ide,unit=0,file=disk.img,format=raw,media=disk -boot d \
	    -m 32M \
	    -vga std \
	    -no-reboot \
	    -nographic

run-grub: run

run-fs: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -boot d -m 32M -vga std -full-screen -no-reboot

run-gtk: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -drive if=ide,unit=0,file=disk.img,format=raw,media=disk -boot d -m 32M -vga std -display gtk,zoom-to-fit=on -no-reboot

run-sdl: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -drive if=ide,unit=0,file=disk.img,format=raw,media=disk -boot d -m 32M -vga std -display sdl

run-curses: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -boot d -m 32M -vga std -display curses

run-debug: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -boot d -m 32M -vga std -s -S &
	gdb -ex "target remote :1234" -ex "set arch i386"
