/*
 * @file: fs_types.h
 * @author: Kevin Allison
 *
 * Header file containing structure definitons
 */
#ifndef FS_TYPES_XINU_HEADER
#define FS_TYPES_XINU_HEADER

#define     FAT16       0
#define     FAT32       1
 
#define MOUNT_LIMIT     10
#define FILE_LIMIT      255

typedef struct mount_s {
    char      *device_name;
    char      *path;
    int       fs_type;
} mount_t;

typedef struct fileinfo_s {
    char    *path;
    char    *name;
    int     device;
    int     offset;
    int     size;
} file_t;

typedef struct dir_info {
    char    *path;
    int     device;
    int     offset;
} dir_t;

typedef struct dir_entry {
    char    *name;
    int     time;
    // Offset is # of 32-bit entries from the start of the cluster
    int     offset; 
    int     dir;
    void    *misc;
} dir_entry_t;

typedef struct fs_table_s {
    int (*init)(int);
    int (*createfile)(int, file_t*);
    int (*openfile)(int, file_t*, int cd);
    int (*deletefile)(file_t*);
    int (*read)(int,void*,int);
    int (*write)(int, const void*,int);
    dir_entry_t (*readdir)(dir_t*);
    int (*teardown)();
} fs_table_t;

extern file_t filetable[];
extern dir_t *dirtable[];
extern fs_table_t fs_table[];
extern mount_t *mount_table[];

#endif
