#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "mkmemefs.h"

#define BLOCK_SIZE 512 //structure definitions
#define FAT_ENTRY_COUNT 256
#define DIRECTORY_ENTRY_SIZE 32
#define DIRECTORY_BLOCK_COUNT 14

typedef struct {
    uint16_t file_type_permissions;
    uint16_t first_block; //first block of file
    char filename[11];
    uint8_t unused;
    uint8_t last_modified[8]; //timestamp (BCD)
    uint32_t file_size; //in bytes
    uint16_t owner_id;
    uint16_t group_id;
} __attribute__((packed)) directory_entry_t;

static char *filesystem_image = NULL; //global variables
static FILE *fs_image_fp = NULL;
static uint16_t fat[FAT_ENTRY_COUNT];
static directory_entry_t directory[DIRECTORY_BLOCK_COUNT * BLOCK_SIZE / DIRECTORY_ENTRY_SIZE];

static int memefs_init(const char *image_path); //function prototypes
static void memefs_cleanup();
static int memefs_getattr(const char *path, struct stat *stbuf);
static int memefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int memefs_open(const char *path, struct fuse_file_info *fi);
static int memefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

static struct fuse_operations memefs_oper = { //FUSE operations structure
    .getattr = memefs_getattr,
    .readdir = memefs_readdir,
    .open = memefs_open,
    .read = memefs_read,
};

static int memefs_init(const char *image_path) { //helper functions
    fs_image_fp = fopen(image_path, "rb"); //open filesystem image file
    if (!fs_image_fp) {
        perror("Failed to open filesystem image");
        return -1;
    }

    fseek(fs_image_fp, 254 * BLOCK_SIZE, SEEK_SET); //read FAT
    fread(fat, sizeof(uint16_t), FAT_ENTRY_COUNT, fs_image_fp);

    fseek(fs_image_fp, 253 * BLOCK_SIZE, SEEK_SET); //read directory
    fread(directory, sizeof(directory_entry_t), DIRECTORY_BLOCK_COUNT * BLOCK_SIZE / DIRECTORY_ENTRY_SIZE, fs_image_fp);

    return 0;
}

static void memefs_cleanup() {
    if (fs_image_fp) {
        fclose(fs_image_fp);
        fs_image_fp = NULL;
    }
}

static int find_directory_entry(const char *path, directory_entry_t **entry) {
    if (strcmp(path, "/") == 0) return -1; //root directory

    for (size_t i = 0; i < DIRECTORY_BLOCK_COUNT * BLOCK_SIZE / DIRECTORY_ENTRY_SIZE; ++i) {
        if (directory[i].file_type_permissions != 0x0000 &&
            strncmp(directory[i].filename, path + 1, 11) == 0) {
            *entry = &directory[i];
            return 0;
        }
    }
    return -ENOENT;
}

static int memefs_getattr(const char *path, struct stat *stbuf) { //FUSE functions
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    directory_entry_t *entry;
    if (find_directory_entry(path, &entry) == 0) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = ntohl(entry->file_size);
        return 0;
    }

    return -ENOENT;
}

static int memefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    if (strcmp(path, "/") != 0) return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (size_t i = 0; i < DIRECTORY_BLOCK_COUNT * BLOCK_SIZE / DIRECTORY_ENTRY_SIZE; ++i) {
        if (directory[i].file_type_permissions != 0x0000) {
            char name[12] = {0};
            memcpy(name, directory[i].filename, 11);
            filler(buf, name, NULL, 0);
        }
    }

    return 0;
}

static int memefs_open(const char *path, struct fuse_file_info *fi) {
    directory_entry_t *entry;
    if (find_directory_entry(path, &entry) == 0) {
        return 0;
    }
    return -ENOENT;
}

static int memefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    directory_entry_t *entry;
    if (find_directory_entry(path, &entry) != 0) {
        return -ENOENT;
    }

    size_t file_size = ntohl(entry->file_size);
    if ((size_t)offset >= file_size) return 0;

    size = (offset + size > file_size) ? file_size - offset : size;

    uint16_t block = ntohs(entry->first_block);
    size_t bytes_read = 0;

    while (block != 0xFFFF && size > 0) {
        fseek(fs_image_fp, block * BLOCK_SIZE, SEEK_SET);

        size_t to_read = (size > BLOCK_SIZE - offset) ? BLOCK_SIZE - offset : size;
        fread(buf + bytes_read, 1, to_read, fs_image_fp);

        size -= to_read;
        bytes_read += to_read;
        offset = 0;

        block = ntohs(fat[block]);
    }

    return bytes_read;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image file> <mount point>\n", argv[0]);
        return 1;
    }

    filesystem_image = argv[1];
    if (memefs_init(filesystem_image) < 0) {
        return 1;
    }

    int ret = fuse_main(argc - 1, argv + 1, &memefs_oper, NULL);
    memefs_cleanup();
    return ret;
}

