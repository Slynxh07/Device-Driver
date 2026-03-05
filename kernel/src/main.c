#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/usb.h>
#include <linux/hid.h>

#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "keydriver"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean");
MODULE_DESCRIPTION("Keyboard Driver");

int reg_dev(void);
void dereg_dev(void);

static struct usb_device_id keyboard_table[] = 
{
    { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_KEYBOARD)},
    {}
};

MODULE_DEVICE_TABLE(usb, keyboard_table);

static int keyboard_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    printk(KERN_INFO "Keyboard connected\n");
    return 0;
}

static void keyboard_disconnect(struct usb_interface *interface)
{
    printk(KERN_INFO "Keyboard disconnected\n");
}

//USB keyboards send 8-bytes HID reports every keypress
//Example report
/*
Byte0: modifier
Byte1: reserved
Byte2-7: keycodes
*/

//Exaple 00 00 04 00 00 00 00 00
// 04 = KEY_A

//ssize_t device_file_read (struct file *, char *, size_t, loff_t *);

static struct usb_driver keyboard_driver =
{
    .name = "myKeyboard",
    .probe = keyboard_probe,
    .disconnect = keyboard_disconnect,
    .id_table = keyboard_table,
};

static struct file_operations simple_driver_fops = 
{
    .owner = THIS_MODULE
};

int reg_dev(void)
{
    if (register_chrdev(42, "My driver", &simple_driver_fops)) return -1;
    return usb_register(&keyboard_driver) < 0;
}

void dereg_dev(void)
{
    usb_deregister(&keyboard_driver);
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