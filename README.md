<div align="center">

<img src="docs/screenshots/1.png" width="800" alt="SavaOS Desktop"/>

# SavaOS

**A 32-bit operating system built entirely from scratch — kernel, GUI, drivers, and apps.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/Platform-x86%20%2832--bit%29-orange)
![Language](https://img.shields.io/badge/Language-C%20%2B%20Assembly-green)
![RAM](https://img.shields.io/badge/RAM-32MB-purple)
![Resolution](https://img.shields.io/badge/VGA-320×200%2C%20256%20colors-teal)

[**Run in QEMU**](#quick-start) · [**Screenshots**](#screenshots) · [**Architecture**](#architecture) · [**Add an App**](#adding-an-application)

</div>

---

SavaOS is a bare-metal x86 operating system written in C and Assembly — no Linux, no POSIX, no borrowed kernel. Everything runs directly on hardware: the window manager, the FAT32 driver, the PS/2 mouse, the RTC clock, and the six bundled applications.

> Built to understand how computers *actually* work.

**~17,000 lines of code. Zero dependencies. Boots in QEMU in seconds.**

---

## Screenshots

<div align="center">
<img src="docs/screenshots/1.png" width="400"/> <img src="docs/screenshots/2.png" width="400"/>
<img src="docs/screenshots/3.png" width="400"/> <img src="docs/screenshots/5.png" width="400"/>
</div>

---

## What's Inside

| Layer | What it does |
|---|---|
| **Bootloader** | GRUB-based, loads kernel via Multiboot |
| **Kernel** | Protected mode, IDT, IRQ, PIC, PIT timer |
| **Memory** | Manual heap management, no MMU |
| **Graphics** | VGA Mode 13h, double-buffered, custom 256-color palette |
| **Input** | PS/2 keyboard + mouse drivers |
| **Storage** | ATA/IDE driver + FAT32 read/write + RAM-based VFS |
| **Desktop** | Window manager, menu bar, cursor, click handling |
| **Applications** | Notepad, Calculator, Terminal, File Manager, Puzzle, Control Panel |

---

## Applications

### 📝 Notepad
Full text editor with undo/redo, copy/paste, find, text selection, `Ctrl+S` save to FAT32 disk.

### 🧮 Calculator
Standard and scientific modes. Keyboard input. Handles decimals, negatives, backspace, and clear entry.

### 💻 Terminal
Custom shell with 21 built-in commands — `ls`, `cat`, `touch`, `rm`, `fatls`, `diskinfo`, `fsstate`, and more.

### 📁 File Manager
FAT32 browser: navigate directories, open files, create folders, copy/paste, delete. Full keyboard navigation.

### 🧩 Puzzle
Classic 15-tile sliding puzzle. Shuffles on start, tracks move count, arrow key controls.

### ⚙️ Control Panel
Desktop pattern picker, color chooser, clock settings, system options. *(partially implemented)*

---

## Quick Start

### Prerequisites

```bash
# Ubuntu / Debian / WSL
sudo apt-get install gcc-multilib binutils grub-common grub-pc-bin xorriso qemu-system-x86
```

### Build & Run

```bash
git clone https://github.com/RubenSavytskyi/SavaOS.git
cd SavaOS
make
make run
```

That's it. QEMU opens with a running desktop.

### All Build Targets

| Command | Description |
|---|---|
| `make` | Build `savaos.iso` |
| `make run` | Run in QEMU (auto-detects GTK/SDL) |
| `make run-gtk` | GTK display, zoom-to-fit |
| `make run-sdl` | SDL display |
| `make run-fs` | Fullscreen, no disk image |
| `make run-debug` | GDB stub on port 1234 |
| `make clean` | Remove old build |

---

## Architecture

```
┌───────────────────────────────────────────┐
│              User Applications            │
│    Notepad · Calc · Terminal · Files      │
├───────────────────────────────────────────┤
│             Desktop Manager               │
│      Window manager · UI rendering        │
├───────────────────────────────────────────┤
│              File System                  │
│       RAM-based VFS  ·  FAT32 driver      │
├───────────────────────────────────────────┤
│             Device Drivers                │
│   VGA · PS/2 KB/Mouse · RTC · PIT Timer  │
├───────────────────────────────────────────┤
│              Kernel Core                  │
│    IDT · IRQ · PIC · Memory · I/O ports   │
├───────────────────────────────────────────┤
│              x86 Hardware                 │
└───────────────────────────────────────────┘
```

### Memory Layout

```
0x00000000–0x000003FF  Real-mode IVT (unused in PM)
0x00000400–0x00007BFF  BIOS data area (reserved)
0x00007C00–0x00007DFF  Boot sector (512 bytes)
0x00100000–0x002FFFFF  Kernel code + data (2 MB, loaded by GRUB)
0xA0000–0xAFFFF        VGA graphics buffer (64 KB)
0xB8000                VGA text buffer
```

---

## Project Structure

```
savaos/
├── kernel/
│   ├── entry.S          # Assembly entry point
│   ├── isr.S            # Interrupt service routines
│   ├── kernel.c         # Kernel init
│   ├── vga.c/h          # VGA Mode 13h driver
│   ├── sv_gfx.c/h       # High-level drawing primitives
│   ├── sv_desktop.c/h   # Desktop + window manager
│   ├── gui.c/h          # Window system
│   ├── fs.c/h           # Virtual File System
│   ├── fat32.c/h        # FAT32 driver
│   ├── ata.c/h          # ATA/IDE disk driver
│   ├── keyboard.c/h     # PS/2 keyboard
│   ├── mouse.c/h        # PS/2 mouse
│   ├── timer.c/h        # PIT timer
│   ├── rtc.c/h          # Real-time clock
│   ├── idt.c/h          # Interrupt descriptor table
│   └── ...
├── boot/boot.S          # Boot sector
├── apps/                # Application source
├── tools/               # Build utilities (Python)
├── Makefile
└── savaos.iso           # Bootable ISO
```

---

## Adding an Application

Five steps to add a new app to the desktop:

**1. Register the app type** in `kernel/sv_desktop.h`:
```c
typedef enum { APP_NONE, APP_ABOUT, ..., APP_MYAPP } app_kind_t;
```

**2. Implement the draw function** in `kernel/sv_desktop.c`:
```c
static void draw_client_myapp(int id, int cx, int cy, int cw, int ch) {
    vga13_fill_rect(cx, cy, cw, ch, VGA13_WHITE);
    vga13_draw_string(cx + 10, cy + 10, "Hello!", VGA13_BLACK, VGA13_WHITE, 0);
}
```

**3. Initialize** in `win_open()`:
```c
if (app == APP_MYAPP) myapp_init(id);
```

**4. Wire the menu** in `app_open()`:
```c
case APP_MYAPP: win_open(APP_MYAPP, "My App", 50, 30, 200, 150); break;
```

**5. Add to Makefile**:
```makefile
KERNEL_ELF_OBJS += build/myapp.o
build/myapp.o: kernel/myapp.c | build
	$(CC) $(CFLAGS) -c -o $@ $<
```

---

## Keyboard Shortcuts

### Notepad
`Ctrl+S` Save · `Ctrl+Z` Undo · `Ctrl+Y` Redo · `Ctrl+C/X/V` Copy/Cut/Paste · `Ctrl+A` Select All · `Home/End` Line start/end · `PgUp/PgDn` Scroll

### File Manager
`↑↓` Navigate · `Enter` Open · `Backspace` Parent dir · `Delete` Delete · `Ctrl+C/V` Copy/Paste

### Calculator
`0–9` Digits · `. ,` Decimal · `+ - * /` Operators · `Enter` Calculate · `Esc` Clear

### Puzzle
`↑ ↓ ← →` Move tiles · `Enter` New game

### Terminal Commands
```
help  clear  echo  about  time  ver  uname
ls  cat  touch  rm
fatls  fatmkdir  fatrm  fatrmdir
diskinfo  fsstate  mounttest  ata test  identifytest
```

---

## Troubleshooting

**`gcc: unrecognized option '-m32'`**
```bash
sudo apt-get install gcc-multilib
```

**Black screen in QEMU**
```bash
# Try a different VGA mode:
qemu-system-i386 -cdrom savaos.iso -vga cirrus
```

**Disk errors / `disk.img` not found**
```bash
qemu-img create -f raw disk.img 64M
```

**GTK/SDL display not working**
```bash
sudo apt-get install libsdl2-dev
make run-curses  # fallback: text mode
```

**GDB won't connect**
```bash
make run-debug           # starts QEMU with -s -S
# in another terminal:
gdb -ex "target remote :1234"
```

---

## Roadmap

- [ ] Control Panel — full settings implementation
- [ ] Sound driver (PC speaker / Sound Blaster)
- [ ] TCP/IP networking stack
- [ ] Window resizing and minimization
- [ ] Serial port debugging
- [ ] ext2 / NTFS read-only support
- [ ] **DOOM port** 🎮
- [ ] Image viewer
- [ ] More terminal commands

---

## Technical Specs

| Property | Value |
|---|---|
| Architecture | x86, 32-bit protected mode |
| C code | ~15,000 lines |
| Assembly | ~2,000 lines |
| Graphics | VGA Mode 13h — 320×200, 256 colors |
| Input | PS/2 keyboard + mouse |
| Storage | ATA/IDE + FAT32 + RAM VFS |
| RAM required | 32 MB |
| Boot | GRUB Multiboot |

---

## Contributing

Pull requests welcome. Most-needed areas:

- **Drivers** — USB, SATA, network cards
- **Applications** — image viewer, music player
- **Bug fixes** — especially edge cases in FAT32 and window manager
- **Documentation** — inline comments, architecture notes

---

## License

MIT — see [LICENSE](LICENSE).

---

## Acknowledgments

- [OSDev Wiki](https://wiki.osdev.org) — the bible
- Bran's Kernel Development Tutorial
- QEMU and GNU toolchain maintainers

---

<div align="center">

Made by [Ruben Savytskyi](https://github.com/RubenSavytskyi) · ruvimsavitsky@gmail.com

*If this project helped you understand how operating systems work — give it a ⭐*

</div>
