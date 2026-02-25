#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean");
MODULE_DESCRIPTION("My first driver");

static int __init driver_init(void) {
    printk(KERN_INFO "Driver loaded\n");
    return 0;
}

static void __exit driver_exit(void) {
    printk(KERN_INFO "Driver unloaded\n");
}

module_init(driver_init);
module_exit(driver_exit);