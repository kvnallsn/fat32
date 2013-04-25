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
dir_entry_t readdir(int dir);
void changedir(char *dirname);
void closedir(int dir);

int fileopen(const char *fname);
int filewrite(int file, const char *buffer, int count);

/*
 * Read a file opened with fileopen, placing the contents into buffer
 * 
 * @param   file        File id to read from
 * @param   buffer      char* buffer to read data into
 * @param   count       Number of bytes to read into buffer
 *
 * @return  -1 for Error, 0 for EOF, else total number of bytes read. 
 */
int fileread(int file, char *buffer, int count);
void fileclose(int file);
#endif
