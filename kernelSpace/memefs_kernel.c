#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nimai Patel");
MODULE_DESCRIPTION("MEMEfs kernel module");
MODULE_VERSION("1.0");

static DEFINE_MUTEX(memefs_mutex);

#define BUFFER_SIZE 512
static char device_buffer[BUFFER_SIZE];
static size_t data_size = 0;

static int memefs_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "MEMEfs: File opened.\n");
    return 0;
}

static int memefs_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "MEMEfs: File closed.\n");
    return 0;
}

static ssize_t memefs_write(struct file *file, const char __user *buf, size_t len, loff_t *off) {
    size_t bytes_not_copied;

    printk(KERN_INFO "MEMEfs: Write called with length = %zu.\n", len);

    if (len > BUFFER_SIZE) {
        printk(KERN_ERR "MEMEfs: Write size exceeds buffer capacity.\n");
        return -EINVAL;
    }

    mutex_lock(&memefs_mutex);

    bytes_not_copied = copy_from_user(device_buffer, buf, len);
    if (bytes_not_copied) {
        printk(KERN_ERR "MEMEfs: Failed to copy data from user space. Bytes not copied = %zu.\n", bytes_not_copied);
        mutex_unlock(&memefs_mutex);
        return -EFAULT;
    }

    device_buffer[len] = '\0'; //null terminate (debugging)
    data_size = len;
    printk(KERN_INFO "MEMEfs: Writing data - %s\n", device_buffer);

    mutex_unlock(&memefs_mutex);
    return len;
}

static ssize_t memefs_read(struct file *file, char __user *buf, size_t len, loff_t *off) {
    size_t bytes_to_copy;

    printk(KERN_INFO "MEMEfs: Read called with length = %zu.\n", len);

    mutex_lock(&memefs_mutex);

    if (*off >= data_size) { //check if offset > data size
        mutex_unlock(&memefs_mutex);
        return 0;
    }

    bytes_to_copy = min(len, data_size - (size_t)(*off)); //check both operands in min are same type

    if (copy_to_user(buf, device_buffer + *off, bytes_to_copy)) {
        printk(KERN_ERR "MEMEfs: Failed to copy data to user space.\n");
        mutex_unlock(&memefs_mutex);
        return -EFAULT;
    }

    *off += bytes_to_copy;
    printk(KERN_INFO "MEMEfs: Read returned %zu bytes.\n", bytes_to_copy);

    mutex_unlock(&memefs_mutex);
    return bytes_to_copy;
}

static struct file_operations memefs_fops = {
    .owner = THIS_MODULE,
    .open = memefs_open,
    .release = memefs_release,
    .write = memefs_write,
    .read = memefs_read,
};

static int major_num;
static char *memefs_name = "memefs";

static int __init memefs_init(void) {
    printk(KERN_INFO "MEMEfs: Initializing the MEMEfs kernel module.\n");

    major_num = register_chrdev(0, memefs_name, &memefs_fops);
    if (major_num < 0) {
        printk(KERN_ERR "MEMEfs: Failed to register a major number.\n");
        return major_num;
    }

    printk(KERN_INFO "MEMEfs: Registered with major number %d.\n", major_num);
    return 0;
}

static void __exit memefs_exit(void) {
    printk(KERN_INFO "MEMEfs: Cleaning up the MEMEfs kernel module.\n");
    unregister_chrdev(major_num, memefs_name);
}

module_init(memefs_init);
module_exit(memefs_exit);

