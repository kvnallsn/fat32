/*
 * @file: .c
 *
 * @author: Kevin Allison
 *
 * Provide common functions for  filesystems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "skinny28.h"

/* Extern Definitions */
uint8_t DskTableSKINNY28_NumEntries = 6;
DskSiztoSecPerClus_t DskTable[] = {
    {66600, 0}, {32680, 1}, {16777216, 8},
    {33554432, 16}, {67108864, 32}, {0xFFFFFFFF, 64}
};

fat_t skinny_table[MOUNT_LIMIT];

fat_file_t skinny_file_table[FILE_LIMIT];

// Store the cluster number of current directory
int current_directory;          

/*********** Inline Functions ***************/

/*
 * Converts a lowercase ASCII character to an uppercase one
 *
 * @param   c       Lowercase character to convert
 *
 * @return  Uppercase varient of c
 */
inline static char to_upper(char c) {
    return (c > 0x60 && c < 0x7B) ? (c - 0x20) : c;
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

/*********** FAT Table Manipulation ********/

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
unsigned int read_skinny_table(int device, fat_t* fat, int cluster) {
    unsigned char FAT[fat->bs->bytes_per_sector];
    unsigned int  fat_offset = cluster * 4;
    unsigned int  fat_sector = fat->bs->reserved_sector_count + (fat_offset / fat->bs->bytes_per_sector);
    unsigned int  ent_offset = fat_offset % fat->bs->bytes_per_sector;
    
    lseek(device, fat_sector * fat->bs->bytes_per_sector, SEEK_SET);
    read(device, FAT, fat->bs->bytes_per_sector);
    unsigned int tbl_val = *(unsigned int*)&FAT[ent_offset] & 0x0FFFFFFF;
    
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
unsigned int write_skinny_table(int device, fat_t* fat, unsigned int cluster, unsigned int value) {
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

    
    //printf("[FAT_WRITE]: Using cluster: [%d] at offset [%d] in sector [%d] at entry [%d]\n", cluster, fat_offset, fat_sector, ent_offset);
    lseek(device, fat_sector * fat->bs->bytes_per_sector, SEEK_SET);
    write(device, FAT, fat->bs->bytes_per_sector);
    
    //printf("[FAT_WRITE]: Wrote Value: 0x%08X\n", *(unsigned int*)&FAT[ent_offset]);
    return cluster;
}

/*********** Versioning Functions **********/

int find_open_vers_table(int device, fat_t *fat) {
    int vers_cluster = fat->bs->root_entry_count;       /* Vers Table Loc */
    int vers_loc = get_cluster_location(fat, vers_cluster);
    printf("Vers Table at: 0x%08X\n", vers_loc);
    
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    
    int pos = -1;
    do {
        lseek(device, get_cluster_location(fat, vers_cluster), SEEK_SET);
        for (int i = 0; i < cluster_size; i++) {
            char versbuff[sizeof(skinny_vers_t)];
            read(device, versbuff, sizeof(skinny_vers_t));
            
            if (versbuff[i] == 0x00) {
                pos = i;
                break;
            }
        }
        if (pos != -1) break;
    } while ((vers_cluster = read_skinny_table(device, fat, vers_cluster)) < 0x0FFFFFF7);
    
    if (pos == -1) { 
        // allocate new cluster 
    }
    
    skinny_vers_t vers_info = {0, 0, 0, 0};
    lseek(device, get_cluster_location(fat, vers_cluster) + (pos * sizeof(skinny_vers_t)), SEEK_SET);
    printf("Seeking to <<0x%08lX>>\n", get_cluster_location(fat, vers_cluster) + (pos * sizeof(skinny_vers_t)));
    write(device, &vers_info, sizeof(skinny_vers_t));
    printf("Using position [%d]\n", pos);
    
    return pos;
}

int get_most_recent_cluster(int device, fat_t *fat, int pos) {
    int vers_loc = get_cluster_location(fat, fat->bs->root_entry_count);    
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    if (pos > (cluster_size / 4)) { return -1; }
    
    lseek(device, vers_loc + (pos * sizeof(skinny_vers_t)), SEEK_SET);
    skinny_vers_t vers;
    read(device, &vers, sizeof(skinny_vers_t));
    
    return vers.vcurr;    
}

int insert_revision(int device, fat_t *fat, int pos, int cluster) {
    int vers_loc = get_cluster_location(fat, fat->bs->root_entry_count) + (pos * sizeof(skinny_vers_t));   
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    if (pos > (cluster_size / 4)) { return -1; }
    
    lseek(device, vers_loc, SEEK_SET);
    //printf("Seeking to <<0x%08X>>\n", vers_loc);
    skinny_vers_t vers;
    read(device, &vers, sizeof(skinny_vers_t));
       
    //printf("In cluster: [%d]\n", cluster);
    vers.v3 = vers.v2;
    vers.v2 = vers.v1;
    vers.v1 = vers.vcurr;
    vers.vcurr = cluster;
    
    //printf("Revising...\n\tCurrent: [%d]\n\t     -1: [%d]\n\t     -2: [%d]\n\t     -3: [%d]\n", vers.vcurr, vers.v1, vers.v2, vers.v3);
    
    // rewrite the dir cluster
    lseek(device, vers_loc, SEEK_SET);
    //printf("Seeking to <<0x%08X>>\n", vers_loc);
    write(device, &vers, sizeof(skinny_vers_t));
    
    return cluster;
}

void revert_to_revision(int device, fat_t *fat, int pos, int revision) {
    int vers_loc = get_cluster_location(fat, fat->bs->root_entry_count) + (pos * sizeof(skinny_vers_t));    
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    if (pos > (cluster_size / 4)) { return; }
    
    // Read the vers table
    skinny_vers_t vers;
    lseek(device, vers_loc, SEEK_SET);
    read(device, &vers, sizeof(skinny_vers_t));
    
    // Move the revisions based on the input
    switch (revision) {
        case 3:
            vers.vcurr = vers.v3;
            vers.v3 = 1;
            vers.v2 = 0;
            vers.v3 = 0;
            break;
        case 2:
            vers.vcurr = vers.v2;
            vers.v1 = vers.v3;
            vers.v2 = 0;
            vers.v3 = 0;
        case 1:
            vers.vcurr = vers.v1;
            vers.v1 = vers.v2;
            vers.v2 = vers.v3;
            vers.v3 = 0;
    }    
    
    // Rewrite the vers table
    lseek(device, vers_loc, SEEK_SET);
    write(device, &vers, sizeof(skinny_vers_t));    
}



/*********** Local Functions ***************/

void update_fsinfo(const char *device_name, fat_fsinfo_t *info) {
    int device = open(device_name, O_WRONLY);
    lseek(device, 1000, SEEK_SET);
    write(device, info, 8);
    close(device);
}

unsigned char * process_long_entry(unsigned char *buff, int *offcount) {
    fat_long_direntry_t *ent = (fat_long_direntry_t*)buff;
    
    
    int seq = 0;
    if ((ent->order & 0x40) != 0x40) {
        printf("[]: Not Start of Long Filename. Skipping\n");
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

dir_entry_t extract_dir_entry(dir_t *dir, int cluster_size, unsigned char *buff) {
    dir_entry_t de;
    unsigned char *filename = NULL;
    for (int i = (dir->offset * 32); i < cluster_size / 4; i += 32) {
        de.dir = 0;
        unsigned char *b = buff + i;

        if (b[0] == 0x00) { de.name = NULL; break; }
        if (b[0] == 0xE5) { dir->offset++; continue; }    /* Skip Deleted Entry */
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
                case 0x10:
                    de.dir = 1;
                default:
                    if (filename == NULL) { filename = ((fat_direntry_t*)b)->name; } 
            }
            dir->offset++;
            de.name = b[0] == 0x2E ? (b[1] == 0x2E ? ".." : ".") : (char*)filename;
            de.misc = b;
            break;
        }
    }   
    return de;
}

int find_dir_cluster(fat_t *fat, file_t *file) {

    int current_cluster = current_directory;;
    if(file->name[0] == '/') {
        //printf("Starting from root\n");
    } else {
        //printf("Starting from current dir\n");
    }
    
    /* Navigate to directory */
    int flen = strlen(file->name);
    char path[flen];
    strncpy(path, file->name, flen);
    char *lvl = strtok(path, "/");  
    
    /* Create DIR structure */
    dir_t dir;    
    for (int i = flen - 1; i >= 0; i--) {
        if (file->name[i] == '/') {
            if (i == 0) {
                file->name++;
                dir.path = "/";
            } else if (i == flen-1) {
                file->name[i] = '\0';
                dir.path = "/";
            } else {
                dir.path = file->name;
                file->name = &(file->name[i+1]);
                dir.path[i] = '\0';
            }
        }
    }   
    dir.offset = 0;
    dir.device = file->device;
   
    int device = open(mount_table[file->device]->device_name, O_RDONLY);
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
      
   
    do {    
        int fat_offset = get_cluster_location(fat, current_cluster);
        
        lseek(device, fat_offset, SEEK_SET);
        unsigned char buff[cluster_size];
        read(device, buff, cluster_size);
        
        dir_entry_t dirent;
        
        /* Keep going until the file is found */
        while (1) {
            dirent = extract_dir_entry(&dir, cluster_size, buff);
            if (dirent.name == NULL || strcmp(dirent.name, lvl) == 0) { break; }
        }
        
        if (dirent.name == NULL) {
            printf("%s: File Not found\n", file->name);
            break;
        } else {
            return fat_offset + (dir.offset * 32);
        }
    
    } while ((current_cluster = read_skinny_table(device, fat, current_cluster)) < 0x0FFFFFF7); 
    
    return 0;    
}

int skinny28_read(int dev, int cluster, int offset, void *buffer, int count) {

    fat_t fat = skinny_table[dev];        
    
    int device = open(mount_table[dev]->device_name, O_RDONLY);
    //int rcluster = get_most_recent_cluster(device, &fat, cluster);
    /* Read count bytes */
    int fat_data_loc = get_cluster_location(&fat, cluster);    
    lseek(device, fat_data_loc+offset, SEEK_SET);    
    int nr = read(device, buffer, count);
    if (nr < 0) perror("read");
    close(device);
    return nr;
}

/*
 * Write a buffer to the device 
 *
 * @param dev       Name of the device to write to
 * @param fat       FAT table to reference
 * @param cluster   Cluster to start writing at
 * @param offset    Current offset in the cluster to write at
 * @param buffer    Data to write
 * @param count     Amount of data in buffer to write
 *
 * @return          The total amount of data written
 */ 
int skinny28_writedata(int file, int cluster, const void *buffer, int count) { 
    // Get the FAT/File Information
    file_t *fp = &(filetable[file]);
    fat_file_t *f =  &(skinny_file_table[file]);
    fat_t *fat = &(skinny_table[fp->device]);  
    
    //printf("Curr Clu: <<%d>>\n", cluster);
    //int cluster_size = 5;
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;
    int clu_offset = fp->offset % cluster_size;    
    int total_written = 0;
    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    
    // Seek to cluster to start at
    for (int i = 0; i < (fp->offset / cluster_size); i++) {
        cluster = read_skinny_table(device, fat, cluster); 
    }    
    
    while (count > 0) {
        // Determine amount of data to write
        int amt_to_write = (clu_offset + count) >= cluster_size ? count - (cluster_size - clu_offset) : count;
            
        // Seek to cluster
        int loc = get_cluster_location(fat, cluster) + clu_offset;

        //printf("Seeking  <<0x%08X>> [%d]\n", loc, cluster);
        lseek(device, loc, SEEK_SET);
        int amt_written = write(device, buffer, amt_to_write);    
        if (amt_written < 0) { break; }
        total_written += amt_written;
        count -= amt_written;

        if (loc + amt_written > f->eof_marker) f->eof_marker = loc + amt_written;
        
        if (count > 0) { 
            int next_cluster = find_free_cluster(mount_table[fp->device]->device_name, fat, cluster+1);
            write_skinny_table(device, fat, cluster, next_cluster);
            cluster = next_cluster;
            clu_offset = 0;

        } else {
            write_skinny_table(device, fat, cluster, 0x0FFFFFFF);
        }
    }
    close(device);
    
    return total_written;
}

/*********** Exported Functions ***************/

/*  This need to load the BootSector, 
 *  Check the Info Sector and Load the FAT Table 
 */
int skinny28_init(int dev) { //const char *device_name) {
    const char *device_name = mount_table[dev]->device_name;
    int device = open(device_name, O_RDONLY);
    if (device < 0) { perror(""); exit(EXIT_FAILURE); }

    fat_t fat;
    fat_BS_t *bs = calloc(1, sizeof(fat_BS_t));
    fat.bs = bs;
    
    int rd = read(device, fat.bs, 90);
    if (rd <= 0) {
        close(device);
        perror("");
        return -1;
    }
    
    lseek(device, 910, SEEK_CUR);
    fat.info = calloc(1, sizeof(fat_fsinfo_t));
    rd = read(device, fat.info, 8);
    if (rd <= 0) {
        close(device);
        perror("");
        return -1;
    }
      
    printf("Free Clusters Count: %d\n", fat.info->num_free_clusters);
    printf("Last Allocd Cluster: 0x%08X\n", fat.info->last_alloc);
    printf("Sectors Per Cluster: %d\n", fat.bs->sectors_per_cluster);
    
    /* Load the FAT Table after computing its position */
    int root_dir_sectors = (fat.bs->bytes_per_sector - 1) / fat.bs->bytes_per_sector;    
    int tblsize = (fat.bs->table_size_16 != 0) ? fat.bs->table_size_16 : ((fat_extBS_32_t*)fat.bs->extended_section)->table_size_32;    
    
    fat.data_sect = fat.bs->reserved_sector_count + (fat.bs->table_count * tblsize) + root_dir_sectors;
    int n_sectors = ((fat.bs->total_sectors_16 != 0) ? fat.bs->total_sectors_16 : fat.bs->total_sectors_32) - fat.data_sect;
    fat.n_clusters = n_sectors / fat.bs->sectors_per_cluster;
    fat.fs_type = SKINNY28;
    int n_free = 0;
    
    printf("Size of FAT: %d\n", fat.n_clusters);
    lseek(device, fat.bs->reserved_sector_count * fat.bs->bytes_per_sector, SEEK_SET);
    for (int i = 0; i < fat.n_clusters; i++) {
        /* Read each cluster and check if it is free or not */
        int cluster = 0;
        read(device, &cluster, 4);
        if ((cluster & 0x0FFFFFFF) < 0x0FFFFFF7) ++n_free; else fat.info->last_alloc = i;
    }
    close(device);
    
    printf("Number of Free Clusters: %d\n", n_free);
    
    fat.info->num_free_clusters = n_free;
    
    // Load the rood dir as the current directory
    current_directory = ((fat_extBS_32_t*)fat.bs->extended_section)->root_cluster;    
    
    update_fsinfo(device_name, fat.info);

    skinny_table[dev] = fat;
    
    return 0;
}

int skinny28_createfile(int pos, file_t *file, int dir) {
    fat_direntry_t fat_dirent;
    
    char *bname = gen_basis_name(file->name);
    for (int i = 0; i < 11; i++) {
        fat_dirent.name[i] = bname[i];
    }
    
    fat_t *fat = &(skinny_table[file->device]);
    int device = open(mount_table[file->device]->device_name, O_RDWR);   
    // Write revision table: 
    int vers_pos = find_open_vers_table(device, fat);
    
    fat_dirent.attributes = 0x00;
    fat_dirent.time_milli = 0x00;
    fat_dirent.time = 0x0000;
    fat_dirent.date = 0x0000;
    fat_dirent.last_accessed = 0x0000;
    fat_dirent.high_clu = (vers_pos >> 16);
    fat_dirent.mod_time = 0x0000;
    fat_dirent.mod_date = 0x0000;
    fat_dirent.low_clu = (vers_pos & 0xFFFF);
    fat_dirent.size = 0x00000000;
    
    int namelen = strlen(file->name);
    int size_req = (namelen / 13) + (namelen % 13 != 0 ? 1 : 0);
    
    // Look through dir table
    int cluster = current_directory; 
    int cluster_size = fat->bs->bytes_per_sector * fat->bs->sectors_per_cluster;    
    int dir_pos = -1;
    do {
        lseek(device, get_cluster_location(fat, cluster), SEEK_SET);
        char buff[cluster_size];
        read(device, buff, cluster_size);
        
        int num_free = 0;
        int num_free_start = 0;
        for (int i = 0; i < cluster_size; i += 32) {
            if (buff[i] == 0x00) {
                // Free from here on. use it!
                dir_pos = i;
                break;
            } else if (buff[i] == 0xE5) {
                if (num_free == 0) {num_free_start = i;}
                ++num_free;
                if (num_free == size_req) {
                    dir_pos = num_free_start;
                    break;
                }
            } else {
                num_free = 0;
                num_free_start = 0;
            }
        }
        
        if (dir_pos != -1) break;
    } while ((cluster = read_skinny_table(device, fat, cluster)) < 0x0FFFFFF7);
    
    if (dir_pos == -1) { return -1; }   // No more room for files!
    
    int dir_location = get_cluster_location(fat, cluster) + dir_pos;

    // Generate Long File Names
    for (int i = size_req - 1; i >= 0; i--) {
        int order = (i+1);
        order |= (i == size_req - 1) ? 0x40 : 0x00;
        fat_long_direntry_t ld_entry = build_long_entry(order, file->name, fat_dirent.name);
        lseek(device, dir_location, SEEK_SET);
        write(device, &ld_entry, 32);
    }
    
    write(device, &fat_dirent, 32);
    
    close(device);
  
    return pos;
}

/* 
 * Open a file - Reads in the entry from the directory table
 *
 * @param file      File to open
 */
int skinny28_openfile(int pos, file_t *file, int cd) {
    fat_t fat = skinny_table[file->device];
    fat_direntry_t fat_dirent;
    
    int len = strlen(file->path);
    char *path = calloc(len+1, sizeof(char));
    strncpy(path, file->path, len);
    
    dir_t dir;
    dir.offset = 0;
    dir.device = file->device;
    dir.path = calloc(len+1, sizeof(char));
    dir.path[0] = '/';
    
    /* Navigate to directory */
    char *lvl = strtok(path, "/");
    
    int current_cluster = current_directory;
    
    int device = open(mount_table[dir.device]->device_name, O_RDONLY);
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int cluster = (dir.offset * 4) / cluster_size;     // 4 for # of bytes in 32-bits, cluster to stop at
   
   /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        current_cluster = read_skinny_table(device, &fat, current_cluster);
    }

    int fat_offset = get_cluster_location(&fat, current_cluster);    
    while (1) {
        /* Look for file */                
        lseek(device, fat_offset, SEEK_SET);
        unsigned char buff[cluster_size];
        read(device, buff, cluster_size);
        
        dir_entry_t dirent;
        fat_direntry_t *dirent_p; 
        
        while ((dirent = extract_dir_entry(&dir, cluster_size, buff)).name != NULL &&
                strcmp(dirent.name, lvl) != 0);

        if (dirent.name == NULL) {
            pos = -1;
            break;
        }
        
        dirent_p = (fat_direntry_t*)dirent.misc;
        if (dirent_p == NULL) {printf("ERROR\n"); break;};
        fat_dirent = *dirent_p;
        
        /* If its a directory, reload info and recurse into it */
        if (fat_dirent.attributes == 0x10) {
            fat_offset =  get_cluster_location(&fat, (fat_dirent.high_clu << 16) | fat_dirent.low_clu);   
            dir.offset = 0;
            /* If this is a change directory command and we found the right dir,
             * then updated the current_directory and break out of the loop */
            if (cd == 1 && strcmp(file->name, lvl) == 0) {
                current_directory = (fat_dirent.high_clu << 16) | fat_dirent.low_clu;
                if (current_directory == 0) {
                    // Reload Root Directory
                    current_directory = fat.fs_type == FAT16 ? 
                        fat.bs->reserved_sector_count + (fat.bs->table_count * fat.bs->total_sectors_16) : 
                        ((fat_extBS_32_t*)fat.bs->extended_section)->root_cluster;   
                }
                break;
            }
            lvl = strtok(NULL, "/");
            if (lvl == NULL) break;
        } else {
            skinny_file_table[pos].dir_ent = *dirent_p;
            skinny_file_table[pos].offset = dir.offset * 32 + fat_offset - 32;
            int vers_loc = (dirent_p->high_clu << 16) | dirent_p->low_clu;
            skinny_file_table[pos].beg_marker = get_cluster_location(&fat, get_most_recent_cluster(device, &fat, vers_loc));
            //printf("0x%08X\n", skinny_file_table[pos].beg_marker);
            skinny_file_table[pos].eof_marker = skinny_file_table[pos].beg_marker + dirent_p->size;
            //printf("0x%08X\n", skinny_file_table[pos].eof_marker);
            file->size = dirent_p->size;
            break;
        }
    }
    
    close(device);
    return pos;
}

// Offset is # of 32-bit entries from the start of the cluster
dir_entry_t skinny28_readdir(dir_t *dir) {
    /* Get the FAT Information from the table of open mounted FATs */
    fat_t fat = skinny_table[dir->device];
    /* Open Device */
    int device = open(mount_table[dir->device]->device_name, O_RDWR);
    int rootdir = current_directory;  
        
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int cluster = (dir->offset * 4) / cluster_size;     // 4 for # of bytes in 32-bits
    /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        rootdir = read_skinny_table(device, &fat, rootdir);
    }
    
    int fat_offset = get_cluster_location(&fat, rootdir);
    lseek(device, fat_offset, SEEK_SET);
    unsigned char buff[cluster_size];
    read(device, buff, cluster_size);
    
    dir_entry_t de = extract_dir_entry(dir, cluster_size, buff); 
   
    close(device);   
    
    return de;
}

/*
 * Update the directory table with a new file 
 */
void skinny28_writedir(file_t *file, int startclu) {
    dir_t dir;
    dir.device = file->device;
    dir.offset = 0;
    /* Get the FAT Information from the table of open mounted FATs */
    fat_t fat = skinny_table[dir.device];
    /* Open Device */
    int device = open(mount_table[dir.device]->device_name, O_RDWR);
    int rootdir = current_directory; 
        
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int cluster = (dir.offset * 4) / cluster_size;
    /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        rootdir = read_skinny_table(device, &fat, rootdir);
    }
    
    fat_direntry_t dirent;
    char *filename = gen_basis_name(file->name);
    for (int i = 0; i < 11; i++) dirent.name[i] = filename[i];
    
    dirent.attributes = 0x0;
    dirent.reserved_nt = 0;
    dirent.time_milli = 0;
    dirent.time = 0;
    dirent.date = 0;
    dirent.last_accessed = 0;
    dirent.high_clu = (startclu & 0xFFFF0000) >> 16;
    dirent.mod_time = 0;
    dirent.mod_date = 0;
    dirent.low_clu = (startclu & 0x0000FFFF);
    dirent.size = file->size;

    int num_long = strlen(file->name) / 13;
    num_long += strlen(file->name) % 13 == 0 ? 0 : 1;

    int fat_offset = get_cluster_location(&fat, rootdir);
    lseek(device, fat_offset, SEEK_SET);
    unsigned char buff[cluster_size];
    read(device, buff, cluster_size);
    
    /* Loop through dir until end is found */
    for (int i = (dir.offset * 32); i < cluster_size / 4; i += 32) {
        unsigned char *b = buff + i;
        if (b[0] == 0x00) { 
            lseek(device, fat_offset + i, SEEK_SET);
            /* Write Long Filenames, then the directory entry */
            for (int i = num_long; i > 0; i--) {
                fat_long_direntry_t de = build_long_entry(i == num_long ? i | 0x40 : i, file->name, dirent.name);
                write(device, &de, sizeof(de));
            }
            write(device, &dirent, sizeof(dirent));
            break; 
        }
    }    
   
    close(device);   
}


int skinny28_getrevision(int file, int index) {
    
    // Get the FAT/File Information
    file_t *fp = &(filetable[file]);
    fat_file_t *f =  &(skinny_file_table[file]);
    fat_t fat = skinny_table[fp->device];    
    
    int pos = (f->dir_ent.high_clu << 16) | f->dir_ent.low_clu;
    
    int vers_loc = get_cluster_location(&fat, fat.bs->root_entry_count);    
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    if (pos > (cluster_size / 4)) { return -1; }
    
    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    lseek(device, vers_loc + (pos * sizeof(skinny_vers_t)), SEEK_SET);
    skinny_vers_t vers;
    read(device, &vers, sizeof(skinny_vers_t));
    close(device);
    
    switch (index) {
        case 0: return vers.vcurr;
        case 1: return vers.v1;
        case 2: return vers.v2;
        case 3: return vers.v3;
        default: return -1;
    }

}

int skinny28_revert(int file, int revision) {

    if (revision > 3) return -1;
    
    // Get the FAT/File Information
    file_t *fp = &(filetable[file]);
    fat_file_t *f =  &(skinny_file_table[file]);
    fat_t fat = skinny_table[fp->device];   
    
    int pos = (f->dir_ent.high_clu << 16) | f->dir_ent.low_clu;
    
    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    revert_to_revision(device, &fat, pos, revision);
    close(device);
    
    return 0;
}

int skinny28_printrevision(int file, void *buffer, int count, int revision) {
    file_t *fp = &(filetable[file]);
    fat_file_t *f = &(skinny_file_table[file]);
    fat_direntry_t *fat_dirent = &(f->dir_ent);
    fat_t fat = skinny_table[fp->device]; 
    
    int rev_loc = (fat_dirent->high_clu << 16) | fat_dirent->low_clu;
    int vers_loc = get_cluster_location(&fat, fat.bs->root_entry_count);    
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    if (rev_loc > (cluster_size / 4)) { return -1; }
    
    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    lseek(device, vers_loc + (rev_loc * sizeof(skinny_vers_t)), SEEK_SET);
    skinny_vers_t vers;
    read(device, &vers, sizeof(skinny_vers_t));
    close(device);
        
    int cluster;
    switch (revision) {
        case 3: cluster = vers.v3; break;
        case 2: cluster = vers.v2; break;
        case 1: cluster = vers.v1; break;
        default: cluster = vers.vcurr;
    }
    
    if (fat_dirent->size == 0) { return 0; }
    int num_to_read = (fp->offset + count > fat_dirent->size) ? fat_dirent->size - fp->offset : count;

    int nr = skinny28_read(fp->device, cluster, fp->offset, buffer, num_to_read);

    fp->offset += nr;
    return nr;
}

int skinny28_readfile(int file, void *buffer, int count) {
    file_t *fp = &(filetable[file]);
    fat_file_t *f = &(skinny_file_table[file]);
    fat_direntry_t *fat_dirent = &(f->dir_ent);
    fat_t fat = skinny_table[fp->device]; 
    
    int pos = (fat_dirent->high_clu << 16) | fat_dirent->low_clu;
    int device = open(mount_table[fp->device]->device_name, O_RDONLY);
    int cluster = get_most_recent_cluster(device, &fat, pos);    
    close(device);
    
    if (fat_dirent->size == 0) { return 0; }
    int num_to_read = (fp->offset + count > fat_dirent->size) ? fat_dirent->size - fp->offset : count;

    int nr = skinny28_read(fp->device, cluster, fp->offset, buffer, num_to_read);

    fp->offset += nr;

    return nr;
}

int skinny28_deletefile(file_t *file) {
    // Load file
    fat_t fat = skinny_table[file->device];
    int pos = find_dir_cluster(&fat, file);
    
    int device = open(mount_table[file->device]->device_name, O_RDWR);    
    int stop = 0;
    int i = 0;
    do {
        pos -= 32;
        lseek(device, pos, SEEK_SET);
        char buff[32];
        read(device, buff, 32);
        if (buff[11] == 0x0F && (buff[0] & 0x40) == 0x40) stop = 1;
        lseek(device, pos, SEEK_SET);
        unsigned int del = 0xE5;
        write(device, &del, 1);
        lseek(device, pos, SEEK_SET);
        ++i;
    } while (!stop);
    
    close(device);
    
    return -1;
}

int skinny28_write(int file, const void *buffer, int count) {
    // Get the FAT/File Information
    file_t *fp = &(filetable[file]);
    fat_file_t *f =  &(skinny_file_table[file]);
    fat_t fat = skinny_table[fp->device];    

    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    int vers_pos = (f->dir_ent.high_clu << 16) | f->dir_ent.low_clu;
    int cluster = get_most_recent_cluster(device, &fat, vers_pos);

    // Always search for a new cluster b/c of revisions!
    // Find a cluster to start in, because the current cluster in the dir entry is 0
    cluster = find_free_cluster(mount_table[fp->device]->device_name, &fat, cluster);
    insert_revision(device, &fat, vers_pos, cluster);
     
    // reset beg/eof markers
    f->beg_marker = get_cluster_location(&fat, cluster);
    f->eof_marker = f->beg_marker;

    int wrote = skinny28_writedata(file, cluster, buffer, count);
    fp->offset += wrote;
    
    f->dir_ent.size = f->eof_marker - f->beg_marker;

    lseek(device, f->offset, SEEK_SET);
    write(device, &(f->dir_ent), 32);
    close(device);
    
    return wrote;
}

int skinny28_teardown() {
    return 0;
}
