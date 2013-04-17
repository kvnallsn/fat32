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

int get_sector_location(fat_t *fat, int cluster) {
    int fatoffset = fat->fs_type == FAT16 ? cluster * 2 : cluster * 4;
    int fatsector = fat->bs->reserved_sector_count + (fatoffset / fat->bs->bytes_per_sector);
    return fatsector;
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

int fat32_write(int dev, const void* buffer, int count) {
    /* Get the FAT Information from the table of open mounted FATs */
    fat_t *fat = fat_table[dev];
    
    
    /* Determine # of clusters needed */
    int clusize = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    int clusters_needed = count / clusize + (count % clusize == 0 ? 0 : 1);
    printf("Allocating [%d] clusters of size [%d] bytes\n", clusters_needed, clusize);
    
    /* Open Device */
    int device = open(mount_table[dev]->device_name, O_RDWR);
    
    int cluster = fat->info->last_alloc;
    int filled_clusters = 0;
    while (filled_clusters < clusters_needed) {
        ++cluster;
        /* Find an empty cluster */        
        while (read_fat_table(device, fat, cluster) >= 0x0FFFFFF7) ++cluster;

        printf("Found free cluster [%d]\n", cluster);
        write_fat_table(device, fat, cluster, 0x0FFFFFFF);
        ++filled_clusters;
    }
    close(device);
    return 0;
}

int fat32_teardown() {
    return 0;
}