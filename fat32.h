/*
 * @file: fat32.h
 * 
 * @author: Kevin Allison
 * @date: 11 Apr 13
 *
 * Contains structures and function definitions
 * for the FAT32 filesystem
 */

#ifndef FAT32_XINU_HEADER
#define FAT32_XINU_HEADER

#include "fat_common.h"
#include "fs_types.h"

/* Extern Variables */
extern uint8_t              DskTableFAT16_NumEntries;
extern uint8_t              DskTableFAT32_NumEntries;
extern DskSiztoSecPerClus_t DskTableFAT16[];
extern DskSiztoSecPerClus_t DskTableFAT32[];

/* Functions */
int fat32_init(int dev);
int fat32_createfile(int pos, file_t *file, int dir);
int fat32_openfile(int pos, file_t *file, int cd);
int fat32_readfile(int file, void *buffer, int count);
int fat32_deletefile(file_t *file);
int fat32_write(int file, const void* buffer, int count);
dir_entry_t fat32_readdir(dir_t *dir);
int fat32_teardown();
#endif
