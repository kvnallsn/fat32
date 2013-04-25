/*
 * @file: futil.c
 *
 * @author: Kevin Allison
 *
 * Utility Program for FAT32 Filesystems
 */

#include "futil.h"

file_t filetable[FILE_LIMIT];
dir_t *dirtable[FILE_LIMIT];

int next_file_pos = 0;

fs_table_t fs_table[] = {
    {fat32_init, fat32_openfile, fat32_readfile, fat32_write, fat32_readdir, fat32_teardown},
    {fat32_init, fat32_openfile, fat32_readfile, fat32_write, fat32_readdir, fat32_teardown}
};

mount_t *mount_table[MOUNT_LIMIT];

void mount_fs(const char *device_name, const char *path) {
    int mount_pos;
    for (mount_pos = 0; mount_pos < MOUNT_LIMIT; mount_pos++) {
        if (mount_table[mount_pos] == NULL) { break; }
    }
    if (mount_pos == MOUNT_LIMIT) { fprintf(stderr, "Could not mount device.  No room left in table\n"); return; }
     
    mount_t *newmount = calloc(1, sizeof(mount_t));
    newmount->device_name = calloc(strlen(device_name)+1, sizeof(char));
    strncpy(newmount->device_name, device_name, strlen(device_name));
    newmount->path = calloc(strlen(path)+1, sizeof(char));
    strncpy(newmount->path, path, strlen(path));
    newmount->fs_type = FAT32;
    mount_table[mount_pos] = newmount;
    
    fs_table[newmount->fs_type].init(mount_pos);
}

void unmount_fs(const char *mount_point) {
    int mount_pos;
    for (mount_pos = 0; mount_pos < MOUNT_LIMIT; mount_pos++) {
        if (mount_table[mount_pos] != NULL) {
            if (strcmp(mount_table[mount_pos]->path, mount_point) == 0) {
                
                fs_table[mount_table[mount_pos]->fs_type].teardown();
                
                free(mount_table[mount_pos]->device_name);
                free(mount_table[mount_pos]->path);
                free(mount_table[mount_pos]);
                mount_table[mount_pos] = NULL;
                break;
            }
        }
    }
}

/*
 * Matches the path name to the actual mount point
 *
 * @param   path        Absolute path to the file
 * 
 * @return  Integer cooresponding to the position in the 
 *          file mount table
 */
int get_device(const char *path) {
    int best_match = -1;
    int highest_char_match = -1;
    for (int i = 0; i < MOUNT_LIMIT; i++) {
        if (mount_table[i] != NULL) {
            int match_count = 0;
            for (int j = 0; j < strlen(mount_table[i]->path); j++) {
                if (mount_table[i]->path[j] == path[j]) ++match_count;
            }
            if (match_count > highest_char_match) {highest_char_match = match_count; best_match = i;};
        }
    }
    return best_match;
}

/* 
 * Insert a file/directory entry into the filetable
 */
int get_next_table_pos(void *table, int limit) {
    for (int fpos = 0; fpos < FILE_LIMIT; fpos++) {
        if (filetable[fpos].name == NULL) return fpos;
    }
    return -1;
}

int opendir(const char *path) {    
    dir_t *dir = calloc(1, sizeof(dir_t));
    
    dir->path = calloc(strlen(path), sizeof(char));
    strncpy(dir->path, path, strlen(path));
    dir->device = get_device(path);
    dir->offset = 0;
    
    int pos = get_next_table_pos(dirtable, FILE_LIMIT);
    //printf("pos: %d\n", pos);
    if (pos != -1) dirtable[pos] = dir;
    
    return pos;
}

dir_entry_t readdir(int dir) {
    dir_t *dir_info = dirtable[dir];
    return fs_table[mount_table[dir_info->device]->fs_type].readdir(dir_info);
}

void changedir(char *dirname) {

    file_t file;
    file.name = strrchr(dirname, '/');
    file.name++;    // Increase 1 past the last /
    file.path = dirname;
    file.device = get_device(dirname);
    file.directory = 1;
    file.offset = 0;
    file.size = 0;

    fs_table[mount_table[file.device]->fs_type].openfile(-1, &file, 1);
}

void closedir(int dir) {
    if (dir > FILE_LIMIT) { return; }
    
    // Flush All Changes Here
    
    dir_t *directory = dirtable[dir];
    dirtable[dir] = NULL;
    
    if (directory->path != NULL) free(directory->path);
    if (directory != NULL) free(directory);
}

int fileopen(const char *fname) {
    
    int pos = get_next_table_pos(filetable, FILE_LIMIT);
    char *npos = strrchr(fname, '/'); int sz = strlen(++npos);    
    filetable[pos].name = calloc(sz, sizeof(char));
    strncpy(filetable[pos].name, npos, sz);
    
    filetable[pos].path = calloc(strlen(fname), sizeof(char));
    strncpy(filetable[pos].path, fname, strlen(fname));
    filetable[pos].directory = opendir(filetable[pos].path);
    
    filetable[pos].device = get_device(fname);
    
    // byte offset in file
    filetable[pos].offset = 0;
    filetable[pos].size = 0;
        
    mount_t *mp = mount_table[filetable[pos].device];
    fs_table[mp->fs_type].openfile(pos, &filetable[pos], 0);
    
    return pos;
}

int filewrite(int file, const char *buffer, int count) {
    if (file > FILE_LIMIT) { return -1; }
    file_t *fp = &(filetable[file]);
    mount_t *mp = mount_table[fp->device];
    
    return fs_table[mp->fs_type].write(file, buffer, count);
}

int fileread(int file, char *buffer, int count) {
    if (file > FILE_LIMIT) { return -1; }
    file_t *fp = &(filetable[file]);
    mount_t *mp = mount_table[fp->device];
    
    int num_read = fs_table[mp->fs_type].read(file, buffer, count);

    if (num_read < count) buffer[num_read] = '\0';

    return num_read;
}

void fileclose(int file) {
    if (file > FILE_LIMIT) { return; }
    
    // Flush All Changes Here
    
    file_t *fp = &(filetable[file]);
    //filetable[file] = NULL;
    
    if (fp->name != NULL) free(fp->name);
}
