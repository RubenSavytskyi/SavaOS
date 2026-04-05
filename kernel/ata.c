#include "ata.h"
#include "string.h"
#include "timer.h"

static void ata_delay(void) {
    int i;
    for (i = 0; i < 15; i++) {
        inb(ATA_PRIMARY_STATUS);
    }
}

static int ata_wait_ready(void) {
    u8 status;
    int timeout = 100000;

    while (timeout-- > 0) {
        status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_STATUS_BSY)) {
            if (status & ATA_STATUS_RDY) {
                return 0;
            }
        }
    }
    return -1;
}

static int ata_wait_drq(void) {
    u8 status;
    int timeout = 100000;

    while (timeout-- > 0) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_DRQ) {
            return 0;
        }
        if (status & ATA_STATUS_ERR) {
            return -1;
        }
    }
    return -1;
}

void ata_init(void) {

    outb(ATA_PRIMARY_CONTROL, 0x04);
    ata_delay();
    outb(ATA_PRIMARY_CONTROL, 0x00);
    ata_delay();

    ata_wait_ready();
}

int ata_read_sectors(u32 lba, u8 count, void *buffer) {
    if (count == 0) return -1;
    if (buffer == 0) return -1;

    {
        u8 *buf = (u8 *)buffer;

        for (int try_slave = 0; try_slave < 2; try_slave++) {
            u8 head_base = try_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;

            outb(ATA_PRIMARY_DRIVE_HEAD, head_base | ATA_LBA_MODE | ((lba >> 24) & 0x0F));
            ata_delay();

            if (ata_wait_ready() != 0) continue;

            outb(ATA_PRIMARY_SECTOR_CNT, count);

            outb(ATA_PRIMARY_LBA_LO, lba & 0xFF);
            outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
            outb(ATA_PRIMARY_LBA_HI, (lba >> 16) & 0xFF);

            outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);
            ata_delay();

            u8 sector_i;
            for (sector_i = 0; sector_i < count; sector_i++) {
                if (ata_wait_drq() != 0) break;

                u16 *wbuf = (u16 *)buf;
                for (int j = 0; j < 256; j++) {
                    wbuf[j] = inw(ATA_PRIMARY_DATA);
                }
                buf += 512;

                ata_delay();
            }

            if (sector_i == count) {
                (void)ata_wait_ready();
                return 0;
            }
        }
    }

    return -1;
}

int ata_write_sectors(u32 lba, u8 count, const void *buffer) {
    if (count == 0) return -1;
    if (buffer == 0) return -1;

    for (int try_slave = 0; try_slave < 2; try_slave++) {
        u8 head_base = try_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;
        const u8 *buf = (const u8 *)buffer;

        outb(ATA_PRIMARY_DRIVE_HEAD, head_base | ATA_LBA_MODE | ((lba >> 24) & 0x0F));
        ata_delay();
        if (ata_wait_ready() != 0) continue;

        outb(ATA_PRIMARY_SECTOR_CNT, count);

        outb(ATA_PRIMARY_LBA_LO, lba & 0xFF);
        outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_PRIMARY_LBA_HI, (lba >> 16) & 0xFF);

        outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
        ata_delay();

        u8 sector_i;
        for (sector_i = 0; sector_i < count; sector_i++) {
            if (ata_wait_drq() != 0) break;

            const u16 *wbuf = (const u16 *)buf;
            for (int j = 0; j < 256; j++) {
                outw(ATA_PRIMARY_DATA, wbuf[j]);
            }
            buf += 512;

            ata_delay();
        }

        if (sector_i == count) {
            (void)ata_wait_ready();

            outb(ATA_PRIMARY_COMMAND, ATA_CMD_FLUSH_CACHE);
            (void)ata_wait_ready();
            return 0;
        }
    }

    return -1;
}

int ata_identify(void) {
    u16 i;

    if (ata_wait_ready() != 0) return -1;

    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    ata_delay();

    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    if (inb(ATA_PRIMARY_STATUS) == 0) return -1;

    if (ata_wait_drq() != 0) return -1;

    for (i = 0; i < 256; i++) {
        (void)inw(ATA_PRIMARY_DATA);
    }

    return 0;
}
