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
    build/pci.o        \
    build/rtl8139.o    \
    build/net.o        \
    build/sv_gfx.o     \
    build/sv_desktop.o \
    build/gfx_demo.o   \
    build/rtc.o        \
    build/timer.o      \
    build/gui.o        \
    build/string.o     \
    build/fs.o         \
    build/kernel.o     \
    build/app_about.o       \
    build/app_calc.o        \
    build/app_disk.o        \
    build/app_notepad.o     \
    build/app_puzzle.o      \
    build/app_pong.o        \
    build/app_control_panel.o \
    build/app_terminal.o    \
    build/app_browser.o     \
    build/app_trash.o       \
    build/app_registry.o

.PHONY: all clean run run-grub run-fs run-gtk

all: $(ISO)
	@echo ""
	@echo "  ╔══════════════════════════════════╗"
	@echo "  ║                                  ║"
	@echo "  ║   SavaOS built successfully!     ║"
	@echo "  ║                                  ║"
	@echo "  ╚══════════════════════════════════╝"
	@echo ""
	@ls -lh $(ISO)

$(BOOT): tools/mkboot.py | build
	$(PYTHON) tools/mkboot.py $(BOOT)

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

build/app_terminal.o: apps/app_terminal.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/kernel.o: kernel/kernel.c | build
	$(CC) $(CFLAGS) -c -o $@ $< 

build/app_about.o: apps/app_about.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_calc.o: apps/app_calc.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_disk.o: apps/app_disk.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_notepad.o: apps/app_notepad.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_puzzle.o: apps/app_puzzle.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_pong.o: apps/app_pong.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_control_panel.o: apps/app_control_panel.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<
 
build/app_trash.o: apps/app_trash.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/app_registry.o: kernel/app_registry.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

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

build/pci.o: kernel/pci.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/rtl8139.o: kernel/rtl8139.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/net.o: kernel/net.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/app_browser.o: apps/app_browser.c | build
	$(CC) $(CFLAGS) -Iapps -c -o $@ $<

build/sv_gfx.o: kernel/sv_gfx.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/sv_desktop.o: kernel/sv_desktop.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/keyboard.o: kernel/keyboard.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(KERNEL_ELF_OBJS) kernel/kernel.ld | build
	$(LD) $(LDFLAGS_ELF) -o $(KERNEL_ELF) $(KERNEL_ELF_OBJS)
	@echo "Kernel(ELF) size: $$(wc -c < $(KERNEL_ELF)) bytes"

$(ISO): $(KERNEL_ELF)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/savaos.elf
	$(PYTHON) -c "open(r'$(ISO_DIR)/boot/grub/grub.cfg','w',encoding='utf-8').write('set timeout=0\\nset default=0\\nmenuentry \"SavaOS\" {\\n  multiboot /boot/savaos.elf\\n  boot\\n}\\n')"
	grub-mkrescue -o $(ISO) $(ISO_DIR)

$(KERNEL): $(KERNEL_OBJS) kernel/kernel.ld | build
	$(LD) $(LDFLAGS) -o $(KERNEL) $(KERNEL_OBJS)
	@echo "Kernel size: $$(wc -c < $(KERNEL)) bytes"

$(IMG): $(BOOT) $(KERNEL)
	$(PYTHON) tools/mkimg.py $(BOOT) $(KERNEL) $(IMG)

build:
	mkdir -p build

clean:
	rm -rf build $(IMG) $(ISO)

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -drive if=ide,unit=0,file=disk.img,format=raw,media=disk -boot d \
		-m 32M \
		-netdev user,id=net0 -device rtl8139,netdev=net0 \
		-object filter-dump,id=f0,netdev=net0,file=/tmp/net.pcap \
		-vga std \
		-no-reboot \
		-serial stdio \
		-display gtk 2>/dev/null || \
	qemu-system-i386 -cdrom $(ISO) -drive if=ide,unit=0,file=disk.img,format=raw,media=disk -boot d \
		-m 32M \
		-netdev user,id=net0 -device rtl8139,netdev=net0 \
		-object filter-dump,id=f0,netdev=net0,file=/tmp/net.pcap \
		-vga std \
		-no-reboot \
		-serial stdio \
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