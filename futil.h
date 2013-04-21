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

#include "fs_types.h"
#include "fat32.h"

void mount_fs(const char *device_name, const char *path);
void unmount_fs(const char *mount_point);

int opendir(const char *path);
char * readdir(int dir);
int fileopen(const char *fname);
int filewrite(int file, const char *buffer, int count);
void fileclose(int file);
#endif
