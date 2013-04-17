/*
 * @file: futil.h
 * 
 * @author: Kevin Allison
 * @date: 11 Apr 13
 *
 * Contains structures and function definitions
 * for the FAT32 filesystem
 */

#ifndef FUTIL_XINU_HEADER
#define FUTIL_XINU_HEADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat32.h"

#define MOUNT_LIMIT     10
#define FILE_LIMIT      255

typedef struct mount_s {
    char      *device_name;
    char      *path;
    int       fs_type;
} mount_t;

typedef struct fileinfo_s {
    char    *name;
    int     device;
} fileinfo_t;

typedef struct fs_table_s {
    int (*init)(int);
    int (*read)(void*,int);
    int (*write)(int, const void*,int);
    int (*teardown)();
} fs_table_t;

extern fileinfo_t *filetable[];
extern fs_table_t fs_table[];
extern mount_t *mount_table[];

void mount_fs(const char *device_name, const char *path);
void unmount_fs(const char *mount_point);

int fileopen(const char *fname);
int filewrite(int file, const char *buffer, int count);
void fileclose(int file);
#endif