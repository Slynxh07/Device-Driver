#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean");
MODULE_DESCRIPTION("My first driver");

int reg_dev(void);
void dereg_dev(void);

//ssize_t device_file_read (struct file *, char *, size_t, loff_t *);

static struct file_operations simple_driver_fops = 
{
    .owner = THIS_MODULE
};

int reg_dev(void)
{
    return register_chrdev(42, "My driver", &simple_driver_fops);
}

void dereg_dev(void)
{
    unregister_chrdev(42, "My driver");
}

static int __init driver_init(void) {
    if (reg_dev() < 0) return -1;
    printk(KERN_INFO "Driver loaded\n");
    return 0;
}

static void __exit driver_exit(void) {
    dereg_dev();
    printk(KERN_INFO "Driver unloaded\n");
}

module_init(driver_init);
module_exit(driver_exit);