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

fat_t fat_table[MOUNT_LIMIT];

fat_direntry_t fat_file_table[FILE_LIMIT];

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


/*********** Local Functions ***************/

/*
 * Compute the checksum for the DOS filename
 *
 * @param   filename        8.3 Filename to compute the checksum of
 *
 * @return  Checksum of the 8.3 filename
 */
unsigned char lfn_checksum(const unsigned char *filename) {
   int i;
   unsigned char sum = 0;
 
   for (i = 11; i; i--)
      sum = ((sum & 1) << 7) + (sum >> 1) + *filename++;
 
   return sum;
}

/*
 * Generates and 8.3 representation of a filename
 * 
 * @param   input       Filename to convert to 8.3
 *
 * @return  The 8.3 representation of the filename
 */
char * gen_basis_name(char *input) {
    int len = strlen(input);
    char *shortname = calloc(12, sizeof(char));
    int start;
    for (start = 0; start < len && (input[start] == 0x20 || input[start] == 0x2e); start++);
    
    int char_copied = 0;
    int pos;
    for (pos = start; pos < len && input[pos] != 0x2E && char_copied < 8; pos++) {
        char c;
        switch (input[pos]) {
        case 0x20:
            printf("stop\n");
            break;
        case 0x21:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x40:
        case 0x5E:
        case 0x5F:
        case 0x60:
        case 0x7B:
        case 0x7D:
        case 0x7E:
            c = '_';
            shortname[char_copied++] = c; 
            break;
        default:
            c = to_upper(input[pos]);
            shortname[char_copied++] = c; 
        }       
    }

    int ext;
    for (ext = len - 1; ext > pos; ext--) {
        if (input[ext] == 0x2E) break;
    }

    //if (ext >= pos) shortname[char_copied++] = '.';
    for (;char_copied < 8; char_copied++) shortname[char_copied] = ' ';
    for (int i = ext+1; i < len && char_copied < 11; i++) {
        shortname[char_copied++] = to_upper(input[i]);
    } 
    
    return shortname;    
}

fat_long_direntry_t build_long_entry(int order, char *input, unsigned char *shortname) {
    fat_long_direntry_t long_ent;
    long_ent.order = order;
    long_ent.attribute = 0x0F;
    long_ent.type = 0x00;
    long_ent.checksum = lfn_checksum(shortname);
    long_ent.zero = 0x0000;
    
    order = order & 0x9F;
    int pos = (order - 1) * 13;
    int len = strlen(input);
    
    long_ent.charset1[0] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset1[1] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset1[2] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset1[3] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset1[4] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset2[0] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset2[1] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset2[2] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset2[3] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset2[4] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset2[5] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset3[0] = (pos < len) ? input[pos++] : 0x0000;
    long_ent.charset3[1] = (pos < len) ? input[pos++] : 0x0000;
    
    return long_ent;
}

void update_fsinfo(const char *device_name, fat_fsinfo_t *info) {
    int device = open(device_name, O_WRONLY);
    lseek(device, 1000, SEEK_SET);
    write(device, info, 8);
    close(device);
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
    lseek(device, fat_sector * fat->bs->bytes_per_sector, SEEK_SET);
    write(device, FAT, fat->bs->bytes_per_sector);
    
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

dir_entry_t extract_dir_entry(dir_t *dir, int cluster_size, unsigned char *buff) {
    dir_entry_t de;
    unsigned char *filename = NULL;
    for (int i = (dir->offset * 32); i < cluster_size / 4; i += 32) {
        de.dir = 0;
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
                case 0x10:
                    de.dir = 1;
                default:
                    //printf("%s\n", (filename != NULL ? filename : dir->name));
                    if (filename == NULL) { filename = ((fat_direntry_t*)b)->name; } //printf("%s\n", filename); free(filename); filename = NULL; }
            }
            dir->offset++;
            de.name = (char*)filename;
            de.misc = b;
            break;
        }
    }   
    return de;
}

int fat32_read(int dev, int cluster, int offset, void *buffer, int count) {

    fat_t fat = fat_table[dev];        
    
    int device = open(mount_table[dev]->device_name, O_RDONLY);
    /* Read count bytes */
    int fat_data_loc = get_cluster_location(&fat, cluster);    
    lseek(device, fat_data_loc+offset, SEEK_SET);    
    int nr = read(device, buffer, count);
    if (nr < 0) perror("read");
    close(device);
    return nr;
}

/*********** Exported Functions ***************/

/*  This need to load the BootSector, 
 *  Check the Info Sector and Load the FAT Table 
 */
int fat32_init(int dev) { //const char *device_name) {
    const char *device_name = mount_table[dev]->device_name;
    int device = open(device_name, O_RDONLY);
    if (device < 0) { perror("fat32"); exit(EXIT_FAILURE); }
    //fat_t *fat = calloc(1, sizeof(fat_t));
    fat_t fat;
    fat_BS_t *bs = calloc(1, sizeof(fat_BS_t));
    fat.bs = bs;
    
    int rd = read(device, fat.bs, 90);
    if (rd <= 0) {
        close(device);
        perror("fat32");
        return -1;
    }
    
    lseek(device, 910, SEEK_CUR);
    fat.info = calloc(1, sizeof(fat_fsinfo_t));
    rd = read(device, fat.info, 8);
    if (rd <= 0) {
        close(device);
        perror("fat32");
        return -1;
    }
      
    printf("Free Clusters Count: %d\n", fat.info->num_free_clusters);
    printf("Last Allocd Cluster: 0x%08X\n", fat.info->last_alloc);
    printf("Sectors Per Cluster: %d\n", fat.bs->sectors_per_cluster);
    
    /* Load the FAT Table after computing its position */
    int root_dir_sectors = ((fat.bs->root_entry_count * 32) + (fat.bs->bytes_per_sector - 1)) / fat.bs->bytes_per_sector;    
    int tblsize = (fat.bs->table_size_16 != 0) ? fat.bs->table_size_16 : ((fat_extBS_32_t*)fat.bs->extended_section)->table_size_32;    
    
    fat.data_sect = fat.bs->reserved_sector_count + (fat.bs->table_count * tblsize) + root_dir_sectors;
    int n_sectors = ((fat.bs->total_sectors_16 != 0) ? fat.bs->total_sectors_16 : fat.bs->total_sectors_32) - fat.data_sect;
    fat.n_clusters = n_sectors / fat.bs->sectors_per_cluster;
    fat.fs_type = (fat.n_clusters < 65525) ? FAT16 : FAT32;
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
    current_directory = fat.fs_type == FAT16 ? 
    fat.bs->reserved_sector_count + (fat.bs->table_count * fat.bs->total_sectors_16) : 
    ((fat_extBS_32_t*)fat.bs->extended_section)->root_cluster;    
    
    update_fsinfo(device_name, fat.info);

    fat_table[dev] = fat;
    
    return 0;
}

/* 
 * Open a file - Reads in the entry from the directory table
 *
 * @param file      File to open
 */
int fat32_openfile(int pos, file_t *file, int cd) {
    fat_t fat = fat_table[file->device];
    fat_direntry_t dirent;
    
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
    
    int device = open(mount_table[dir.device]->device_name, O_RDWR);
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int cluster = (dir.offset * 4) / cluster_size;     // 4 for # of bytes in 32-bits, cluster to stop at
   
   /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        current_cluster = read_fat_table(device, &fat, current_cluster);
    }

    int fat_offset = get_cluster_location(&fat, current_cluster);    
    while (1) {
        /* Look for name */        
        lseek(device, fat_offset, SEEK_SET);
        unsigned char buff[cluster_size];
        read(device, buff, cluster_size);
        
        dir_entry_t file;
        fat_direntry_t *dirent_p; 
        
        while ((file = extract_dir_entry(&dir, cluster_size, buff)).name != NULL &&
                strcmp(file.name, lvl) != 0);

        if (file.name == NULL) {
            printf("Not found\n");
            break;
        }
        dirent_p = (fat_direntry_t*)file.misc;
        if (dirent_p == NULL) {printf("ERROR\n"); break;};
        dirent = *dirent_p;
        // If its a directory, reload info and recurse into it
        if (dirent.attributes == 0x10) {
            fat_offset =  get_cluster_location(&fat, (dirent.high_clu << 16) | dirent.low_clu);   
            strcat(dir.path, lvl);
            strcat(dir.path, "/");
            dir.offset = 0;
            if (cd == 1 && strcmp(file.name, lvl) == 0) {
                current_directory = (dirent.high_clu << 16) | dirent.low_clu;
                break;
            }
            lvl = strtok(NULL, "/");
            if (lvl == NULL) break;
        } else {
            break;
        }
    }
    
    close(device);
    
    if (pos > 0) fat_file_table[pos] = dirent;
    return pos;
}

// Offset is # of 32-bit entries from the start of the cluster
dir_entry_t fat32_readdir(dir_t *dir) {
    /* Get the FAT Information from the table of open mounted FATs */
    fat_t fat = fat_table[dir->device];
    /* Open Device */
    int device = open(mount_table[dir->device]->device_name, O_RDWR);
    int rootdir = current_directory;  
        
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int cluster = (dir->offset * 4) / cluster_size;     // 4 for # of bytes in 32-bits
    /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        rootdir = read_fat_table(device, &fat, rootdir);
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
void fat32_writedir(file_t *file, int startclu) {
    dir_t *dir = dirtable[file->directory];
    /* Get the FAT Information from the table of open mounted FATs */
    fat_t fat = fat_table[dir->device];
    /* Open Device */
    int device = open(mount_table[dir->device]->device_name, O_RDWR);
    int rootdir = fat.fs_type == FAT16 ? 
        fat.bs->reserved_sector_count + (fat.bs->table_count * fat.bs->total_sectors_16) : 
        ((fat_extBS_32_t*)fat.bs->extended_section)->root_cluster;    
        
    int cluster_size = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int cluster = (dir->offset * 4) / cluster_size;
    /* Traverse FAT Table */
    for (int i = 0; i < cluster; i++) {
        rootdir = read_fat_table(device, &fat, rootdir);
    }
    
    printf("[FAT_WRITE]: Dir at: %d\n", rootdir);
    
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
    for (int i = (dir->offset * 32); i < cluster_size / 4; i += 32) {
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


int fat32_readfile(int file, void *buffer, int count) {
    file_t *fp = &(filetable[file]);
    
    fat_direntry_t *fat_dirent = &(fat_file_table[file]);
    
    int cluster = (fat_dirent->high_clu << 16) | fat_dirent->low_clu;
    
    int num_to_read = (fp->offset + count > fat_dirent->size) ? fat_dirent->size - fp->offset : count;

    int nr = fat32_read(fp->device, cluster, fp->offset, buffer, num_to_read);
    fp->offset += nr;

    return nr;
}


int fat32_write(int file, const void* buffer, int count) {
    /* Get the FAT Information from the table of open mounted FATs */
    file_t *fp = &(filetable[file]);
    fat_t fat = fat_table[fp->device];
    
    /* Determine # of clusters needed */
    int clusize = fat.bs->bytes_per_sector * fat.bs->sectors_per_cluster;
    int clusters_needed = count / clusize + (count % clusize == 0 ? 0 : 1);
    printf("[FAT_WRITE] Allocating [%d] clusters of size [%d] bytes\n", clusters_needed, clusize);
    
    /* Open Device */
    int device = open(mount_table[fp->device]->device_name, O_RDWR);
    
    /* Build CLuster Chain */
    unsigned int first_cluster = 0;
    unsigned int last_cluster = 0;
    unsigned int cluster = fat.info->last_alloc;
    unsigned int filled_clusters = 0;
    while (filled_clusters < clusters_needed) {
        ++cluster;
        /* Find an empty cluster */        
        while (read_fat_table(device, &fat, cluster) >= 0x0FFFFFF7) ++cluster;

        printf("[FAT_WRITE] Found free cluster [%d]\n", cluster);
        if (filled_clusters > 0) write_fat_table(device, &fat, last_cluster, cluster);
        else first_cluster = cluster;
        
        /* Write data at current cluster */
        lseek(device, get_cluster_location(&fat, cluster), SEEK_SET);
        write(device, buffer, count);
        last_cluster = cluster;
        ++filled_clusters;
    }
    write_fat_table(device, &fat, last_cluster, 0x0FFFFFFF);

    /* Dir Table */
    fat32_writedir(fp, first_cluster);
       
    close(device);
    return 0;
}

int fat32_teardown() {
    return 0;
}
