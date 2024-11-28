#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define BLOCK_SIZE 512
#define VOLUME_SIZE (256 * BLOCK_SIZE)
#define MAX_FILES 224
#define FAT_ENTRIES 256

typedef struct { //example file entry
    char name[256];
    size_t size;
    uint16_t start_block;
    time_t timestamp;
} memefs_file;

typedef struct {
    char signature[16];
    uint8_t cleanly_unmounted;
    uint8_t reserved[3];
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
} __attribute__((packed)) memefs_superblock;

static uint16_t fat[FAT_ENTRIES]; //global variables
static memefs_file directory[MAX_FILES];
static FILE *image_file = NULL;

void load_fat() { //load FAT table
    fseek(image_file, 254 * BLOCK_SIZE, SEEK_SET);
    fread(fat, sizeof(uint16_t), FAT_ENTRIES, image_file);
}

void load_directory() { //load directory 
    fseek(image_file, 253 * BLOCK_SIZE, SEEK_SET);
    fread(directory, sizeof(memefs_file), MAX_FILES, image_file);
}

void sync_fat() { //sync FAT to img
    fseek(image_file, 254 * BLOCK_SIZE, SEEK_SET);
    fwrite(fat, sizeof(uint16_t), FAT_ENTRIES, image_file);
}

void sync_directory() { //sync directory to img
    fseek(image_file, 253 * BLOCK_SIZE, SEEK_SET);
    fwrite(directory, sizeof(memefs_file), MAX_FILES, image_file);
}

void sync_superblock() { //sync superblock (main and backup)
    memefs_superblock sb;
    memset(&sb, 0, sizeof(sb));

    strcpy(sb.signature, "?MEMEFS++CMSC421"); //superblock fields
    sb.cleanly_unmounted = 0xFF;
    sb.fs_version = htonl(1);

    fseek(image_file, 255 * BLOCK_SIZE, SEEK_SET); //write main superblock
    fwrite(&sb, sizeof(sb), 1, image_file);

    fseek(image_file, 0, SEEK_SET); //write backup superblock
    fwrite(&sb, sizeof(sb), 1, image_file);
}

static int memefs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) { //get file attributes (FUSE)
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) { //root directory
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    for (int i = 0; i < MAX_FILES; i++) { //search for file in directory
        if (strcmp(directory[i].name, path + 1) == 0) {
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = directory[i].size;
            return 0;
        }
    }
    return -ENOENT; //file not found
}

static int memefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) { //read directory content (FUSE)
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0) return -ENOENT;

    filler(buf, ".", NULL, 0, 0); //add current and parent directories
    filler(buf, "..", NULL, 0, 0);

    for (int i = 0; i < MAX_FILES; i++) { //add files from directory
        if (directory[i].name[0] != '\0') {
            filler(buf, directory[i].name, NULL, 0, 0);
        }
    }
    return 0;
}

static int memefs_open(const char *path, struct fuse_file_info *fi) { //open file (FUSE)
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(directory[i].name, path + 1) == 0) {
            return 0;
        }
    }
    return -ENOENT;
}

static int memefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) { //read data from file (FUSE)
    (void)fi;

    for (int i = 0; i < MAX_FILES; i++) { //search for file in directory
        if (strcmp(directory[i].name, path + 1) == 0) {
            if ((size_t)offset >= directory[i].size) return 0;

            size_t to_read = size;
            if (offset + size > directory[i].size) to_read = directory[i].size - offset;

            fseek(image_file, directory[i].start_block * BLOCK_SIZE + offset, SEEK_SET); //read file from img
            fread(buf, 1, to_read, image_file);

            return to_read;
        }
    }
    return -ENOENT; //file not found
}

static int memefs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) { //write data to file (FUSE)
    (void)fi;

    for (int i = 0; i < MAX_FILES; i++) { //search for file in directory
        if (strcmp(directory[i].name, path + 1) == 0) {
            fseek(image_file, directory[i].start_block * BLOCK_SIZE + offset, SEEK_SET);
            fwrite(buf, 1, size, image_file);

            directory[i].size += size; //update file size
            sync_directory();
            sync_superblock();

            return size;
        }
    }
    return -ENOENT; //file not found
}

static const struct fuse_operations memefs_oper = {
    .getattr = memefs_getattr,
    .readdir = memefs_readdir,
    .open = memefs_open,
    .read = memefs_read,
    .write = memefs_write,
};

int main(int argc, char *argv[]) { 
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <image-file> <mount-point>\n", argv[0]);
        return 1;
    }

    image_file = fopen(argv[1], "r+b"); //open file system 
    if (!image_file) {
        perror("fopen");
        return 1;
    }

    load_fat(); //load FAT and directory
    load_directory();

    struct fuse_args args = FUSE_ARGS_INIT(argc - 1, argv + 1); //FUSE main loop
    return fuse_main(args.argc, args.argv, &memefs_oper, NULL);
}

