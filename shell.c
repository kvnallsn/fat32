/* Generic C Headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"

#define KNRM    "\x1B[0m"
#define KRED    "\x1B[31m"
#define KGRN    "\x1B[32m"
#define KYEL    "\x1B[33m"
#define KBLU    "\x1B[34m"
#define KMAG    "\x1B[35m"
#define KCYN    "\x1B[36m"
#define KWHT    "\x1B[37m"

typedef struct arg_info_s {
    int argc;
    char **argv;
} arg_info_t;

static char *current_dir = "/";

arg_info_t tokenize(char *input) {
    int num_cmds = 2;
    int i;
    char *token;
    arg_info_t arg_info;
    
    for (i = 0; i < strlen(input); i++) {
        if (input[i] == ' ') ++num_cmds;
    }  
    
    arg_info.argv = calloc(num_cmds+1, sizeof(char*));
    for (arg_info.argc = 0; (token = strtok(NULL, " ")) != NULL; arg_info.argc++) {
        arg_info.argv[arg_info.argc] = token;
    }
    
    arg_info.argv[num_cmds] = (char*)'\0';
    return arg_info;
}

char * prepend_path(char *path) {
    if (path == NULL) { return NULL; }
    
    if (path[0] != '/') {
        char *full_path = calloc(strlen(path) + strlen(current_dir) + 1, sizeof(char));
        strcpy(full_path, current_dir);
        strcat(full_path, path);
        return full_path;
    }
    
    return path;
}

void mount(arg_info_t args) {

    int i;
    char *device_name;
    char *path;    
    int type;
    if (args.argc == 0) {        
        for (i = 0; i < MOUNT_LIMIT; i++) {
            if (mount_table[i] != NULL) {
                printf("%s on %s type %s\n", mount_table[i]->device_name, mount_table[i]->path, "FAT32");
            }
        }
        return;
    } else if (args.argc < 3) {
        printf("usage: [f|s] mount device mount-point\n");
        return;
    }
    
    type = strcmp(args.argv[0], "f") == 0 ? FAT32 : SKINNY28;
    device_name = args.argv[args.argc - 2];
    path = args.argv[args.argc - 1];
    
    mount_fs(device_name, path, type);
}

void umount(arg_info_t args) {
    if (args.argc != 1) {
        printf("usage: umount mount-point\n");
        return;
    }
    unmount_fs(args.argv[0]);
}

void ls(arg_info_t args) {
    int dir;
    dir_entry_t file;
    
    if (args.argc != 0) {
        printf("usage: ls\n");
        return;
    }
    dir = opendir("/"); 
    
    while ((file = readdir(dir)).name != NULL) {
        if (file.dir == 1) printf(KRED "%s\n" KNRM, file.name);
        else printf("%s\n", file.name);
    }
    closedir(dir);
}

void touch(arg_info_t args) {
    int fp;
    if (args.argc != 1) {
        printf("usage: touch filename\n");
        return;
    }
    
    fp = filecreate(prepend_path(args.argv[0]), 0);
    fileclose(fp);
}

void mkdir(arg_info_t args) {
    int fp;
    if (args.argc != 1) {
        printf("usage: mkdir directory\n");
        return;
    }
    
    fp = filecreate(prepend_path(args.argv[0]), 1);
    fileclose(fp);
}


void cat(arg_info_t args) {
    int fp;
    int nr;
    char buffer[512];
    if (args.argc != 1) {
        printf("usage: cat filename\n");
        return;
    }
    
    fp = fileopen(prepend_path(args.argv[0]), BEGIN);
    if (fp == -1) { printf("cat: %s: No Such File or Directory\n", args.argv[0]); return; }
    nr = 0;
    while ((nr = fileread(fp, buffer, 512)) > 0) {
        printf("%s", buffer);
    }
    fileclose(fp);
}

void cd(arg_info_t args) {
    if (args.argc != 1) {
        printf("usage: cd directory\n");
        return;
    } 
    changedir(args.argv[0]);
}

void rm(arg_info_t args) {
    if (args.argc != 1) {
        printf("usage: rm file\n");
        return;
    } 
    
    deletefile(args.argv[0]);
}

void echo(arg_info_t args) {
    int fp;
    if (args.argc != 2) {
        printf("usage: echo word file\n");
        return;
    }     
    
    fp = fileopen(prepend_path(args.argv[1]), BEGIN);
    if (fp == -1) { printf("Error\n"); }
    filewrite(fp, args.argv[0], strlen(args.argv[0]));
    fileclose(fp);    
}

void echoa(arg_info_t args) {
    int fp;
    if (args.argc != 2) {
        printf("usage: echo word file\n");
        return;
    }     
    
    fp = fileopen(prepend_path(args.argv[1]), APPEND);
    if (fp == -1) { printf("Error\n"); }
    filewrite(fp, args.argv[0], strlen(args.argv[0]));
    fileclose(fp);    
}

void revprint(arg_info_t args) {
    int fp;
    int rclus;
    int i;
    
    if (args.argc != 1) {
        printf("usage: revs file\n");
        return;
    }     
    
    fp = fileopen(prepend_path(args.argv[0]), APPEND);
    if (fp == -1) { printf("Error\n"); }
    
    
    rclus = -1;
    for (i = 0; ((rclus = filegetrevision(fp, i)) != -1); i++) {
        (i == 0) ? printf("Current: ") : printf("%6d: ", i);
        printf("[%d]\n", rclus);
    }
    
    fileclose(fp);    
}

void revert(arg_info_t args) {
    int fp;
    if (args.argc != 2) {
        printf("usage: revert file revision\n");
        return;
    }     
    
    fp = fileopen(prepend_path(args.argv[0]), APPEND);
    if (fp == -1) { printf("Error\n"); }
    filerevert(fp, atoi(args.argv[1]));
    fileclose(fp);
}

void printrev(arg_info_t args) {
    int fp;
    int nr;
    char buffer[512];
    
    if (args.argc != 2) {
        printf("usage: printrev file revision\n");
        return;
    }     
    
    fp = fileopen(prepend_path(args.argv[0]), BEGIN);
    if (fp == -1) { printf("printrev: %s: No Such File or Directory\n", args.argv[0]); return; }
    nr = 0;
    while ((nr = fileprintrev(fp, buffer, 512, atoi(args.argv[1]))) > 0) {
        printf("%s", buffer);
    }
    fileclose(fp);
}

int main(int argc, char **argv) {

    char *input;
    char *cmd;

    /* Temporarily auto mount hello */

    while (1) {
        printf("> ");
        input = calloc(80, sizeof(char));
        input = fgets(input, 80, stdin);
        input[strlen(input)-1] = '\0';  /* Strip New Line */
        
        cmd = strtok(input, " ");
        if (cmd != NULL) {
            char *exec = cmd + strlen(cmd) + 1;
            if (strcmp(cmd, "exit") == 0) {
                free(input);
                break;
            } else if(strcmp(cmd, "mount") == 0) {
                mount(tokenize(exec));
            } else if(strcmp(cmd, "umount") == 0) {
                umount(tokenize(exec));
            } else if (strcmp(cmd, "ls") == 0) {
                ls(tokenize(exec));
            } else if (strcmp(cmd, "touch") == 0) {
                touch(tokenize(exec));
            } else if (strcmp(cmd, "mkdir") == 0) {
                mkdir(tokenize(exec));
            } else if (strcmp(cmd, "cat") == 0) {
                cat(tokenize(exec));
            } else if (strcmp(cmd, "cd") == 0) {
                cd(tokenize(exec));
            } else if (strcmp(cmd, "pwd") == 0) {
            
            } else if (strcmp(cmd, "rm") == 0) {
                rm(tokenize(exec));
            } else if (strcmp(cmd, "echo") == 0) {
                echo(tokenize(exec));
            } else if (strcmp(cmd, "echoa") == 0) {
                echoa(tokenize(exec));
            } else if (strcmp(cmd, "revs") == 0) {
                revprint(tokenize(exec));
            } else if (strcmp(cmd, "revert") == 0) {
                revert(tokenize(exec));
            } else if (strcmp(cmd, "printrev") == 0) {
                printrev(tokenize(exec));
            } else {
                printf("%s: Command Not Found\n", input);
            }
        }
        
        free(input);
    }
    
    return EXIT_SUCCESS;
}
