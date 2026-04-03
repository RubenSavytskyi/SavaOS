#ifndef FAT32_H
#define FAT32_H

#include "types.h"




typedef struct __attribute__((packed)) {
    
    u8  jmp_boot[3];           
    u8  oem_name[8];          
    u16 bytes_per_sector;     
    u8  sectors_per_cluster;    
    u16 reserved_sector_count;  
    u8  num_fats;             
    u16 root_entry_count;     
    u16 total_sectors_16;     
    u8  media_type;           
    u16 fat_size_16;          
    u16 sectors_per_track;    
    u16 num_heads;            
    u32 hidden_sectors;       
    u32 total_sectors_32;     
    
    
    u32 fat_size_32;          
    u16 ext_flags;            
    u16 fs_version;           
    u32 root_cluster;         
    u16 fs_info_sector;       
    u16 backup_boot_sector;   
    u8  reserved_1[12];       
    
    
    u8  drive_number;         
    u8  reserved_2;           
    u8  boot_signature;       
    
    
    u32 volume_serial;        
    u8  volume_label[11];     
    u8  file_system_type[8];  
    
    
    u8  boot_code[420];       
    u16 boot_sector_signature; 
} fat32_bpb_t;


typedef struct __attribute__((packed)) {
    u8  name[8];              
    u8  ext[3];               
    u8  attributes;           
    u8  nt_reserved;          
    u8  create_time_tenth;    
    u16 create_time;          
    u16 create_date;          
    u16 access_date;          
    u16 cluster_high;         
    u16 modify_time;          
    u16 modify_date;          
    u16 cluster_low;          
    u32 file_size;            
} fat32_dir_entry_t;


typedef struct __attribute__((packed)) {
    u8  order;                
    u16 name1[5];             
    u8  attributes;           
    u8  type;                 
    u8  checksum;             
    u16 name2[6];             
    u16 cluster_low;          
    u16 name3[2];             
} fat32_lfn_entry_t;


typedef struct __attribute__((packed)) {
    u32 lead_signature;       
    u8  reserved_1[480];      
    u32 structure_signature;  
    u32 free_cluster_count;   
    u32 next_free_cluster;    
    u8  reserved_2[12];       
    u32 trail_signature;      
} fat32_fsinfo_t;


#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LFN          0x0F  


#define FAT32_CLUSTER_FREE      0x00000000
#define FAT32_CLUSTER_RESERVED  0x00000001
#define FAT32_CLUSTER_BAD       0x0FFFFFF7
#define FAT32_CLUSTER_END       0x0FFFFFFF
#define FAT32_CLUSTER_EOC_MIN  0x0FFFFFF8  







typedef struct __attribute__((packed)) {
    
    u8  name[8];
    u8  ext[3];
    u8  attributes;
    
    
    u8  svs_type_code[4];     
    u8  svs_creator_code[4];  
    
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 modify_time;
    u16 modify_date;
    u16 cluster_low;
    u32 file_size;
} fat32_svs_dir_entry_t;


#define SVS_DESKTOP_DB "DESKTOP.DB"
#define SVS_DESKTOP_DF "DESKTOP.DF"


typedef struct {
    fat32_bpb_t bpb;          
    u32 base_lba;            
    u32 fat_start_sector;     
    u32 root_dir_sector;        
    u32 data_start_sector;      
    u32 sectors_per_fat;      
    u32 total_clusters;       
    u32 free_clusters;        
    u32 fsinfo_sector;        
    u8  mounted;              
} fat32_context_t;




int fat32_init(void);
int fat32_mount(void);
void fat32_unmount(void);
int fat32_is_mounted(void);


u32 fat32_cluster_to_sector(u32 cluster);
u32 fat32_get_next_cluster(u32 cluster);
int fat32_read_cluster(u32 cluster, void *buffer);
int fat32_write_cluster(u32 cluster, const void *buffer);


int fat32_set_fat_entry(u32 cluster, u32 value);
u32 fat32_alloc_cluster(void);
int fat32_free_chain(u32 first_cluster);


int fat32_read_dir(u32 cluster, fat32_dir_entry_t *entries, int max_entries);
int fat32_find_file(const char *name, fat32_dir_entry_t *entry);
int fat32_find_in_dir(u32 dir_cluster, const char *name, fat32_dir_entry_t *entry);


int fat32_mkdir_in_dir(u32 parent_dir_cluster, const char *name83);
int fat32_unlink_in_dir(u32 parent_dir_cluster, const char *name83);
int fat32_rmdir_empty_in_dir(u32 parent_dir_cluster, const char *name83);
int fat32_write_file_new_in_dir(u32 parent_dir_cluster, const char *name83, const void *data, u32 len);
int fat32_copy_file_in_dir(u32 src_dir_cluster, const char *src_name83, u32 dst_dir_cluster, const char *dst_name83);


int fat32_file_read(const char *path, void *buffer, u32 max_size);
int fat32_file_size(const char *path, u32 *size);


void fat32_set_svs_type_creator(const char *filename, const char type[4], const char creator[4]);
int fat32_get_svs_type_creator(const char *filename, char type[4], char creator[4]);


u32 fat32_get_root_cluster(void);
const fat32_bpb_t *fat32_get_bpb(void);


int fat32_debug_dump(char* out, int out_cap);


int fat32_debug_bpb(char* out, int out_cap);
int fat32_debug_cluster2(char* out, int out_cap);

#endif 
