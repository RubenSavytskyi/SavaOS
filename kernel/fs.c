#include "fs.h"
#include "fat32.h"
#include "ata.h"
#include "string.h"


static FSFile files[FS_MAX_FILES];
static int use_fat32 = 0;  


static void fat32_entry_to_name(const fat32_dir_entry_t* e, char* out, int out_cap) {
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

u32 fs_root_dir_cluster(void) {
    if (fs_using_fat32()) return fat32_get_root_cluster();
    return 0;
}

static u32 fat32_entry_first_cluster(const fat32_dir_entry_t* e) {
    return ((u32)e->cluster_high << 16) | (u32)e->cluster_low;
}

int fs_list_dir(u32 dir_cluster, FSDirEnt* out, int max) {
    if (!out || max <= 0) return 0;
    for (int i = 0; i < max; i++) {
        out[i].name[0] = 0;
        out[i].size = 0;
        out[i].first_cluster = 0;
        out[i].is_dir = 0;
    }

    if (fs_using_fat32()) {
        fat32_dir_entry_t entries[FS_MAX_FILES];
        int count = fat32_read_dir(dir_cluster, entries, max);
        int n = 0;
        for (int i = 0; i < count && n < max; i++) {
            
            if (entries[i].name[0] == '.' && (entries[i].name[1] == ' ' || entries[i].name[1] == '.')) continue;
            fat32_entry_to_name(&entries[i], out[n].name, FS_MAX_NAME);
            out[n].size = entries[i].file_size;
            out[n].first_cluster = fat32_entry_first_cluster(&entries[i]);
            out[n].is_dir = (entries[i].attributes & FAT32_ATTR_DIRECTORY) ? 1 : 0;
            n++;
        }
        return n;
    }

    
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES && n < max; i++) {
        if (files[i].used) {
            kstrncpy(out[n].name, files[i].name, FS_MAX_NAME);
            out[n].size = files[i].size;
            out[n].first_cluster = 0;
            out[n].is_dir = 0;
            n++;
        }
    }
    return n;
}

int fs_mkdir(u32 dir_cluster, const char* name83) {
    if (!name83 || !name83[0]) return -1;
    if (!fs_using_fat32()) return -1;
    return fat32_mkdir_in_dir(dir_cluster, name83);
}

int fs_unlink(u32 dir_cluster, const char* name83) {
    if (!name83 || !name83[0]) return -1;
    if (!fs_using_fat32()) return fs_delete(name83);
    return fat32_unlink_in_dir(dir_cluster, name83);
}

int fs_rmdir_empty(u32 dir_cluster, const char* name83) {
    if (!name83 || !name83[0]) return -1;
    if (!fs_using_fat32()) return -1;
    return fat32_rmdir_empty_in_dir(dir_cluster, name83);
}

int fs_copy_file(u32 src_dir_cluster, const char* src_name83, u32 dst_dir_cluster, const char* dst_name83) {
    if (!src_name83 || !src_name83[0]) return -1;
    if (!fs_using_fat32()) return -1;
    return fat32_copy_file_in_dir(src_dir_cluster, src_name83, dst_dir_cluster, dst_name83);
}

void fs_init(void) {
    int i;
    int fat32_ok = 0;
    
    
    ata_init();
    
    
    for (volatile int delay = 0; delay < 10000000; delay++) { }
    
    
    for (int attempt = 0; attempt < 3 && !fat32_ok; attempt++) {
        
        ata_init();
        fat32_unmount();
        if (fat32_mount() == 0) {
            use_fat32 = 1;
            fat32_ok = 1;
        } else {
            
            for (volatile int delay = 0; delay < 5000000; delay++) { }
        }
    }
    
    if (fat32_ok) {
        
        {
            fat32_dir_entry_t e;
            u32 root = fat32_get_root_cluster();
            if (fat32_find_in_dir(root, "DESKTOP", &e) != 0) {
                (void)fat32_mkdir_in_dir(root, "DESKTOP");
            }
        }
        
        for (i = 0; i < FS_MAX_FILES; i++) {
            files[i].used = 0;
            files[i].size = 0;
            files[i].name[0] = 0;
            files[i].data[0] = 0;
        }
        return; 
    }
    
    
    use_fat32 = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].name[0] = 0;
        files[i].data[0] = 0;
    }
    
    int fd;
    fd = fs_create("readme.txt");
    fs_write(fd,
        "Welcome to SavaOS v0.1!\r\n"
        "========================\r\n"
        "\r\n"
        "SavaOS is a simple 32-bit operating system\r\n"
        "inspired by Windows 95/98 aesthetics.\r\n"
        "\r\n"
        "Keyboard shortcuts:\r\n"
        "  TAB       - Switch between windows\r\n"
        "  ESC       - Open/close Start menu\r\n"
        "  F1        - Cycle desktop icons\r\n"
        "  ENTER     - Open focused icon\r\n"
        "\r\n"
        "Terminal commands: help, ls, cat, echo,\r\n"
        "  clear, time, uname, calc, ver\r\n", 0);

    fd = fs_create("autoexec.txt");
    fs_write(fd,
        "@echo SavaOS AutoExec\r\n"
        "@ver\r\n", 0);

    fd = fs_create("notes.txt");
    fs_write(fd,
        "My Notes\r\n"
        "--------\r\n"
        "TODO: explore SavaOS!\r\n", 0);
}


int fs_using_fat32(void) {
    
    return use_fat32 || fat32_is_mounted();
}

static FSFile* find_file(const char* name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && kstrcmp(files[i].name, name) == 0)
            return &files[i];
    return (void*)0;
}

int fs_create(const char* name) {
    int i;
    
    if (fs_using_fat32()) {
        
        (void)name;
        return -1;
    }
    
    
    
    for (i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && kstrcmp(files[i].name, name) == 0)
            return i;
    
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            files[i].used = 1;
            files[i].size = 0;
            kstrncpy(files[i].name, name, FS_MAX_NAME);
            files[i].data[0] = 0;
            return i;
        }
    }
    return -1;
}

int fs_open(const char* name) {
    int i;
    
    if (fs_using_fat32()) {
        fat32_dir_entry_t entry;
        if (fat32_find_file(name, &entry) != 0) return -1;
        if (entry.attributes & FAT32_ATTR_DIRECTORY) return -1;

        
        for (i = 0; i < FS_MAX_FILES; i++) {
            if (files[i].used && kstrcmp(files[i].name, name) == 0) return i;
        }

        
        for (i = 0; i < FS_MAX_FILES; i++) {
            if (!files[i].used) {
                
                int n = fat32_file_read(name, files[i].data, FS_MAX_SIZE - 1);
                if (n < 0) return -1;
                files[i].data[n] = 0;
                files[i].size = (u32)n;
                kstrncpy(files[i].name, name, FS_MAX_NAME);
                files[i].used = 1;
                return i;
            }
        }
        return -1; 
    }
    
    
    for (i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && kstrcmp(files[i].name, name) == 0)
            return i;
    return -1;
}

int fs_write(int fd, const char* data, u32 len) {
    if (fd < 0 || fd >= FS_MAX_FILES || !files[fd].used) return -1;
    if (len == 0) len = kstrlen(data);
    if (len > FS_MAX_SIZE - 1) len = FS_MAX_SIZE - 1;
    kmemcpy(files[fd].data, data, len);
    files[fd].data[len] = 0;
    files[fd].size = len;
    return (int)len;
}

int fs_read(int fd, char* buf, u32 len) {
    if (fd < 0 || fd >= FS_MAX_FILES || !files[fd].used) return -1;
    u32 n = files[fd].size < len ? files[fd].size : len;
    kmemcpy(buf, files[fd].data, n);
    buf[n] = 0;
    return (int)n;
}

int fs_delete(const char* name) {
    if (fs_using_fat32()) {
        
        (void)name;
        return -1;
    }
    FSFile* f = find_file(name);
    if (!f) return -1;
    f->used = 0;
    return 0;
}

int fs_exists(const char* name) { return find_file(name) ? 1 : 0; }

u32 fs_size(int fd) {
    if (fd < 0 || fd >= FS_MAX_FILES || !files[fd].used) return 0;
    return files[fd].size;
}

const char* fs_get_data(int fd) {
    if (fd < 0 || fd >= FS_MAX_FILES || !files[fd].used) return "";
    return files[fd].data;
}

int fs_list(char names[][FS_MAX_NAME], int max) {
    int n = 0;
    int i;
    
    if (fs_using_fat32()) {
        
        fat32_dir_entry_t entries[FS_MAX_FILES];
        int count = fat32_read_dir(fat32_get_root_cluster(), entries, max);
        if (count == 0) {
            
            count = fat32_read_dir(2, entries, max);
        }
        n = count;
        for (i = 0; i < count && i < max; i++) {
            fat32_entry_to_name(&entries[i], names[i], FS_MAX_NAME);
        }
    } else {
        
        for (i = 0; i < FS_MAX_FILES && n < max; i++)
            if (files[i].used) kstrcpy(names[n++], files[i].name);
    }
    
    return n;
}

u32 fs_file_size(const char* name) {
    fat32_dir_entry_t entry;
    
    if (fs_using_fat32()) {
        if (fat32_find_file(name, &entry) == 0) {
            return entry.file_size;
        }
        return 0;
    }
    
    
    int fd = fs_open(name);
    if (fd < 0) return 0;
    return fs_size(fd);
}


int fs_read_file(const char* name, char* buf, u32 len) {
    if (fs_using_fat32()) {
        return fat32_file_read(name, buf, len);
    }
    
    
    int fd = fs_open(name);
    if (fd < 0) return -1;
    return fs_read(fd, buf, len);
}


static u8 fs_tmp_cluster_buf[4096];

u32 fs_file_size_in_dir(u32 dir_cluster, const char* name83) {
    fat32_dir_entry_t e;
    if (!name83 || !name83[0]) return 0;
    if (!fs_using_fat32()) return fs_file_size(name83);
    if (fat32_find_in_dir(dir_cluster, name83, &e) != 0) return 0;
    return e.file_size;
}

int fs_read_file_in_dir(u32 dir_cluster, const char* name83, char* buf, u32 len) {
    fat32_dir_entry_t e;
    u32 cluster;
    u32 bytes_read = 0;
    u32 bytes_to_read;
    u32 bytes_per_cluster;

    if (!name83 || !name83[0] || !buf || len == 0) return -1;
    if (!fs_using_fat32()) return fs_read_file(name83, buf, len);

    if (fat32_find_in_dir(dir_cluster, name83, &e) != 0) return -2;
    if (e.attributes & FAT32_ATTR_DIRECTORY) return -3;

    cluster = ((u32)e.cluster_high << 16) | (u32)e.cluster_low;
    bytes_to_read = e.file_size;
    if (bytes_to_read > len) bytes_to_read = len;
    bytes_per_cluster = (u32)fat32_get_bpb()->sectors_per_cluster * ATA_SECTOR_SIZE;
    if (bytes_per_cluster > sizeof(fs_tmp_cluster_buf)) bytes_per_cluster = sizeof(fs_tmp_cluster_buf);

    while (cluster >= 2 && cluster < FAT32_CLUSTER_END && bytes_read < bytes_to_read) {
        if (fat32_read_cluster(cluster, fs_tmp_cluster_buf) != 0) break;
        u32 chunk = bytes_to_read - bytes_read;
        if (chunk > bytes_per_cluster) chunk = bytes_per_cluster;
        kmemcpy(buf + bytes_read, fs_tmp_cluster_buf, chunk);
        bytes_read += chunk;
        cluster = fat32_get_next_cluster(cluster);
    }
    return (int)bytes_read;
}


int fs_append_line(int fd, const char* line) {
    FSFile* f;
    int llen;
    
    if (fs_using_fat32()) {
        
        (void)fd;
        (void)line;
        return -1;
    }
    
    
    if (fd < 0 || fd >= FS_MAX_FILES || !files[fd].used) return -1;
    f = &files[fd];
    llen = kstrlen(line);
    if (f->size + llen + 2 >= FS_MAX_SIZE) return -1;
    kmemcpy(f->data + f->size, line, llen);
    f->size += llen;
    f->data[f->size++] = '\r';
    f->data[f->size++] = '\n';
    f->data[f->size] = 0;
    return llen;
}


int fs_try_mount_fat32(void) {
    
    if (fs_using_fat32()) return 0;
    
    
    ata_init();
    fat32_unmount();
    if (fat32_mount() == 0) {
        use_fat32 = 1;
        
        {
            fat32_dir_entry_t e;
            u32 root = fat32_get_root_cluster();
            if (fat32_find_in_dir(root, "DESKTOP", &e) != 0) {
                (void)fat32_mkdir_in_dir(root, "DESKTOP");
            }
        }
        return 0;
    }
    return -1;
}
