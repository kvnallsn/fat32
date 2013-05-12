/*
 * @file: skinny28.h
 * 
 * @author: Kevin Allison
 * @date: 05 May 13
 *
 * Contains structures and function definitions
 * for the Skinny28 filesystem
 */

#ifndef SKINNY28_XINU_HEADER
#define SKINNY28_XINU_HEADER

#include <stdint.h>
#include "fs_types.h"
#include "fat_common.h"

typedef struct skinny_vers {
    unsigned int vcurr;
    unsigned int v1;
    unsigned int v2;
    unsigned int v3;
}__attribute__((packed)) skinny_vers_t;

/* Extern Variables */
extern uint8_t              DskTableSKINNY32_NumEntries;
extern DskSiztoSecPerClus_t DskTableSKINNY32[];

/* Functions */
int skinny28_init(int dev);
int skinny28_createfile(int pos, file_t *file, int dir);
int skinny28_openfile(int pos, file_t *file, int cd);
int skinny28_getrevision(int file, int index);
int skinny28_revert(int file, int revision);
int skinny28_printrevision(int file, void *buffer, int count, int revision);
int skinny28_readfile(int file, void *buffer, int count);
int skinny28_deletefile(file_t *file);
int skinny28_write(int file, const void* buffer, int count);
dir_entry_t skinny28_readdir(dir_t *dir);
int skinny28_teardown();
#endif
