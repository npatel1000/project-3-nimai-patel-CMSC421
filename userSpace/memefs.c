#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#define BLOCK_SIZE 512
#define VOLUME_SIZE (256 * BLOCK_SIZE)

typedef struct { //demo file struct
    char name[256];
    size_t size;
    char *content;
} memefs_file;

static memefs_file dummy_file = {
    .name = "testfile",
    .size = 13,
    .content = "Hello, MEMEfs!"
};

static int memefs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi; //for unused parameter warning

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, "/testfile") == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = dummy_file.size;
    } else {
        return -ENOENT;
    }
    return 0;
}

static int memefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, dummy_file.name, NULL, 0, 0);

    return 0;
}

static int memefs_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    if (strcmp(path, "/testfile") != 0)
        return -ENOENT;

    return 0;
}

static int memefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;

    if (strcmp(path, "/testfile") != 0)
        return -ENOENT;

    if ((size_t)offset >= dummy_file.size)
        return 0;

    size_t to_read = (size > (dummy_file.size - (size_t)offset)) ? (dummy_file.size - (size_t)offset) : size;
    memcpy(buf, dummy_file.content + offset, to_read);

    return to_read;
}

static int memefs_access(const char *path, int mask) {
    (void)mask; //for unused parameter warning
    
    if (strcmp(path, "/") == 0 || strcmp(path, "/testfile") == 0) { //validate existance
        return 0; //access
    }
    return -ENOENT; //not found
}

static const struct fuse_operations memefs_oper = {
    .getattr = memefs_getattr,
    .readdir = memefs_readdir,
    .open = memefs_open,
    .read = memefs_read,
    .access = memefs_access,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <image-file> <mount-point>\n", argv[0]);
        return 1;
    }

    argv[2] = argv[1];  //adjust img
    argv[1] = argv[0];  //adjust fuse_main

    struct fuse_args args = FUSE_ARGS_INIT(argc - 1, argv + 1); //shift args
    return fuse_main(args.argc, args.argv, &memefs_oper, NULL);

}
