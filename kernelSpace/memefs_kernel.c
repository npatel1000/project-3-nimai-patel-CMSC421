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

static int memefs_open(struct inode *inode, struct file *file) { //file operations
    printk(KERN_INFO "MEMEfs: File opened.\n");
    return 0;
}

static int memefs_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "MEMEfs: File closed.\n");
    return 0;
}

static ssize_t memefs_write(struct file *file, const char __user *buf, size_t len, loff_t *off) {
    char *kernel_buf;
    size_t bytes_not_copied;

    kernel_buf = kmalloc(len, GFP_KERNEL);
    if (!kernel_buf)
        return -ENOMEM;

    bytes_not_copied = copy_from_user(kernel_buf, buf, len);
    if (bytes_not_copied) {
        kfree(kernel_buf);
        return -EFAULT;
    }

    mutex_lock(&memefs_mutex);
    printk(KERN_INFO "MEMEfs: Writing data - %s\n", kernel_buf);
    mutex_unlock(&memefs_mutex);

    kfree(kernel_buf);
    return len;
}

static struct file_operations memefs_fops = {
    .owner = THIS_MODULE,
    .open = memefs_open,
    .release = memefs_release,
    .write = memefs_write,
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
    unregister_chrdev(major_num, memefs_name);
    printk(KERN_INFO "MEMEfs: Cleaning up the MEMEfs kernel module.\n");
}

module_init(memefs_init);
module_exit(memefs_exit);
