#ifndef MKMEMEFS_H
#define MKMEMEFS_H

#include <stdint.h>

typedef struct memefs_superblock {
    char signature[16];
    uint8_t cleanly_unmounted;
    uint8_t reserved1[3];
    uint32_t fs_version;
    uint8_t fs_ctime[8];
    uint16_t main_fat;
    uint16_t main_fat_size;
    uint16_t backup_fat;
    uint16_t backup_fat_size;
    uint16_t directory_start;
    uint16_t directory_size;
    uint16_t num_user_blocks;
    uint16_t first_user_block;
    char volume_label[16];
    uint8_t unused[448];
} __attribute__((packed)) memefs_superblock_t;

#define BLOCK_SIZE 512 //other constants and macros
#define FAT_FREE 0x0000
#define FAT_EOC 0xFFFF

#endif

