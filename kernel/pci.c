#include "pci.h"
#include "types.h"

#define PCI_CMD_IO_SPACE   0x0001u
#define PCI_CMD_BUS_MASTER 0x0004u

static u32 pci_cfg_read(u8 bus, u8 slot, u8 func, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
               ((u32)func << 8) | ((u32)off & 0xFCu);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_cfg_write32(u8 bus, u8 slot, u8 func, u8 off, u32 val) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
               ((u32)func << 8) | ((u32)off & 0xFCu);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

u16 pci_find_rtl8139_io(void) {
    u16 bus;
    u8 dev, func;

    
    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            u32 hdr_c = pci_cfg_read((u8)bus, dev, 0, 0x0Cu);
            u8 htype = (u8)((hdr_c >> 16) & 0xFFu);
            int maxf = (htype & 0x80u) ? 8 : 1;
            for (func = 0; func < maxf; func++) {
                u32 id = pci_cfg_read((u8)bus, dev, func, 0);
                u16 vid, did;
                u32 bar0;
                u16 io;
                u32 cmdl, newcmd;

                if (id == 0xFFFFFFFFu || id == 0)
                    continue;
                vid = (u16)(id & 0xFFFFu);
                did = (u16)(id >> 16);
                if (vid != 0x10ECu || did != 0x8139u)
                    continue;
                bar0 = pci_cfg_read((u8)bus, dev, func, 0x10);
                if (!(bar0 & 1u))
                    continue;
                io = (u16)(bar0 & 0xFFFCu);
                if (io == 0)
                    continue;

                
                cmdl = pci_cfg_read((u8)bus, dev, func, 0x04);
                newcmd = cmdl | (PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER);
                if (newcmd != cmdl)
                    pci_cfg_write32((u8)bus, dev, func, 0x04, newcmd);

                return io;
            }
        }
    }
    return 0;
}
