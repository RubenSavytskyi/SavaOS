#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILES   32
#define FS_MAX_NAME    16
#define FS_MAX_SIZE    2048  
#define FS_TOTAL_BUF   (FS_MAX_FILES * FS_MAX_SIZE)

typedef struct {
    char  name[FS_MAX_NAME];
    u32   size;
    int   used;
    char  data[FS_MAX_SIZE];
} FSFile;

typedef struct {
    char name[FS_MAX_NAME]; 
    u32  size;
    u32  first_cluster;
    int  is_dir;
} FSDirEnt;

void  fs_init(void);
int   fs_create(const char* name);
int   fs_open(const char* name);
int   fs_write(int fd, const char* data, u32 len);
int   fs_read(int fd, char* buf, u32 len);
int   fs_delete(const char* name);
int   fs_exists(const char* name);
u32   fs_size(int fd);
const char* fs_get_data(int fd);
int   fs_list(char names[][FS_MAX_NAME], int max);


int   fs_using_fat32(void);
u32   fs_file_size(const char* name);
int   fs_read_file(const char* name, char* buf, u32 len);
u32   fs_file_size_in_dir(u32 dir_cluster, const char* name83);
int   fs_read_file_in_dir(u32 dir_cluster, const char* name83, char* buf, u32 len);


u32   fs_root_dir_cluster(void);
int   fs_list_dir(u32 dir_cluster, FSDirEnt* out, int max);
int   fs_mkdir(u32 dir_cluster, const char* name83);
int   fs_unlink(u32 dir_cluster, const char* name83);
int   fs_rmdir_empty(u32 dir_cluster, const char* name83);
int   fs_copy_file(u32 src_dir_cluster, const char* src_name83, u32 dst_dir_cluster, const char* dst_name83);


int   fs_append_line(int fd, const char* line);


int   fs_try_mount_fat32(void);

#endif
