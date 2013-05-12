/*
 * @file: fat_common.h
 *
 * @author: Kevin Allison
 *
 * Provide common functions for FAT filesystems
 */

#ifndef FATCOMMON_XINU
#define FATCOMMON_XINU

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

typedef struct fat_extBS_32 {
	//extended fat32 stuff
	unsigned int		table_size_32;
	unsigned short		extended_flags;
	unsigned short		fat_version;
	unsigned int		root_cluster;
	unsigned short		fat_info;
	unsigned short		backup_BS_sector;
	unsigned char 		reserved_0[12];
	unsigned char		drive_number;
	unsigned char 		reserved_1;
	unsigned char		boot_signature;
	unsigned int 		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];
 
}__attribute__((packed)) fat_extBS_32_t;

typedef struct fat_extBS_16 {
	//extended fat12 and fat16 stuff
	unsigned char		bios_drive_num;
	unsigned char		reserved1;
	unsigned char		boot_signature;
	unsigned int		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];
 
}__attribute__((packed)) fat_extBS_16_t;
 
typedef struct fat_BS {
	unsigned char 		bootjmp[3];
	unsigned char 		oem_name[8];
	unsigned short 	    bytes_per_sector;
	unsigned char		sectors_per_cluster;
	unsigned short		reserved_sector_count;
	unsigned char		table_count;
	unsigned short		root_entry_count;
	unsigned short		total_sectors_16;
	unsigned char		media_type;
	unsigned short		table_size_16;
	unsigned short		sectors_per_track;
	unsigned short		head_side_count;
	unsigned int 		hidden_sector_count;
	unsigned int 		total_sectors_32;
 
	//this will be cast to it's specific type once the 
    // driver actually knows what type of FAT this is.
	unsigned char		extended_section[54];
 
}__attribute__((packed)) fat_BS_t;

typedef struct fat_long_direntry {
    unsigned char       order;
    unsigned short      charset1[5];
    unsigned char       attribute;
    unsigned char       type;
    unsigned char       checksum;
    unsigned short      charset2[6];
    unsigned short      zero;
    unsigned short      charset3[2];
}__attribute__((packed)) fat_long_direntry_t;

typedef struct fat_direntry {
    unsigned char       name[11];           /* 8.3 File Name */
    unsigned char       attributes;
    unsigned char       reserved_nt;
    uint8_t             time_milli;
    uint16_t            time;
    uint16_t            date;
    uint16_t            last_accessed;
    uint16_t            high_clu;
    uint16_t            mod_time;
    uint16_t            mod_date;
    uint16_t            low_clu;
    uint32_t            size;
}__attribute__((packed)) fat_direntry_t;

typedef struct fat_file {
    char            *longname;
    fat_direntry_t  dir_ent;    
    int             offset;
    int             beg_marker;
    int             eof_marker;
} fat_file_t;

typedef struct fat_fsinfo {
    unsigned int        num_free_clusters;
    unsigned int        last_alloc;
}__attribute__((packed)) fat_fsinfo_t;

typedef struct fat_s {
    fat_BS_t *bs;
    fat_fsinfo_t *info;
    int fs_type;
    int data_sect;   
    int n_clusters;
} fat_t;


unsigned int read_fat_table(int device, fat_t* fat, int cluster);
unsigned char lfn_checksum(const unsigned char *filename);
char * gen_basis_name(char *input);
fat_long_direntry_t build_long_entry(int order, char *input, unsigned char *shortname);
int find_free_cluster(char *dev, fat_t *fat, int cluster);
#endif