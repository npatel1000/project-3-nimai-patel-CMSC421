#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/time.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nimai Patel");
MODULE_DESCRIPTION("MEMEfs kernel module");
MODULE_VERSION("1.0");

#define MEMEFS_BLOCK_SIZE 512
#define MEMEFS_NUM_BLOCKS 256
#define MEMEFS_MAX_FILES 224

struct memefs_fat_entry {
    uint16_t next_block;
};

struct memefs_dir_entry {
    char name[64];
    uint16_t start_block;
    size_t size;
    struct timespec64 timestamp;
    bool is_used;
};

static struct memefs_fat_entry fat_table[MEMEFS_NUM_BLOCKS];
static struct memefs_dir_entry directory[MEMEFS_MAX_FILES];
static char *memefs_storage;
static DEFINE_MUTEX(memefs_mutex);

static int find_free_block(void) {
    int i;
    for (i = 1; i < MEMEFS_NUM_BLOCKS; i++) { //block 0 = metadata
        if (fat_table[i].next_block == 0xFFFF) {
            return i;
        }
    }
    return -ENOSPC; //no free blocks
}

static int find_free_directory_entry(void) {
    int i;
    for (i = 0; i < MEMEFS_MAX_FILES; i++) {
        if (!directory[i].is_used) {
            return i;
        }
    }
    return -ENOSPC;
}

static int memefs_create(struct user_namespace *ns, struct inode *dir,
                         struct dentry *dentry, umode_t mode, bool excl) {
    int dir_idx, block_idx;

    if (dentry->d_name.len > 63) {
        return -ENAMETOOLONG;
    }

    mutex_lock(&memefs_mutex);

    dir_idx = find_free_directory_entry();
    if (dir_idx < 0) {
        mutex_unlock(&memefs_mutex);
        return dir_idx;
    }

    block_idx = find_free_block();
    if (block_idx < 0) {
        mutex_unlock(&memefs_mutex);
        return block_idx;
    }

    directory[dir_idx].is_used = true;
    directory[dir_idx].start_block = block_idx;
    directory[dir_idx].size = 0;
    strncpy(directory[dir_idx].name, dentry->d_name.name, 64);
    ktime_get_real_ts64(&directory[dir_idx].timestamp);

    fat_table[block_idx].next_block = 0; //mark end of chain

    mutex_unlock(&memefs_mutex);
    return 0;
}

static int memefs_unlink(struct inode *dir, struct dentry *dentry) {
    int i, block, next;

    mutex_lock(&memefs_mutex);

    for (i = 0; i < MEMEFS_MAX_FILES; i++) {
        if (directory[i].is_used &&
            strncmp(directory[i].name, dentry->d_name.name, 64) == 0) {

            block = directory[i].start_block; //free FAT blocks
            while (block > 0 && block < MEMEFS_NUM_BLOCKS) {
                next = fat_table[block].next_block;
                fat_table[block].next_block = 0xFFFF; //mark free
                block = next;
            }

            memset(&directory[i], 0, sizeof(directory[i])); //free directory entry
            mutex_unlock(&memefs_mutex);
            return 0;
        }
    }

    mutex_unlock(&memefs_mutex);
    return -ENOENT;
}

static ssize_t memefs_write(struct file *file, const char __user *buf,
                            size_t len, loff_t *off) {
    size_t bytes_to_copy;
    int block, next_block;
    struct memefs_dir_entry *file_entry;

    if (!file->private_data) {
        return -EBADF;
    }

    mutex_lock(&memefs_mutex);

    file_entry = file->private_data;
    block = file_entry->start_block;
    while (*off >= MEMEFS_BLOCK_SIZE) {
        if (fat_table[block].next_block == 0) {
            next_block = find_free_block();
            if (next_block < 0) {
                mutex_unlock(&memefs_mutex);
                return next_block;
            }
            fat_table[block].next_block = next_block;
        }
        block = fat_table[block].next_block;
        *off -= MEMEFS_BLOCK_SIZE;
    }

    bytes_to_copy = min(len, (size_t)(MEMEFS_BLOCK_SIZE - *off));
    if (copy_from_user(memefs_storage + (block * MEMEFS_BLOCK_SIZE) + *off,
                       buf, bytes_to_copy)) {
        mutex_unlock(&memefs_mutex);
        return -EFAULT;
    }

    *off += bytes_to_copy;
    file_entry->size = max(file_entry->size, (size_t)*off);
    mutex_unlock(&memefs_mutex);
    return bytes_to_copy;
}

static ssize_t memefs_read(struct file *file, char __user *buf, size_t len,
                           loff_t *off) {
    size_t bytes_to_copy;
    int block;
    struct memefs_dir_entry *file_entry;

    if (!file->private_data) {
        return -EBADF;
    }

    mutex_lock(&memefs_mutex);

    file_entry = file->private_data;
    block = file_entry->start_block;
    while (*off >= MEMEFS_BLOCK_SIZE) {
        if (fat_table[block].next_block == 0) {
            mutex_unlock(&memefs_mutex);
            return 0; //end of file
        }
        block = fat_table[block].next_block;
        *off -= MEMEFS_BLOCK_SIZE;
    }

    bytes_to_copy = min(len, (size_t)(MEMEFS_BLOCK_SIZE - *off));
    if (copy_to_user(buf,
                     memefs_storage + (block * MEMEFS_BLOCK_SIZE) + *off,
                     bytes_to_copy)) {
        mutex_unlock(&memefs_mutex);
        return -EFAULT;
    }

    *off += bytes_to_copy;
    mutex_unlock(&memefs_mutex);
    return bytes_to_copy;
}

static const struct inode_operations memefs_inode_operations = {
    .create = memefs_create,
    .unlink = memefs_unlink,
};

static const struct file_operations memefs_file_operations = {
    .read = memefs_read,
    .write = memefs_write,
};

static int __init memefs_init(void) {
    int i;

    printk(KERN_INFO "MEMEfs: Initializing.\n");

    memefs_storage = kzalloc(MEMEFS_BLOCK_SIZE * MEMEFS_NUM_BLOCKS,
                             GFP_KERNEL);
    if (!memefs_storage) {
        return -ENOMEM;
    }

    for (i = 0; i < MEMEFS_NUM_BLOCKS; i++) {
        fat_table[i].next_block = 0xFFFF;
    }

    return 0;
}

static void __exit memefs_exit(void) {
    printk(KERN_INFO "MEMEfs: Exiting.\n");
    kfree(memefs_storage);
}

module_init(memefs_init);
module_exit(memefs_exit);
