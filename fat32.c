/*
 * @file: fat32.c
 *
 * @author: Kevin Allison
 *
 * Provide common functions for FAT32 filesystems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "fat32.h"

/* Extern Definitions */
uint8_t DskTableFAT16_NumEntries = 8;
DskSiztoSecPerClus_t DskTableFAT16[] = {
    {8400, 0}, {32680, 2}, {262144, 4},
    {524288, 8}, {1048576, 16}, {2097152, 32}, 
    {4194304, 64},{0xFFFFFFFF, 64}
};

uint8_t DskTableFAT32_NumEntries = 6;
DskSiztoSecPerClus_t DskTableFAT32[] = {
    {66600, 0}, {32680, 1}, {16777216, 8},
    {33554432, 16}, {67108864, 32}, {0xFFFFFFFF, 64}
};

fat_t *fat_table[MOUNT_LIMIT];

void update_fsinfo(const char *device_name, fat_fsinfo_t *info) {
    int device = open(device_name, O_WRONLY);
    lseek(device, 1000, SEEK_SET);
    write(device, info, 8);
    close(device);
}

/* This need to load the BootSector, 
*  Check the Info Sector and Load the FAT Table */
int fat32_init(int dev) { //const char *device_name) {
    const char *device_name = mount_table[dev]->device_name;
    int device = open(device_name, O_RDONLY);
    if (device < 0) { perror("fat32"); exit(EXIT_FAILURE); }
    fat_t *fat = calloc(1, sizeof(fat_t));
    fat_BS_t *bs = calloc(1, sizeof(fat_BS_t));
    fat->bs = bs;
    
    int rd = read(device, fat->bs, 90);
    if (rd <= 0) {
        close(device);
        perror("fat32");
        return -1;
    }
    
    lseek(device, 910, SEEK_CUR);
    fat->info = calloc(1, sizeof(fat_fsinfo_t));
    rd = read(device, fat->info, 8);
    if (rd <= 0) {
        close(device);
        perror("fat32");
        return -1;
    }
      
    printf("Free Clusters Count: %d\n", fat->info->num_free_clusters);
    printf("Last Allocd Cluster: 0x%08X\n", fat->info->last_alloc);
    printf("Sectors Per Cluster: %d\n", fat->bs->sectors_per_cluster);
    
    /* Load the FAT Table after computing its position */
    int root_dir_sectors = ((fat->bs->root_entry_count * 32) + (fat->bs->bytes_per_sector - 1)) / fat->bs->bytes_per_sector;    
    int tblsize = (fat->bs->table_size_16 != 0) ? fat->bs->table_size_16 : ((fat_extBS_32_t*)fat->bs->extended_section)->table_size_32;    
    
    fat->data_sect = fat->bs->reserved_sector_count + (fat->bs->table_count * tblsize) + root_dir_sectors;
    int n_sectors = ((fat->bs->total_sectors_16 != 0) ? fat->bs->total_sectors_16 : fat->bs->total_sectors_32) - fat->data_sect;
    fat->n_clusters = n_sectors / fat->bs->sectors_per_cluster;
    fat->fs_type = (fat->n_clusters < 65525) ? FAT16 : FAT32;
    int n_free = 0;
    
    printf("Size of FAT: %d\n", fat->n_clusters);
    lseek(device, fat->bs->reserved_sector_count * fat->bs->bytes_per_sector, SEEK_SET);
    for (int i = 0; i < fat->n_clusters; i++) {
        /* Read each cluster and check if it is free or not */
        int cluster = 0;
        read(device, &cluster, 4);
        if ((cluster & 0x0FFFFFFF) < 0x0FFFFFF7) ++n_free; else fat->info->last_alloc = i;
    }
    printf("Number of Free Clusters: %d\n", n_free);
    
    fat->info->num_free_clusters = n_free;
    
    close(device);
    
    update_fsinfo(device_name, fat->info);

    fat_table[dev] = fat;
    return 0;
}

int fat32_read(void *buffer, int count) {
    return 0;
}

/*
 * Compute the byte offset from SEEK_SET of a given cluster
 * 
 * @param   fat             FAT Information Struct containing necessary values to compute location
 * @param   cluster         Relative cluster in the data section
 *
 * @return  Offset in bytes from SEEK_SET of this cluster
 */
inline static int get_cluster_location(fat_t *fat, int cluster) {
    return (fat->data_sect + (fat->bs->sectors_per_cluster * (cluster - 2))) * fat->bs->bytes_per_sector;
}

/* 
 * Retrieves the value stored in the FAT table indicating
 * whether this cluster is available for use or not
 *
 * @param   device          Device to read from (i.e. /dev/sda1 or a file), already opened
 * @param   fat             FAT Information to use to compute cluster/sector location
 * @param   cluster         Cluster to index into the FAT Table to retrieve
 *
 * @return  Value stored in the FAT Table at cluster cluster.
 */
unsigned int read_fat_table(int device, fat_t* fat, int cluster) {
    unsigned char FAT[fat->bs->bytes_per_sector];
    unsigned int  fat_offset = cluster * 4;
    unsigned int  fat_sector = fat->bs->reserved_sector_count + (fat_offset / fat->bs->bytes_per_sector);
    unsigned int  ent_offset = fat_offset % fat->bs->bytes_per_sector;
    
    //printf("[FAT_READ]: Using cluster: [%d] at offset [%d] in sector [%d] at entry [%d]\n", cluster, fat_offset, fat_sector, ent_offset);
    lseek(device, fat_sector * fat->bs->bytes_per_sector, SEEK_SET);
    read(device, FAT, fat->bs->bytes_per_sector);
    unsigned int tbl_val = *(unsigned int*)&FAT[ent_offset] & 0x0FFFFFFF;
    
    //printf("[FAT_READ]: Tbl Value: 0x%08X\n", tbl_val);
    return tbl_val;
}

/* 
 * Writes the value passed in to the FAT table
 *
 * @param   device          Device to read/write from (i.e. /dev/sda1 or a file), already opened
 * @param   fat             FAT Information to use to compute cluster/sector location
 * @param   cluster         Cluster of FAT to write to
 * @param   value           Value to write
 *
 * @return  Cluster written to
 */
unsigned int write_fat_table(int device, fat_t* fat, unsigned int cluster, unsigned int value) {
    unsigned char FAT[fat->bs->bytes_per_sector];
    unsigned int  fat_offset = cluster * 4;
    unsigned int  fat_sector = fat->bs->reserved_sector_count + (fat_offset / fat->bs->bytes_per_sector);
    unsigned int  ent_offset = fat_offset % fat->bs->bytes_per_sector;
    
    /* Populate FAT */
    lseek(device, fat_sector * fat->bs->bytes_per_sector, SEEK_SET);
    read(device, FAT, fat->bs->bytes_per_sector);
    
    /* Crazy ass way of writing according to the MS FAT 1.03 Specification */
    *((unsigned int*)&FAT[ent_offset]) = (*((unsigned int*)&FAT[ent_offset])) & 0xF0000000;
    *((unsigned int*)&FAT[ent_offset]) = (*((unsigned int*)&FAT[ent_offset])) | (value & 0x0FFFFFFF);

    
    printf("[FAT_WRITE]: Using cluster: [%d] at offset [%d] in sector [%d] at entry [%d]\n", cluster, fat_offset, fat_sector, ent_offset);
    //lseek(device, fat_sector * fat->bs->bytes_per_sector, SEEK_SET);
    //write(device, FAT, fat->bs->bytes_per_sector);
    
    printf("[FAT_WRITE]: Wrote Value: 0x%08X\n", *(unsigned int*)&FAT[ent_offset]);
    return cluster;
}

unsigned char * process_long_entry(unsigned char *buff, int *offcount) {
    fat_long_direntry_t *ent = (fat_long_direntry_t*)buff;
    
    int seq = 0;
    if ((ent->order & 0x40) != 0x40) {
        printf("[FAT32]: Not Start of Long Filename. Skipping\n");
        return NULL;
    }
    
    seq = ent->order & 0x9F;
    int num_char = seq * 13;
    unsigned char *str = calloc(num_char, sizeof(char));    
    num_char--;
    
    for (int i = seq; i > 0; i--) {   
        fat_long_direntry_t *ent = (fat_long_direntry_t*)buff;
        str[num_char--] = (char)(ent->charset3[1]);
        str[num_char--] = (char)(ent->charset3[0]);
        str[num_char--] = (char)(ent->charset2[5]);
        str[num_char--] = (char)(ent->charset2[4]);
        str[num_char--] = (char)(ent->charset2[3]);
        str[num_char--] = (char)(ent->charset2[2]);
        str[num_char--] = (char)(ent->charset2[1]);
        str[num_char--] = (char)(ent->charset2[0]);
        str[num_char--] = (char)(ent->charset1[4]);
        str[num_char--] = (char)(ent->charset1[3]);
        str[num_char--] = (char)(ent->charset1[2]);
        str[num_char--] = (char)(ent->charset1[1]);
        str[num_char--] = (char)(ent->charset1[0]);
        
        if (seq != 1) {
            buff += 32; /* Increase Buffer by 32 to look at next entry */
        }
        ++(*offcount);
    }
    
    return str;
}

/* unsigned char * read_dir_entry(int device, fat_t *fat, unsigned int cluster) {

    unsigned char *filename = NULL;
    unsigned char buff[32];
    int nr = read(device, buff, 32);
    if ((   nr < 32) != 0) {
        printf("[FAT32] Error reading directory\n");
        return NULL;
    }
    if (buff[11] == 0x0F) {
        filename = process_long_entry(device, (fat_long_direntry_t*)buff);
    } else {
        // * Regular Entry
        fat_direntry_t *dir = (fat_direntry_t*)buff;
        
        switch (dir->attributes) {
            case 0x02:
            case 0x08:
            case 0x40:
                break;
            default:
                //printf("%s\n", (filename != NULL ? filename : dir->name));
                if (filename == NULL) { filename = dir->name; } //printf("%s\n", filename); free(filename); filename = NULL; }
        }
    }
    
    return filename;
} */

int find_dir(const char *path) {
    return -1;
}

// Offset is # of 32-bit entries from the start of the cluster
dir_entry_t fat32_readdir(dir_t *dir) {
    /* Get the FAT Information from the table of open mounted FATs */
    fat_t *fat = fat_table[dir->device];
    /* Open Device */
    int device = open(mount_table[dir->device]->device_name, O_RDWR);
    int rootdir = fat->fs_type == FAT16 ? 
        fat->bs->reserved_sector_count + (fat->bs->table_count * fat->bs->total_sectors_16) : 
        ((fat_extBS_32_t*)fat->bs->extended_section)->root_cluster;    
        
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    int cluster = (dir->offset * 4) / cluster_size;
    /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        rootdir = read_fat_table(device, fat, rootdir);
    }
    
    int fat_offset = get_cluster_location(fat, rootdir);
    lseek(device, fat_offset, SEEK_SET);
    unsigned char buff[cluster_size];
    read(device, buff, cluster_size);
    
    dir_entry_t de;
    unsigned char *filename = NULL;
    for (int i = (dir->offset * 32); i < cluster_size / 4; i += 32) {
        unsigned char *b = buff + i;
        if (b[0] == 0x00) { de.name = NULL; break; }
        if (b[11] == 0x0F) {
            int off = 0;
            int *off_ref = &off;
            filename = process_long_entry(b, off_ref);
            dir->offset += off;
            i += ((off - 1) * 32);
        } else {
            /* Regular Entry */        
            switch (((fat_direntry_t*)b)->attributes) {
                case 0x02:
                case 0x08:
                case 0x40:
                    dir->offset++;
                    continue;
                default:
                    //printf("%s\n", (filename != NULL ? filename : dir->name));
                    if (filename == NULL) { filename = ((fat_direntry_t*)b)->name; } //printf("%s\n", filename); free(filename); filename = NULL; }
            }
            dir->offset++;
            de.name = (char*)filename;
            break;
        }
    }    
    
    close(device);   
    
    return de;
}

int fat32_write(int file, const void* buffer, int count) {
    /* Get the FAT Information from the table of open mounted FATs */
    fileinfo_t *fp = filetable[file];
    fat_t *fat = fat_table[fp->device];
    
    /* Determine # of clusters needed */
    int clusize = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    int clusters_needed = count / clusize + (count % clusize == 0 ? 0 : 1);
    printf("Allocating [%d] clusters of size [%d] bytes\n", clusters_needed, clusize);
    
    /* Open Device */
    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    
    /* Build CLuster Chain */
    unsigned int first_cluster = 0;
    unsigned int last_cluster = 0;
    unsigned int cluster = fat->info->last_alloc;
    unsigned int filled_clusters = 0;
    while (filled_clusters < clusters_needed) {
        ++cluster;
        /* Find an empty cluster */        
        while (read_fat_table(device, fat, cluster) >= 0x0FFFFFF7) ++cluster;

        printf("Found free cluster [%d]\n", cluster);
        if (filled_clusters > 0) write_fat_table(device, fat, last_cluster, cluster);
        else first_cluster = cluster;
        last_cluster = cluster;
        ++filled_clusters;
    }
    write_fat_table(device, fat, last_cluster, 0x0FFFFFFF);
    
    /* Update Directory Table */
    int rootdir = fat->fs_type == FAT16 ? 
        fat->bs->reserved_sector_count + (fat->bs->table_count * fat->bs->total_sectors_16) : 
        ((fat_extBS_32_t*)fat->bs->extended_section)->root_cluster;    
    fat_direntry_t *direntry;
    
    //read_dir_entry(device, fat, rootdir);
    
    close(device);
    return 0;
}

int fat32_teardown() {
    return 0;
}
