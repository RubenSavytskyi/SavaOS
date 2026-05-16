#include "fat32.h"
#include "ata.h"
#include "string.h"

static fat32_context_t fat32_ctx;

static u8 sector_buffer[ATA_SECTOR_SIZE];

static u8 sector_buffer2[ATA_SECTOR_SIZE];

static u8 cluster_buffer[4096];

static u16 rd16(const u8* p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}
static u32 rd32(const u8* p) {
    return (u32)p[0] |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void fat32_parse_bpb_from_sector(const u8* sec, fat32_bpb_t* out) {
    if (!sec || !out) return;

    kmemset(out, 0, sizeof(*out));

    out->bytes_per_sector = rd16(sec + 11);
    out->sectors_per_cluster = sec[13];
    out->reserved_sector_count = rd16(sec + 14);
    out->num_fats = sec[16];

    out->total_sectors_16 = rd16(sec + 19);
    out->fat_size_16 = rd16(sec + 22);
    out->total_sectors_32 = rd32(sec + 32);
    out->fat_size_32 = rd32(sec + 36);

    out->root_cluster = rd32(sec + 44);
    out->fs_info_sector = rd16(sec + 48);
    out->backup_boot_sector = rd16(sec + 50);

    out->boot_sector_signature = rd16(sec + 510);

    kmemcpy(out->file_system_type, sec + 82, 8);
}

static u32 fat32_entry_cluster(const fat32_dir_entry_t* e) {
    return ((u32)e->cluster_high << 16) | (u32)e->cluster_low;
}

static void fat32_set_entry_cluster(fat32_dir_entry_t* e, u32 clus) {
    e->cluster_high = (u16)((clus >> 16) & 0xFFFF);
    e->cluster_low  = (u16)(clus & 0xFFFF);
}

static int fat32_parse_name83(const char* name, u8 out_name[8], u8 out_ext[3]) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < 8; i++) out_name[i] = ' ';
    for (int i = 0; i < 3; i++) out_ext[i] = ' ';

    int n = 0;
    int e = 0;
    int in_ext = 0;
    for (int i = 0; name[i]; i++) {
        char ch = name[i];
        if (ch == '/') return -1;
        if (ch == '.') { in_ext = 1; continue; }
        if (ch == ' ') continue;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);

        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
            return -1;
        }
        if (!in_ext) {
            if (n >= 8) return -1;
            out_name[n++] = (u8)ch;
        } else {
            if (e >= 3) return -1;
            out_ext[e++] = (u8)ch;
        }
    }
    if (n == 0) return -1;
    return 0;
}

static int fat32_name83_eq(const fat32_dir_entry_t* e, const u8 name83[8], const u8 ext83[3]) {
    for (int i = 0; i < 8; i++) if (e->name[i] != name83[i]) return 0;
    for (int i = 0; i < 3; i++) if (e->ext[i] != ext83[i]) return 0;
    return 1;
}

int fat32_set_fat_entry(u32 cluster, u32 value) {
    if (!fat32_is_mounted()) return -1;
    if (cluster < 2) return -2;

    value &= 0x0FFFFFFF;

    u32 fat_offset = cluster * 4;
    u32 fat_sector_rel = fat_offset / ATA_SECTOR_SIZE;
    u32 sector_offset = fat_offset % ATA_SECTOR_SIZE;

    for (u8 fat_i = 0; fat_i < fat32_ctx.bpb.num_fats; fat_i++) {
        u32 fat_sector = fat32_ctx.base_lba + fat32_ctx.fat_start_sector +
                         fat_sector_rel + (fat_i * fat32_ctx.sectors_per_fat);

        if (ata_read_sectors(fat_sector, 1, sector_buffer) != 0) return -3;

        if (sector_offset <= (ATA_SECTOR_SIZE - 4)) {
            sector_buffer[sector_offset + 0] = (u8)(value & 0xFF);
            sector_buffer[sector_offset + 1] = (u8)((value >> 8) & 0xFF);
            sector_buffer[sector_offset + 2] = (u8)((value >> 16) & 0xFF);
            sector_buffer[sector_offset + 3] = (u8)((value >> 24) & 0xFF);

            if (ata_write_sectors(fat_sector, 1, sector_buffer) != 0) return -4;
        } else {

            if (ata_read_sectors(fat_sector + 1, 1, sector_buffer2) != 0) return -3;
            sector_buffer[sector_offset + 0] = (u8)(value & 0xFF);
            sector_buffer[sector_offset + 1] = (u8)((value >> 8) & 0xFF);
            sector_buffer2[0] = (u8)((value >> 16) & 0xFF);
            sector_buffer2[1] = (u8)((value >> 24) & 0xFF);
            if (ata_write_sectors(fat_sector, 1, sector_buffer) != 0) return -4;
            if (ata_write_sectors(fat_sector + 1, 1, sector_buffer2) != 0) return -4;
        }
    }
    return 0;
}

u32 fat32_alloc_cluster(void) {
    if (!fat32_is_mounted()) return 0;

    u32 max_cluster = fat32_ctx.total_clusters + 1;
    for (u32 cl = 2; cl <= max_cluster; cl++) {
        u32 next = fat32_get_next_cluster(cl);

        u32 fat_offset = cl * 4;
        u32 fat_sector = fat32_ctx.base_lba + fat32_ctx.fat_start_sector + (fat_offset / ATA_SECTOR_SIZE);
        u32 sector_offset = fat_offset % ATA_SECTOR_SIZE;

        if (ata_read_sectors(fat_sector, 1, sector_buffer) != 0) return 0;
        u32 raw;
        if (sector_offset <= (ATA_SECTOR_SIZE - 4)) {
            raw = *(u32 *)(sector_buffer + sector_offset);
        } else {
            if (ata_read_sectors(fat_sector + 1, 1, sector_buffer2) != 0) return 0;
            u32 b0 = sector_buffer[sector_offset + 0];
            u32 b1 = sector_buffer[sector_offset + 1];
            u32 b2 = sector_buffer2[0];
            u32 b3 = sector_buffer2[1];
            raw = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        }
        raw &= 0x0FFFFFFF;

        if (raw == FAT32_CLUSTER_FREE) {
            if (fat32_set_fat_entry(cl, FAT32_CLUSTER_END) != 0) return 0;

            if (fat32_ctx.free_clusters > 0) fat32_ctx.free_clusters--;
            (void)next;
            return cl;
        }
    }
    return 0;
}

int fat32_free_chain(u32 first_cluster) {
    if (!fat32_is_mounted()) return -1;
    if (first_cluster < 2) return 0;

    u32 cl = first_cluster;
    int steps = 0;
    while (cl >= 2 && cl < FAT32_CLUSTER_END && steps < 4096) {
        steps++;
        u32 next = fat32_get_next_cluster(cl);

        if (fat32_set_fat_entry(cl, FAT32_CLUSTER_FREE) != 0) return -2;
        if (fat32_ctx.free_clusters != 0xFFFFFFFF) fat32_ctx.free_clusters++;
        if (next == FAT32_CLUSTER_END) break;
        cl = next;
    }
    return 0;
}

static int fat32_dir_find_free_slot(u32 dir_cluster, u32* out_cluster, u32* out_index) {
    if (!fat32_is_mounted()) return -1;
    if (!out_cluster || !out_index) return -1;

    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    u32 entries_per_cluster = bytes_per_cluster / sizeof(fat32_dir_entry_t);

    u32 cl = dir_cluster;
    int steps = 0;
    while (cl >= 2 && cl < FAT32_CLUSTER_END && steps < 64) {
        steps++;
        if (fat32_read_cluster(cl, cluster_buffer) != 0) return -2;
        fat32_dir_entry_t* ent = (fat32_dir_entry_t*)cluster_buffer;
        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (ent[i].name[0] == 0x00 || ent[i].name[0] == 0xE5) {
                *out_cluster = cl;
                *out_index = i;
                return 0;
            }
        }
        u32 next = fat32_get_next_cluster(cl);
        if (next == FAT32_CLUSTER_END) break;
        cl = next;
    }

    u32 newc = fat32_alloc_cluster();
    if (newc < 2) return -3;

    if (fat32_set_fat_entry(cl, newc) != 0) return -4;
    if (fat32_set_fat_entry(newc, FAT32_CLUSTER_END) != 0) return -4;

    kmemset(cluster_buffer, 0, bytes_per_cluster);
    if (fat32_write_cluster(newc, cluster_buffer) != 0) return -5;
    *out_cluster = newc;
    *out_index = 0;
    return 0;
}

static int fat32_dir_write_entry_at(u32 dir_cluster, u32 index, const fat32_dir_entry_t* entry) {
    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    u32 entries_per_cluster = bytes_per_cluster / sizeof(fat32_dir_entry_t);
    if (index >= entries_per_cluster) return -1;
    if (fat32_read_cluster(dir_cluster, cluster_buffer) != 0) return -2;
    fat32_dir_entry_t* ent = (fat32_dir_entry_t*)cluster_buffer;
    kmemcpy(&ent[index], entry, sizeof(fat32_dir_entry_t));
    if (fat32_write_cluster(dir_cluster, cluster_buffer) != 0) return -3;
    return 0;
}

static int fat32_dir_find_entry(u32 dir_cluster, const char* name83, u32* out_cluster, u32* out_index, fat32_dir_entry_t* out_entry) {
    u8 n83[8], e83[3];
    if (fat32_parse_name83(name83, n83, e83) != 0) return -1;

    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    u32 entries_per_cluster = bytes_per_cluster / sizeof(fat32_dir_entry_t);

    u32 cl = dir_cluster;
    int steps = 0;
    while (cl >= 2 && cl < FAT32_CLUSTER_END && steps < 64) {
        steps++;
        if (fat32_read_cluster(cl, cluster_buffer) != 0) return -2;
        fat32_dir_entry_t* ent = (fat32_dir_entry_t*)cluster_buffer;
        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (ent[i].name[0] == 0x00) return -3;
            if (ent[i].name[0] == 0xE5 || ent[i].name[0] == 0x05) continue;
            if (ent[i].attributes == FAT32_ATTR_LFN) continue;
            if (fat32_name83_eq(&ent[i], n83, e83)) {
                if (out_cluster) *out_cluster = cl;
                if (out_index) *out_index = i;
                if (out_entry) kmemcpy(out_entry, &ent[i], sizeof(*out_entry));
                return 0;
            }
        }
        u32 next = fat32_get_next_cluster(cl);
        if (next == FAT32_CLUSTER_END) break;
        cl = next;
    }
    return -4;
}

int fat32_mkdir_in_dir(u32 parent_dir_cluster, const char *name83) {
    if (!fat32_is_mounted()) return -1;
    u8 n83[8], e83[3];
    if (fat32_parse_name83(name83, n83, e83) != 0) return -2;

    if (fat32_dir_find_entry(parent_dir_cluster, name83, 0, 0, 0) == 0) return -3;

    u32 new_dir_cluster = fat32_alloc_cluster();
    if (new_dir_cluster < 2) return -4;

    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    kmemset(cluster_buffer, 0, bytes_per_cluster);

    fat32_dir_entry_t dot;
    kmemset(&dot, 0, sizeof(dot));
    for (int i = 0; i < 8; i++) dot.name[i] = ' ';
    for (int i = 0; i < 3; i++) dot.ext[i] = ' ';
    dot.name[0] = '.';
    dot.attributes = FAT32_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&dot, new_dir_cluster);
    dot.file_size = 0;

    fat32_dir_entry_t dotdot;
    kmemset(&dotdot, 0, sizeof(dotdot));
    for (int i = 0; i < 8; i++) dotdot.name[i] = ' ';
    for (int i = 0; i < 3; i++) dotdot.ext[i] = ' ';
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    dotdot.attributes = FAT32_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&dotdot, parent_dir_cluster);
    dotdot.file_size = 0;

    fat32_dir_entry_t* ents = (fat32_dir_entry_t*)cluster_buffer;
    kmemcpy(&ents[0], &dot, sizeof(dot));
    kmemcpy(&ents[1], &dotdot, sizeof(dotdot));
    if (fat32_write_cluster(new_dir_cluster, cluster_buffer) != 0) return -5;

    u32 slot_cluster, slot_index;
    if (fat32_dir_find_free_slot(parent_dir_cluster, &slot_cluster, &slot_index) != 0) return -6;

    fat32_dir_entry_t entry;
    kmemset(&entry, 0, sizeof(entry));
    kmemcpy(entry.name, n83, 8);
    kmemcpy(entry.ext, e83, 3);
    entry.attributes = FAT32_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&entry, new_dir_cluster);
    entry.file_size = 0;

    if (fat32_dir_write_entry_at(slot_cluster, slot_index, &entry) != 0) return -7;
    return 0;
}

int fat32_unlink_in_dir(u32 parent_dir_cluster, const char *name83) {
    if (!fat32_is_mounted()) return -1;
    u32 ent_cluster, ent_index;
    fat32_dir_entry_t entry;
    if (fat32_dir_find_entry(parent_dir_cluster, name83, &ent_cluster, &ent_index, &entry) != 0) return -2;
    if (entry.attributes & FAT32_ATTR_DIRECTORY) return -3;

    u32 first = fat32_entry_cluster(&entry);
    if (first >= 2) (void)fat32_free_chain(first);

    if (fat32_read_cluster(ent_cluster, cluster_buffer) != 0) return -4;
    fat32_dir_entry_t* ents = (fat32_dir_entry_t*)cluster_buffer;
    ents[ent_index].name[0] = 0xE5;
    if (fat32_write_cluster(ent_cluster, cluster_buffer) != 0) return -5;
    return 0;
}

static int fat32_dir_is_empty(u32 dir_cluster) {
    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    u32 entries_per_cluster = bytes_per_cluster / sizeof(fat32_dir_entry_t);
    u32 cl = dir_cluster;
    int steps = 0;
    while (cl >= 2 && cl < FAT32_CLUSTER_END && steps < 64) {
        steps++;
        if (fat32_read_cluster(cl, cluster_buffer) != 0) return -1;
        fat32_dir_entry_t* ent = (fat32_dir_entry_t*)cluster_buffer;
        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (ent[i].name[0] == 0x00) return 1;
            if (ent[i].name[0] == 0xE5 || ent[i].name[0] == 0x05) continue;
            if (ent[i].attributes == FAT32_ATTR_LFN) continue;

            if (ent[i].name[0] == '.' && (ent[i].name[1] == ' ' || ent[i].name[1] == '.')) continue;
            return 0;
        }
        u32 next = fat32_get_next_cluster(cl);
        if (next == FAT32_CLUSTER_END) break;
        cl = next;
    }
    return 1;
}

int fat32_rmdir_empty_in_dir(u32 parent_dir_cluster, const char *name83) {
    if (!fat32_is_mounted()) return -1;
    u32 ent_cluster, ent_index;
    fat32_dir_entry_t entry;
    if (fat32_dir_find_entry(parent_dir_cluster, name83, &ent_cluster, &ent_index, &entry) != 0) return -2;
    if (!(entry.attributes & FAT32_ATTR_DIRECTORY)) return -3;

    u32 dirc = fat32_entry_cluster(&entry);
    int empty = fat32_dir_is_empty(dirc);
    if (empty <= 0) return -4;

    (void)fat32_free_chain(dirc);

    if (fat32_read_cluster(ent_cluster, cluster_buffer) != 0) return -5;
    fat32_dir_entry_t* ents = (fat32_dir_entry_t*)cluster_buffer;
    ents[ent_index].name[0] = 0xE5;
    if (fat32_write_cluster(ent_cluster, cluster_buffer) != 0) return -6;
    return 0;
}

int fat32_write_file_new_in_dir(u32 parent_dir_cluster, const char *name83, const void *data, u32 len) {
    if (!fat32_is_mounted()) return -1;
    if (!data && len) return -2;
    u8 n83[8], e83[3];
    if (fat32_parse_name83(name83, n83, e83) != 0) return -3;

    if (fat32_dir_find_entry(parent_dir_cluster, name83, 0, 0, 0) == 0) return -4;

    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    const u8* src = (const u8*)data;

    u32 first_cluster = 0;
    u32 prev_cluster = 0;
    u32 remaining = len;

    if (len == 0) {
        first_cluster = 0;
    } else {
        while (remaining > 0) {
            u32 cl = fat32_alloc_cluster();
            if (cl < 2) return -5;
            if (!first_cluster) first_cluster = cl;
            if (prev_cluster) {
                if (fat32_set_fat_entry(prev_cluster, cl) != 0) return -6;
            }
            prev_cluster = cl;
            if (fat32_set_fat_entry(cl, FAT32_CLUSTER_END) != 0) return -6;

            u32 chunk = remaining > bytes_per_cluster ? bytes_per_cluster : remaining;
            kmemset(cluster_buffer, 0, bytes_per_cluster);
            if (chunk) kmemcpy(cluster_buffer, src, chunk);
            if (fat32_write_cluster(cl, cluster_buffer) != 0) return -7;
            src += chunk;
            remaining -= chunk;
        }
    }

    u32 slot_cluster, slot_index;
    if (fat32_dir_find_free_slot(parent_dir_cluster, &slot_cluster, &slot_index) != 0) return -8;

    fat32_dir_entry_t entry;
    kmemset(&entry, 0, sizeof(entry));
    kmemcpy(entry.name, n83, 8);
    kmemcpy(entry.ext, e83, 3);
    entry.attributes = FAT32_ATTR_ARCHIVE;
    fat32_set_entry_cluster(&entry, first_cluster);
    entry.file_size = len;

    if (fat32_dir_write_entry_at(slot_cluster, slot_index, &entry) != 0) return -9;
    return 0;
}

int fat32_copy_file_in_dir(u32 src_dir_cluster, const char *src_name83, u32 dst_dir_cluster, const char *dst_name83) {
    if (!fat32_is_mounted()) return -1;
    fat32_dir_entry_t src_entry;
    if (fat32_dir_find_entry(src_dir_cluster, src_name83, 0, 0, &src_entry) != 0) return -2;
    if (src_entry.attributes & FAT32_ATTR_DIRECTORY) return -3;

    u32 size = src_entry.file_size;
    u32 src_cluster = fat32_entry_cluster(&src_entry);
    u32 bytes_per_cluster = (u32)fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;

    u32 first_dst = 0;
    u32 prev_dst = 0;
    u32 remaining = size;
    u32 cur_src = src_cluster;

    while (remaining > 0) {
        if (cur_src < 2 || cur_src >= FAT32_CLUSTER_END) return -4;

        if (fat32_read_cluster(cur_src, cluster_buffer) != 0) return -5;

        u32 dstc = fat32_alloc_cluster();
        if (dstc < 2) return -6;
        if (!first_dst) first_dst = dstc;
        if (prev_dst) {
            if (fat32_set_fat_entry(prev_dst, dstc) != 0) return -7;
        }
        prev_dst = dstc;
        if (fat32_set_fat_entry(dstc, FAT32_CLUSTER_END) != 0) return -7;

        u32 chunk = remaining > bytes_per_cluster ? bytes_per_cluster : remaining;

        if (chunk < bytes_per_cluster) {

            kmemset(cluster_buffer + chunk, 0, bytes_per_cluster - chunk);
        }
        if (fat32_write_cluster(dstc, cluster_buffer) != 0) return -8;

        remaining -= chunk;
        cur_src = fat32_get_next_cluster(cur_src);
    }

    const char* out_name = (dst_name83 && dst_name83[0]) ? dst_name83 : src_name83;
    u8 n83[8], e83[3];
    if (fat32_parse_name83(out_name, n83, e83) != 0) return -9;

    u32 slot_cluster, slot_index;
    if (fat32_dir_find_free_slot(dst_dir_cluster, &slot_cluster, &slot_index) != 0) return -10;

    fat32_dir_entry_t out;
    kmemset(&out, 0, sizeof(out));
    kmemcpy(out.name, n83, 8);
    kmemcpy(out.ext, e83, 3);
    out.attributes = FAT32_ATTR_ARCHIVE;
    fat32_set_entry_cluster(&out, first_dst);
    out.file_size = size;

    if (fat32_dir_write_entry_at(slot_cluster, slot_index, &out) != 0) return -11;
    return 0;
}

int fat32_init(void) {

    kmemset(&fat32_ctx, 0, sizeof(fat32_ctx));

    int ret = fat32_mount();

    if (ret == 0) {
        kprintf("FAT32 filesystem initialized successfully\n");
    } else {
        kprintf("FAT32 filesystem initialization failed with error %d\n", ret);
    }

    return ret;
}

int fat32_mount(void) {

    u32 boot_lba = 0;

    if (ata_read_sectors(0, 1, sector_buffer) != 0) return -1;

    u16 sig = rd16(sector_buffer + 510);
    int is_fat32 = (sig == 0xAA55) && (kmemcmp(sector_buffer + 82, "FAT32", 5) == 0);

    if (is_fat32) {
        fat32_parse_bpb_from_sector(sector_buffer, &fat32_ctx.bpb);
        boot_lba = 0;
    } else {

        struct __attribute__((packed)) mbr_part_t {
            u8  boot_indicator;
            u8  chs_first[3];
            u8  type;
            u8  chs_last[3];
            u32 lba_first;
            u32 sectors_total;
        };
        const struct mbr_part_t* parts = (const struct mbr_part_t*)(sector_buffer + 446);

        int found = 0;
        for (int p = 0; p < 4; p++) {
            u8 t = parts[p].type;
            if (t == 0x0B || t == 0x0C) {
                boot_lba = parts[p].lba_first;
                found = 1;
                break;
            }
        }
        if (!found) return -5;
        if (ata_read_sectors(boot_lba, 1, sector_buffer) != 0) return -1;
        fat32_parse_bpb_from_sector(sector_buffer, &fat32_ctx.bpb);
    }

    if (fat32_ctx.bpb.boot_sector_signature != 0xAA55) return -2;
    if (kmemcmp(fat32_ctx.bpb.file_system_type, "FAT32", 5) != 0) return -3;

    if (fat32_ctx.bpb.root_cluster < 2) fat32_ctx.bpb.root_cluster = 2;

    fat32_ctx.base_lba = boot_lba;
    fat32_ctx.sectors_per_fat = fat32_ctx.bpb.fat_size_32;
    if (fat32_ctx.sectors_per_fat == 0) return -4;

    fat32_ctx.fat_start_sector = fat32_ctx.bpb.reserved_sector_count;
    fat32_ctx.data_start_sector = fat32_ctx.fat_start_sector +
                                   (fat32_ctx.bpb.num_fats * fat32_ctx.sectors_per_fat);

    fat32_ctx.root_dir_sector = fat32_cluster_to_sector(fat32_ctx.bpb.root_cluster);

    {
        u32 total_sectors = (fat32_ctx.bpb.total_sectors_16 != 0)
            ? fat32_ctx.bpb.total_sectors_16
            : fat32_ctx.bpb.total_sectors_32;
        u32 data_sectors = total_sectors - fat32_ctx.data_start_sector;
        fat32_ctx.total_clusters = data_sectors / fat32_ctx.bpb.sectors_per_cluster;
    }

    fat32_ctx.fsinfo_sector = fat32_ctx.bpb.fs_info_sector;
    if (fat32_ctx.fsinfo_sector > 0 && fat32_ctx.fsinfo_sector < fat32_ctx.data_start_sector) {
        fat32_fsinfo_t fsinfo;
        if (ata_read_sectors(fat32_ctx.base_lba + fat32_ctx.fsinfo_sector, 1, &fsinfo) == 0) {
            if (fsinfo.lead_signature == 0x41615252 &&
                fsinfo.structure_signature == 0x61417272 &&
                fsinfo.trail_signature == 0xAA550000 &&
                fsinfo.free_cluster_count != 0xFFFFFFFF) {
                fat32_ctx.free_clusters = fsinfo.free_cluster_count;
            }
        }
    }

    fat32_ctx.mounted = 1;
    return 0;
}

void fat32_unmount(void) {
    kmemset(&fat32_ctx, 0, sizeof(fat32_ctx));
}

int fat32_is_mounted(void) {
    return fat32_ctx.mounted;
}

u32 fat32_cluster_to_sector(u32 cluster) {
    if (cluster < 2) {
        return fat32_ctx.base_lba + fat32_ctx.data_start_sector;
    }
    return fat32_ctx.base_lba + fat32_ctx.data_start_sector +
           ((cluster - 2) * fat32_ctx.bpb.sectors_per_cluster);
}

u32 fat32_get_next_cluster(u32 cluster) {
    u32 fat_offset;
    u32 fat_sector;
    u32 sector_offset;
    u32 next_cluster;
    u32 b0, b1, b2, b3;

    if (cluster < 2) {
        return FAT32_CLUSTER_END;
    }

    fat_offset = cluster * 4;
    fat_sector = fat32_ctx.base_lba + fat32_ctx.fat_start_sector + (fat_offset / ATA_SECTOR_SIZE);
    sector_offset = fat_offset % ATA_SECTOR_SIZE;

    if (ata_read_sectors(fat_sector, 1, sector_buffer) != 0) {
        return FAT32_CLUSTER_BAD;
    }

    if (sector_offset <= (ATA_SECTOR_SIZE - 4)) {
        next_cluster = *(u32 *)(sector_buffer + sector_offset);
    } else {
        if (ata_read_sectors(fat_sector + 1, 1, sector_buffer2) != 0) {
            return FAT32_CLUSTER_BAD;
        }
        b0 = sector_buffer[sector_offset + 0];
        b1 = sector_buffer[sector_offset + 1];
        b2 = sector_buffer2[0];
        b3 = sector_buffer2[1];
        next_cluster = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    next_cluster &= 0x0FFFFFFF;

    if (next_cluster == FAT32_CLUSTER_BAD) {
        return FAT32_CLUSTER_END;
    }

    if (next_cluster >= FAT32_CLUSTER_EOC_MIN) {
        return FAT32_CLUSTER_END;
    }

    return next_cluster;
}

static void fat32_short_entry_to_str(const fat32_dir_entry_t* e, char* out, int out_cap) {
    int j = 0;
    if (!e || !out || out_cap <= 1) return;

    for (int i = 0; i < 8 && e->name[i] != ' '; i++) {
        if (j < out_cap - 1) out[j++] = (char)e->name[i];
    }

    if (e->ext[0] != ' ') {
        if (j < out_cap - 1) out[j++] = '.';
        for (int i = 0; i < 3 && e->ext[i] != ' '; i++) {
            if (j < out_cap - 1) out[j++] = (char)e->ext[i];
        }
    }

    out[j] = 0;
}

int fat32_debug_dump(char* out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    out[0] = 0;

    const fat32_bpb_t* b = &fat32_ctx.bpb;
    u32 root_clus = b->root_cluster;
    u32 cluster2 = 2;

    char tmp[256];
    int off = 0;

    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                      "mounted=%d base_lba=%u\n",
                      (int)fat32_ctx.mounted, (unsigned)fat32_ctx.base_lba);

    ksnprintf(tmp, sizeof(tmp),
                  "base_lba=%u root_cluster=%u spc=%u rsvd=%u fats=%u fat_size=%u tot_cl=%u\n",
                  fat32_ctx.base_lba, root_clus,
                  (unsigned)b->sectors_per_cluster,
                  (unsigned)b->reserved_sector_count,
                  (unsigned)b->num_fats,
                  (unsigned)b->fat_size_32,
                  (unsigned)fat32_ctx.total_clusters);
    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
    if (off >= out_cap - 1) return 0;

    ksnprintf(tmp, sizeof(tmp),
                  "fat_start_rel=%u data_start_rel=%u root_dir_sector_abs=%u fsinfo=%u\n",
                  (unsigned)fat32_ctx.fat_start_sector,
                  (unsigned)fat32_ctx.data_start_sector,
                  (unsigned)fat32_ctx.root_dir_sector,
                  (unsigned)fat32_ctx.fsinfo_sector);
    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
    if (off >= out_cap - 1) return 0;

    if (!fat32_is_mounted()) {
        ksnprintf(tmp, sizeof(tmp), "skip dir read (not mounted)\n");
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
        return 0;
    }

    if (fat32_ctx.total_clusters == 0 || b->sectors_per_cluster == 0 || b->fat_size_32 == 0) {
        ksnprintf(tmp, sizeof(tmp), "skip dir read (bogus BPB)\n");
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
        return 0;
    }

    fat32_dir_entry_t entries_root[16];
    fat32_dir_entry_t entries_2[16];
    int count_root = 0, count_2 = 0;

    count_root = fat32_read_dir(root_clus, entries_root, 16);
    count_2 = fat32_read_dir(cluster2, entries_2, 16);

    ksnprintf(tmp, sizeof(tmp),
                  "dir[root_cluster=%u] entries=%d\n",
                  (unsigned)root_clus, count_root);
    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
    if (off >= out_cap - 1) return 0;

    int n_dump = (count_root < 8) ? count_root : 8;
    for (int i = 0; i < n_dump; i++) {
        char name[32];
        fat32_short_entry_to_str(&entries_root[i], name, sizeof(name));
        ksnprintf(tmp, sizeof(tmp),
                      "  [%d] %s size=%u attr=%02x\n",
                      i, name,
                      (unsigned)entries_root[i].file_size,
                      (unsigned)entries_root[i].attributes);
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
        if (off >= out_cap - 1) break;
    }

    if (off < out_cap - 1) {
        ksnprintf(tmp, sizeof(tmp),
                      "dir[cluster2=2] entries=%d\n",
                      count_2);
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
    }

    return 0;
}

int fat32_debug_bpb(char* out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    out[0] = 0;

    int off = 0;
    const fat32_bpb_t* b = &fat32_ctx.bpb;
    char tmp[256];

    off = ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                     "mounted=%d base_lba=%u\n",
                     (int)fat32_ctx.mounted, (unsigned)fat32_ctx.base_lba);
    if (off >= out_cap - 1) return 0;

    ksnprintf(tmp, sizeof(tmp),
                  "root_cluster=%u spc=%u rsvd=%u fats=%u fat_size=%u fs_info=%u backup_bs=%u tot_sec=%u\n",
                  (unsigned)b->root_cluster,
                  (unsigned)b->sectors_per_cluster,
                  (unsigned)b->reserved_sector_count,
                  (unsigned)b->num_fats,
                  (unsigned)b->fat_size_32,
                  (unsigned)b->fs_info_sector,
                  (unsigned)b->backup_boot_sector,
                  (unsigned)((b->total_sectors_16 != 0) ? b->total_sectors_16 : b->total_sectors_32));
    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
    if (off >= out_cap - 1) return 0;

    ksnprintf(tmp, sizeof(tmp),
                  "fat_start_rel=%u data_start_rel=%u root_dir_sector_abs=%u\n",
                  (unsigned)fat32_ctx.fat_start_sector,
                  (unsigned)fat32_ctx.data_start_sector,
                  (unsigned)fat32_ctx.root_dir_sector);
    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
    if (off >= out_cap - 1) return 0;

    return 0;
}

int fat32_debug_cluster2(char* out, int out_cap) {
    if (!out || out_cap <= 0) return -1;
    out[0] = 0;

    int off = 0;
    const int cluster = 2;
    const fat32_bpb_t* b = &fat32_ctx.bpb;
    char tmp[256];

    off = ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                     "mounted=%d\n", (int)fat32_ctx.mounted);
    if (off >= out_cap - 1) return 0;

    if (!fat32_is_mounted()) {
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                          "skip cluster read (not mounted)\n");
        return 0;
    }

    if (b->sectors_per_cluster == 0 || b->fat_size_32 == 0) {
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                          "skip cluster read (bogus BPB)\n");
        return 0;
    }

    if (fat32_read_cluster((u32)cluster, cluster_buffer) != 0) {
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                          "fat32_read_cluster(2) failed\n");
        return 0;
    }

    u32 bytes_per_cluster = (u32)b->sectors_per_cluster * ATA_SECTOR_SIZE;
    u32 entries_per_cluster = bytes_per_cluster / sizeof(fat32_dir_entry_t);
    u32 max_dump = entries_per_cluster < 8 ? entries_per_cluster : 8;

    off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0),
                       "cluster2 abs_sector=%u entries_per_cluster=%u\n",
                       (unsigned)fat32_cluster_to_sector(cluster),
                       (unsigned)entries_per_cluster);

    for (u32 i = 0; i < max_dump; i++) {
        fat32_dir_entry_t *e = (fat32_dir_entry_t *)(cluster_buffer + i * sizeof(fat32_dir_entry_t));
        if (e->name[0] == 0x00) break;
        if (e->name[0] == 0xE5 || e->name[0] == 0x05) continue;
        if (e->attributes == FAT32_ATTR_LFN) continue;
        if (e->attributes & FAT32_ATTR_VOLUME_ID) continue;

        char name[32];
        fat32_short_entry_to_str(e, name, sizeof(name));
        ksnprintf(tmp, sizeof(tmp),
                      "  [%u] %s attr=%02x size=%u firstChar=%02x\n",
                      (unsigned)i, name, (unsigned)e->attributes, (unsigned)e->file_size,
                      (unsigned)(u8)e->name[0]);
        off += ksnprintf(out + off, (off < out_cap ? out_cap - off : 0), "%s", tmp);
        if (off >= out_cap - 1) break;
    }

    return 0;
}

int fat32_read_cluster(u32 cluster, void *buffer) {
    u32 start_sector;
    u8 sectors_per_cluster;
    int i;
    u8 *buf = (u8 *)buffer;

    if (buffer == 0) return -1;
    if (cluster < 2) return -2;

    start_sector = fat32_cluster_to_sector(cluster);
    sectors_per_cluster = fat32_ctx.bpb.sectors_per_cluster;

    for (i = 0; i < sectors_per_cluster; i++) {
        if (ata_read_sectors(start_sector + i, 1, buf + (i * ATA_SECTOR_SIZE)) != 0) {
            return -3;
        }
    }

    return 0;
}

int fat32_write_cluster(u32 cluster, const void *buffer) {
    u32 start_sector;
    u8 sectors_per_cluster;
    int i;
    const u8 *buf = (const u8 *)buffer;

    if (buffer == 0) return -1;
    if (cluster < 2) return -2;

    start_sector = fat32_cluster_to_sector(cluster);
    sectors_per_cluster = fat32_ctx.bpb.sectors_per_cluster;

    for (i = 0; i < sectors_per_cluster; i++) {
        if (ata_write_sectors(start_sector + i, 1, buf + (i * ATA_SECTOR_SIZE)) != 0) {
            return -3;
        }
    }

    return 0;
}

int fat32_read_dir(u32 cluster, fat32_dir_entry_t *entries, int max_entries) {
    u8 *buffer;
    u32 current_cluster;
    int entry_count = 0;
    u32 bytes_per_cluster;
    fat32_dir_entry_t *entry;

    if (entries == 0 || max_entries <= 0) return 0;

    bytes_per_cluster = fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;

    buffer = cluster_buffer;

    current_cluster = cluster;

    int steps = 0;
    while (current_cluster >= 2 &&
           current_cluster < FAT32_CLUSTER_END &&
           steps < 16) {
        steps++;

        if (fat32_read_cluster(current_cluster, buffer) != 0) {
            break;
        }

        u32 i;
        for (i = 0; i < (bytes_per_cluster / sizeof(fat32_dir_entry_t)); i++) {
            entry = (fat32_dir_entry_t *)(buffer + (i * sizeof(fat32_dir_entry_t)));

            if (entry->name[0] == 0x00) {
                return entry_count;
            }

            if (entry->name[0] == 0xE5 || entry->name[0] == 0x05) {
                continue;
            }

            if (entry->attributes == FAT32_ATTR_LFN) {
                continue;
            }

            if (entry->attributes & FAT32_ATTR_VOLUME_ID) {
                continue;
            }

            kmemcpy(&entries[entry_count], entry, sizeof(fat32_dir_entry_t));
            entry_count++;

            if (entry_count >= max_entries) {
                return entry_count;
            }
        }

        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    return entry_count;
}

static void fat32_name_to_string(const u8 *name, const u8 *ext, char *out) {
    int i, j = 0;

    for (i = 0; i < 8 && name[i] != ' '; i++) {
        out[j++] = name[i];
    }

    if (ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && ext[i] != ' '; i++) {
            out[j++] = ext[i];
        }
    }

    out[j] = '\0';
}

int fat32_find_in_dir(u32 dir_cluster, const char *name, fat32_dir_entry_t *entry) {

    fat32_dir_entry_t entries[32];
    int count, i;
    char entry_name[13];
    char search_name[13];

    {
        int j;
        for (j = 0; j < 12 && name[j]; j++) {
            search_name[j] = (name[j] >= 'a' && name[j] <= 'z') ? (name[j] - 32) : name[j];
        }
        search_name[j] = '\0';
    }

    count = fat32_read_dir(dir_cluster, entries, 32);

    for (i = 0; i < count; i++) {
        fat32_name_to_string(entries[i].name, entries[i].ext, entry_name);

        if (kstrcmp(entry_name, search_name) == 0) {
            if (entry) {
                kmemcpy(entry, &entries[i], sizeof(fat32_dir_entry_t));
            }
            return 0;
        }
    }

    return -1;
}

int fat32_find_file(const char *name, fat32_dir_entry_t *entry) {
    return fat32_find_in_dir(fat32_ctx.bpb.root_cluster, name, entry);
}

int fat32_file_size(const char *path, u32 *size) {
    fat32_dir_entry_t entry;

    if (fat32_find_file(path, &entry) != 0) {
        return -1;
    }

    if (size) {
        *size = entry.file_size;
    }

    return 0;
}

int fat32_file_read(const char *path, void *buffer, u32 max_size) {
    fat32_dir_entry_t entry;
    u32 cluster;
    u32 bytes_read = 0;
    u32 bytes_to_read;
    u32 bytes_per_cluster;
    u8 *buf = (u8 *)buffer;

    if (buffer == 0 || max_size == 0) return -1;

    if (fat32_find_file(path, &entry) != 0) {
        return -2;
    }

    if (entry.attributes & FAT32_ATTR_DIRECTORY) {
        return -3;
    }

    cluster = ((u32)entry.cluster_high << 16) | entry.cluster_low;

    bytes_per_cluster = fat32_ctx.bpb.sectors_per_cluster * ATA_SECTOR_SIZE;
    bytes_to_read = entry.file_size;

    if (bytes_to_read > max_size) {
        bytes_to_read = max_size;
    }

    while (cluster >= 2 && cluster < FAT32_CLUSTER_END && bytes_read < bytes_to_read) {
        if (fat32_read_cluster(cluster, cluster_buffer) != 0) {
            return -4;
        }

        u32 chunk_size = bytes_to_read - bytes_read;
        if (chunk_size > bytes_per_cluster) {
            chunk_size = bytes_per_cluster;
        }

        kmemcpy(buf + bytes_read, cluster_buffer, chunk_size);
        bytes_read += chunk_size;

        cluster = fat32_get_next_cluster(cluster);
    }

    return (int)bytes_read;
}

u32 fat32_get_root_cluster(void) {
    return fat32_ctx.bpb.root_cluster;
}

const fat32_bpb_t *fat32_get_bpb(void) {
    return &fat32_ctx.bpb;
}

typedef struct __attribute__((packed)) {
    char filename[12];
    char type_code[4];
    char creator_code[4];
    u8  flags;
    u16 comment_offset;
} svs_file_info_t;

void fat32_set_svs_type_creator(const char *filename, const char type[4], const char creator[4]) {

    (void)filename;
    (void)type;
    (void)creator;

}

int fat32_get_svs_type_creator(const char *filename, char type[4], char creator[4]) {

    (void)filename;
    if (type) kmemset(type, 0, 4);
    if (creator) kmemset(creator, 0, 4);
    return -1;
}
