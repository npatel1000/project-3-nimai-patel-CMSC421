#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#define BLOCK_SIZE 512
#define VOLUME_SIZE (256 * BLOCK_SIZE)

typedef struct { //example
    char name[256];
    size_t size;
    char *content;
} memefs_file;

static memefs_file dummy_file = {
    .name = "testfile",
    .size = 15,
    .content = "Hello, MEMEfs!\n\n"
};

static int memefs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi; //for unused parameter warning

    printf("getattr called for path: %s\n", path);

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) { //directory and file attributes
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, "/testfile") == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = dummy_file.size;
    } else {
        return -ENOENT; //file not found
    }

    return 0;
}

static int memefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    printf("readdir called for path: %s\n", path);

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0); //current directory
    filler(buf, "..", NULL, 0, 0); //parent directory
    filler(buf, dummy_file.name, NULL, 0, 0); //add dummy file
    
    printf("Content being read: %.*s\n", (int)dummy_file.size, dummy_file.content);

    return 0;
}

static int memefs_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    printf("open called for path: %s\n", path);

    if (strcmp(path + 1, dummy_file.name) != 0)
        return -ENOENT;

    return 0;
}

static int memefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;

    printf("read called for path: %s, size: %lu, offset: %ld\n", path, size, offset);

    if (strcmp(path + 1, dummy_file.name) != 0)
        return -ENOENT;

    if ((size_t)offset >= dummy_file.size)
        return 0;

    size_t to_read = (size > (dummy_file.size - (size_t)offset)) ? (dummy_file.size - (size_t)offset) : size;
    memcpy(buf, dummy_file.content + offset, to_read);

    return to_read;
}

static int memefs_access(const char *path, int mask) {
    (void)mask;
    
    printf("access called for path: %s\n", path);

    if (strcmp(path, "/") == 0 || strcmp(path, "/testfile") == 0) { //validate existence
        return 0;
    }
    return -ENOENT;
}

static int memefs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)fi;
    printf("create called for path: %s, mode: %o\n", path, mode);

    if (strlen(path + 1) > 255) { // Validate filename length
        return -ENAMETOOLONG;
    }

    for (int i = 0; i < 224; ++i) { //find unused directory (224 entries)
        if (directory[i].file_type == 0x0000) {
            memset(&directory[i], 0, sizeof(directory_entry_t));
            strncpy(directory[i].name, path + 1, 255);
            directory[i].file_type = 0x8000;
            directory[i].size = 0;
            directory[i].start_block = 0;
            directory[i].timestamp = get_current_timestamp();
            update_fat();
            sync_superblock();
            return 0;
        }
    }

    return -ENOSPC; //no space left
}

static int memefs_unlink(const char *path) {
    printf("unlink called for path: %s\n", path);

    for (int i = 0; i < 224; ++i) {
        if (strcmp(directory[i].name, path + 1) == 0) {
            int block = directory[i].start_block; //free blocks
            while (block != 0xFFFF) {
                int next = fat[block];
                fat[block] = 0x0000; //block = free
                block = next;
            }

            memset(&directory[i], 0, sizeof(directory_entry_t)); //clear directory entry
            update_fat();
            sync_superblock();
            return 0;
        }
    }

    return -ENOENT; //file not found
}

static int memefs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    printf("write called for path: %s, size: %lu, offset: %ld\n", path, size, offset);

    for (int i = 0; i < 224; ++i) {
        if (strcmp(directory[i].name, path + 1) == 0) {
            size_t bytes_written = 0;
            int block = directory[i].start_block;

            while (offset > BLOCK_SIZE) { //navigate to correct block
                if (fat[block] == 0xFFFF) {
                    block = allocate_new_block(block);
                } else {
                    block = fat[block];
                }
                offset -= BLOCK_SIZE;
            }

            while (bytes_written < size) { //write data to blocks
                size_t to_write = (size - bytes_written) < (BLOCK_SIZE - offset) ? (size - bytes_written) : (BLOCK_SIZE - offset);
                memcpy(&disk[block][offset], buf + bytes_written, to_write);
                bytes_written += to_write;
                offset = 0;

                if (bytes_written < size) {
                    block = allocate_new_block(block);
                }
            }

            directory[i].size += bytes_written;
            directory[i].timestamp = get_current_timestamp();
            sync_superblock();
            return bytes_written;
        }
    }

    return -ENOENT; //file not found
}

static int memefs_truncate(const char *path, off_t size) {
    printf("truncate called for path: %s, size: %ld\n", path, size);

    for (int i = 0; i < 224; ++i) {
        if (strcmp(directory[i].name, path + 1) == 0) {
            if ((size_t)size < directory[i].size) {
                int block = directory[i].start_block; //free extra blocks
                while (size > BLOCK_SIZE) {
                    block = fat[block];
                    size -= BLOCK_SIZE;
                }

                int next_block = fat[block];
                fat[block] = 0xFFFF;

                while (next_block != 0xFFFF) {
                    int tmp = fat[next_block];
                    fat[next_block] = 0x0000;
                    next_block = tmp;
                }
            } else if ((size_t)size > directory[i].size) {
                int block = directory[i].start_block; //allocate new blocks
                while (fat[block] != 0xFFFF) {
                    block = fat[block];
                }

                while (directory[i].size < (size_t)size) {
                    block = allocate_new_block(block);
                    directory[i].size += BLOCK_SIZE;
                }
            }

            directory[i].timestamp = get_current_timestamp();
            sync_superblock();
            return 0;
        }
    }

    return -ENOENT; //file not found
}

static const struct fuse_operations memefs_oper = {
    .getattr = memefs_getattr,
    .readdir = memefs_readdir,
    .open = memefs_open,
    .read = memefs_read,
    .access = memefs_access,
};

int allocate_new_block(int current_block) {
    for (int i = 1; i < 201; ++i) {
        if (fat[i] == 0x0000) {
            fat[i] = 0xFFFF;
            if (current_block != -1) {
                fat[current_block] = i;
            }
            return i;
        }
    }
    return -ENOSPC; //no space left
}

uint8_t get_current_timestamp() {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    return (uint8_t[]) {
        pbcd((tm->tm_year + 1900) / 100),
        pbcd((tm->tm_year + 1900) % 100),
        pbcd(tm->tm_mon + 1),
        pbcd(tm->tm_mday),
        pbcd(tm->tm_hour),
        pbcd(tm->tm_min),
        pbcd(tm->tm_sec),
        0
    };
}

int main(int argc, char *argv[]) {
    printf("argc: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d]: %s\n", i, argv[i]);
    }

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <image-file> <mount-point> [options]\n", argv[0]);
        return 1;
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc - 1, argv + 1);

    return fuse_main(args.argc, args.argv, &memefs_oper, NULL);
}


