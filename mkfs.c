/*
 * @file: mkfs.c
 *
 * @author: Kevin Allison
 *
 * Utility to create FAT file systems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat_common.h"
#include "fat32.h"
#include "skinny28.h"

typedef struct cmd_options {
    unsigned int sector_size;            /* In Bytes */
    unsigned int clusters;               /* In clusters_per_sector */
    unsigned long long size;             /* In Bytes */
    char *device;
    unsigned char *label;                         /* Volume Label */
} cmd_options_t;

/* Code to display a not-bootable partition message */
unsigned char bootcode[420] = {0x0E, 0x1F, 0xBE, 0x77, 0x7C, 0xAC, 0x22, 0xC0, 0x74, 0x0B, 0x56, 0xB4,
    0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10, 0x5E, 0xEB, 0xF0, 0x32, 0xE4, 0xCD, 0x16, 0xCD, 0x19,
    0xEB, 0xFE, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x6E, 0x6F, 0x74, 0x20, 0x61,
    0x20, 0x62, 0x6F, 0x6F, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x64, 0x69, 0x73, 0x6B, 0x2E,
    0x20, 0x20, 0x50, 0x6C, 0x65, 0x61, 0x73, 0x65, 0x20, 0x69, 0x6E, 0x73, 0x65, 0x72, 0x74,
    0x20, 0x61, 0x20, 0x62, 0x6F, 0x6F, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x66, 0x6C, 0x6F,
    0x70, 0x70, 0x79, 0x20, 0x61, 0x6E, 0x64, 0x0D, 0x0A, 0x70, 0x72, 0x65, 0x73, 0x73, 0x20,
    0x61, 0x6E, 0x79, 0x20, 0x6B, 0x65, 0x79, 0x20, 0x74, 0x6F, 0x20, 0x74, 0x72, 0x79, 0x20,
    0x61, 0x67, 0x61, 0x69, 0x6E, 0x20, 0x2E, 0x2E, 0x2E, 0x20, 0x0D, 0x0A};

unsigned char bootsig[2] = {0x55, 0xAA};
unsigned char no_name[11] = {'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' '};

void create_file(char *fname, uint64_t size) {
    FILE *fp = fopen(fname, "w");
    if (fp == NULL) {
        printf("Error Creating Device\n");
        exit(EXIT_FAILURE);
    } 
    fseek(fp, size, SEEK_SET);
    fwrite("", 1, 1, fp);
    fclose(fp);
}

void write_bs_to_file(char *fname, int offset, fat_BS_t *boot) {
    
    /* Create FILE-based FAT instead of Disk-based */
    FILE *fp = fopen(fname, "r+");
    if (fp == NULL) {
        printf("[FAT32]: Error Opening Device -> Write BS\n");
        exit(EXIT_FAILURE);
    } 
    fseek(fp, offset, SEEK_SET);
        
    fwrite(boot, sizeof(*boot), 1, fp);
    fwrite(bootcode, sizeof(bootcode), 1, fp);
    fwrite(bootsig, sizeof(bootsig), 1, fp);
    
    fclose(fp);
}

void write_fsinfo_to_file(char *fname, int offset, fat_fsinfo_t *fsinfo) {
    /* Create FILE-based FAT instead of Disk-based */
    
    unsigned char sig1[4] = {0x52, 0x52, 0x61, 0x41};
    unsigned char reserved_0[480] = {0x00};
    unsigned char sig2[4] = {0x72, 0x72, 0x41, 0x61};
    unsigned char reserved_1[12] = {0x00};
    unsigned char sig3[4] = {0x00, 0x00, 0x55, 0xAA};
    
    FILE *fp = fopen(fname, "r+");
    if (fp == NULL) {
       printf("[FAT32]: Error Opening Device -> Write FSInfo\n");
        exit(EXIT_FAILURE);
    } 
    
    fseek(fp, offset, SEEK_SET);
    
    fwrite(sig1, sizeof(sig1), 1, fp);
    fwrite(reserved_0, sizeof(reserved_0), 1, fp);
    fwrite(sig2, sizeof(sig2), 1, fp);
    fwrite(&(fsinfo->num_free_clusters), sizeof(unsigned int), 1, fp);
    fwrite(&(fsinfo->last_alloc), sizeof(unsigned int), 1, fp);
    fwrite(reserved_1, sizeof(reserved_1), 1, fp);
    fwrite(sig3, sizeof(sig2), 1, fp);
    
    fclose(fp);
}

/* Parse the Size of the FS given on the command line */
unsigned long parse_size(char *size) {
    unsigned long long sz = 0;
    int len = strlen(size);
    switch (size[len-1]) {
        case 'K':
            size[len-1] = '\0';
            sz = atol(size) * 1024;
            break;
        case 'M':
            size[len-1] = '\0';
            sz = atol(size) * 1024 * 1024;
            break;
        case 'G':
            size[len-1] = '\0';
            sz = atol(size) * 1024 * 1024 * 1024;
            break;            
        default:
            sz = atol(size);
    }
    return sz;
}

uint8_t get_cluster_size(uint64_t size, uint32_t sector_size, uint8_t fattype) {
    size = size / sector_size;
    
    if (fattype == FAT16) {
        for (int i = 0; i< DskTableFAT16_NumEntries; i++) {
            if (size <= DskTableFAT16[i].DiskSize) {
                return DskTableFAT16[i].SecPerClusVal;
            }
        }
    } else {
        for (int i = 0; i< DskTableFAT32_NumEntries; i++) {
            if (size <= DskTableFAT32[i].DiskSize) {
                return DskTableFAT32[i].SecPerClusVal;
            }
        }
    }
    return 0;
}

inline uint8_t determine_fat_type(uint64_t size) {
    /* Less than 512MB -> FAT16, else FAT32 */
    return (size < 536870912) ? FAT16 : FAT32;   
}

/*inline uint8_t determine_fat_type(uint32_t cluster_count) {
    return (cluster_count < 65525) ? FAT16 : FAT32;   
} */

int main(int argc, char **argv) {
    if (argc < 2 || argc > 7) {
        printf("usage: mkfs [-s sector_size] [-c clusters_per_sector] [-n label] fs_size device\n");
        exit(EXIT_FAILURE);
    }

    cmd_options_t opts;    

    
    // Parse Arguments
    for (int i = 1; i < argc - 2; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            opts.sector_size = (++i < argc - 2) ? atoi(argv[i]) : 0;
        } else if (strcmp(argv[i], "-c") == 0) {
            opts.clusters = (++i < argc - 2) ? atoi(argv[i]) : 0;
        } else if (strcmp(argv[i], "-n") == 0) {
            opts.label = (++i < argc - 2) ? (unsigned char*)argv[i] : NULL;
        }
    }
    
    opts.size = parse_size(argv[argc - 2]);
    int fattype = (opts.size < 536870912) ? FAT16 : FAT32;
    opts.device = argv[argc - 1];
    opts.sector_size = (opts.sector_size) == 0 ? 512 : opts.sector_size;
    opts.label = (opts.label == NULL) ? no_name : opts.label;    
    opts.clusters = (opts.clusters == 0) ? get_cluster_size(opts.size, opts.sector_size, fattype) : opts.sector_size;
        
    printf("Sector Size: %d Bytes\nClusters Size: %d Bytes\nSize: %llu Bytes\n", opts.sector_size, opts.clusters * opts.sector_size, opts.size);
    
    /* Create File */
    create_file(opts.device, opts.size);
    
    fat_BS_t *boot_sector = calloc(1, sizeof(fat_BS_t));
    boot_sector->bootjmp[0] = 0xEB; boot_sector->bootjmp[1] = 0x58; boot_sector->bootjmp[2] = 0x90;
    boot_sector->oem_name[0] = 'm'; boot_sector->oem_name[1] = 'k'; 
    boot_sector->oem_name[2] = 'd'; boot_sector->oem_name[3] = 'o';
    boot_sector->oem_name[4] = 's'; boot_sector->oem_name[5] = 'f';
    boot_sector->oem_name[6] = 's'; boot_sector->oem_name[7] = ' ';
    boot_sector->bytes_per_sector = opts.sector_size;
    boot_sector->sectors_per_cluster = opts.clusters;
    boot_sector->reserved_sector_count = 32;
    boot_sector->table_count = 2;
    boot_sector->root_entry_count = 3;
    boot_sector->total_sectors_16 = 0;      /* 0 -- See Total_Sectors_32 */
    boot_sector->media_type = 0xF8;
    boot_sector->table_size_16 = 0;         /* 0 for Fat32 */
    boot_sector->sectors_per_track = 32;
    boot_sector->head_side_count = 64;
    boot_sector->hidden_sector_count = 0;   /* No Hidden Sectors */
    boot_sector->total_sectors_32 = opts.size / boot_sector->bytes_per_sector;;
    
    /* Compute # Sectors in FAT -- Algorithm according to MS FAT Specification 1.03 */
    int root_dir_sectors = (boot_sector->bytes_per_sector - 1) / boot_sector->bytes_per_sector;
    int tmpval1 = boot_sector->total_sectors_32 - (boot_sector->reserved_sector_count + root_dir_sectors);
    int tmpval2 = (256 * boot_sector->sectors_per_cluster) + boot_sector->table_count;
    if (fattype == FAT32) {
        tmpval2 = tmpval2 / 2;
    }
    int tbl_size = (tmpval1 + (tmpval2 - 1)) / tmpval2;
    
    /* Extended Boot Record */
    fat_extBS_32_t *ext = (fat_extBS_32_t*)boot_sector->extended_section;
    ext->table_size_32 = tbl_size;    
    ext->extended_flags = 0;
    ext->fat_version = 0;                   /* Version Should Be 0.0, hence the 0 input */
    ext->root_cluster = 2;
    ext->fat_info = 1;
    ext->backup_BS_sector = 6;
    ext->reserved_0[0] = 0; ext->reserved_0[1] = 0; ext->reserved_0[2] = 0;
    ext->reserved_0[3] = 0; ext->reserved_0[4] = 0; ext->reserved_0[5] = 0;
    ext->reserved_0[6] = 0; ext->reserved_0[7] = 0; ext->reserved_0[8] = 0;
    ext->reserved_0[9] = 0; ext->reserved_0[10] = 0; ext->reserved_0[11] = 0;
    ext->drive_number = 0;
    ext->reserved_1 = 0;
    ext->boot_signature = 0x29;
    ext->volume_id = 892301;
    
    ext->volume_label[0] = 'R'; ext->volume_label[1] = 'A'; ext->volume_label[2] = 'S';
    ext->volume_label[3] = 'P'; ext->volume_label[4] = 'X'; ext->volume_label[5] = 'I';
    ext->volume_label[6] = 'N'; ext->volume_label[7] = 'N'; ext->volume_label[8] = 'U';
    ext->volume_label[9] = ' '; ext->volume_label[10] = ' ';
    
    ext->fat_type_label[0] = 'S'; ext->fat_type_label[1] = 'K'; ext->fat_type_label[2] = 'I';
    ext->fat_type_label[3] = 'N'; ext->fat_type_label[4] = 'N'; ext->fat_type_label[5] = 'Y';
    ext->fat_type_label[6] = '2'; ext->fat_type_label[7] = '8';
    
    write_bs_to_file(opts.device, 0, boot_sector);
    write_bs_to_file(opts.device, ext->backup_BS_sector * boot_sector->bytes_per_sector, boot_sector);
    
    int num_clusters = (boot_sector->total_sectors_32 - (boot_sector->reserved_sector_count + (boot_sector->table_count * ext->table_size_32) + root_dir_sectors)) / boot_sector->sectors_per_cluster;
    
    /* Create FS Info Structure */
    fat_fsinfo_t *fsinfo = calloc(1, sizeof(fat_fsinfo_t));
    fsinfo->num_free_clusters = 0xFFFFFFFF;
    fsinfo->last_alloc = 0xFFFFFFFF;
    write_fsinfo_to_file(opts.device, ext->fat_info * boot_sector->bytes_per_sector, fsinfo);   
    
        /* Create FAT Table */

    unsigned int fat_table[num_clusters];
    
    fat_table[0] = 0x0FFFFF00 | boot_sector->media_type;
    fat_table[1] = 0x0FFFFFFF;
    fat_table[2] = 0x0FFFFF00 | boot_sector->media_type;
    fat_table[3] = 0x0FFFFFFF;
        
    FILE *fp = fopen(opts.device, "r+");
    if (fp == NULL) {
       printf("[FAT32]: Error Opening Device -> Write FSInfo\n");
        exit(EXIT_FAILURE);
    } 
    
    printf("Writing FAT Table #1 at Sector: %d\n", boot_sector->reserved_sector_count);
    fseek(fp, boot_sector->reserved_sector_count * boot_sector->bytes_per_sector, SEEK_SET);
    printf("Writing FAT Table #2 at Sector: %d\n", boot_sector->reserved_sector_count + ext->table_size_32);
    fwrite(fat_table, ext->table_size_32, 1, fp);
    fseek(fp, (boot_sector->reserved_sector_count * boot_sector->bytes_per_sector) + (ext->table_size_32 * boot_sector->bytes_per_sector), SEEK_SET);
    fwrite(fat_table, ext->table_size_32, 1, fp);
    
    /* Write Dir Structure */
    fat_direntry_t *root = calloc(1, sizeof(fat_direntry_t));
    size_t len = strlen((const char*)opts.label);
    for (int i = 0; i < 11; i++) {
        root->name[i] = i < len ? opts.label[i] : ' ';
    }
    root->attributes = 0x08;
    root->reserved_nt = 0x00;
    root->time_milli = 0x00;
    root->time = 0x0000;
    root->date = 0x0000;
    root->last_accessed = 0x0000;
    root->high_clu = 0x0000;
    root->mod_time = 0xA000;
    root->mod_date = 0x0000;
    root->low_clu = 0x0000;
    root->size = 0x00000000;
    
    // Start location of the data Sector
    int datasect = boot_sector->reserved_sector_count + (boot_sector->table_count * ext->table_size_32) + root_dir_sectors;
    printf("Writing Root Dir at Sector: %d\n", datasect);
    fseek(fp, datasect * boot_sector->bytes_per_sector, SEEK_SET);
    fwrite(root, sizeof(fat_direntry_t), 1, fp);    
    fclose(fp);   
    
    free(boot_sector);
    free(fsinfo);
    free(root);
    return EXIT_SUCCESS;
}
