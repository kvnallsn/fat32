#include "fat_common.h"

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
 * Generates an 8.3 representation of a filename
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
    
    int pos = ((order & 0x9F) - 1) * 13;
    int s_pos = pos;
    int len = strlen(input);
    
    long_ent.charset1[0] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset1[1] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset1[2] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset1[3] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset1[4] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset2[0] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset2[1] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset2[2] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset2[3] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset2[4] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset2[5] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset3[0] = (pos < len) ? input[pos++] : 0xFFFF;
    long_ent.charset3[1] = (pos < len) ? input[pos++] : 0xFFFF;
    
    int end_pos = len - s_pos;
    if ((order & 0x40) == 0x40) {
        if (end_pos < 5) {
            long_ent.charset1[end_pos] = 0x0000;
        } else if (end_pos < 12) {
            long_ent.charset2[end_pos - 5] = 0x0000;
        } else if (end_pos < 13) {
            long_ent.charset3[end_pos - 12] = 0x0000;
        }
    }
    
    return long_ent;
}

int find_free_cluster(char *dev, fat_t *fat, int cluster) {
    int device = open(dev, O_RDONLY);
    while (read_fat_table(device, fat, cluster) > 0x0) ++cluster;
    close(device);
    return cluster;
}