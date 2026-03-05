#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/usb.h>
#include <linux/hid.h>

#define KB_MAJOR 42

#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "keydriver"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean");
MODULE_DESCRIPTION("Keyboard Driver");

int reg_dev(void);
void dereg_dev(void);

typedef struct keyboard_dev
{
    struct usb_device *dev;
    struct urb *urb;
    unsigned char *buffer;
} keyboard_dev_t;

static int dev_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Device Opened\n");
    return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Device closed\n");
    return 0;
}

ssize_t dev_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    printk(KERN_INFO "Read called\n");
    return 0;
}

static void keyboard_irq(struct urb *urb)
{
    keyboard_dev_t *kbd = urb->context;
    unsigned char *data = urb->transfer_buffer;

    printk(KERN_INFO "Keycode %d\n", data[2]);

    usb_submit_urb(urb, GFP_ATOMIC);
}

static struct usb_device_id keyboard_table[] = 
{
    { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_KEYBOARD)},
    {}
};

MODULE_DEVICE_TABLE(usb, keyboard_table);

static int keyboard_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    keyboard_dev_t *kbd = kzalloc(sizeof(keyboard_dev_t), GFP_KERNEL);
    if (!kbd) return -ENOMEM;
    struct usb_host_interface *iface_desc = interface->cur_altsetting;
    struct usb_endpoint_descriptor *endpoint = NULL;
    for (int i = 0; i < iface_desc->desc.bNumEndpoints; i++)
    {
        struct usb_endpoint_descriptor *temp_endpoint;
        temp_endpoint = &iface_desc->endpoint[i].desc;

        if (usb_endpoint_is_int_in(temp_endpoint))
        {
            endpoint = temp_endpoint;
            break;
        }
    }

    if (!endpoint)
    {
        kfree(kbd);
        return -ENODEV;
    }
    //int endpoint = 1;
    //int interval = 10;
    int buff_size = 8;

    kbd->dev = interface_to_usbdev(interface);

    kbd->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!kbd->urb)
    {
        kfree(kbd);
        return -ENOMEM;
    }

    kbd->buffer = kmalloc(buff_size, GFP_KERNEL);
    if (!kbd->buffer) 
    {
        usb_free_urb(kbd->urb);
        kfree(kbd);
        return -ENOMEM;
    }

    usb_set_intfdata(interface, kbd);

    usb_fill_int_urb(kbd->urb, kbd->dev, usb_rcvintpipe(kbd->dev, endpoint->bEndpointAddress), kbd->buffer, buff_size, keyboard_irq, kbd, endpoint->bInterval);

    int ret;

    ret = usb_submit_urb(kbd->urb, GFP_KERNEL);
    if (ret)
    {
        printk(KERN_ERR "Failed to submit URB\n");
        return ret;
    }

    printk(KERN_INFO "Keyboard connected\n");
    return 0;
}

static void keyboard_disconnect(struct usb_interface *interface)
{
    keyboard_dev_t *kbd = usb_get_intfdata(interface);
    usb_kill_urb(kbd->urb);
    usb_free_urb(kbd->urb);
    kfree(kbd->buffer);
    kfree(kbd);
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


static struct usb_driver keyboard_driver =
{
    .name = "myKeyboard",
    .probe = keyboard_probe,
    .disconnect = keyboard_disconnect,
    .id_table = keyboard_table,
};

static struct file_operations simple_driver_fops = 
{
    .owner = THIS_MODULE,
    .open = dev_open,
    .release = dev_release,
    .read = dev_read,
};

int reg_dev(void)
{
    int ret;

    ret = register_chrdev(KB_MAJOR, KBUILD_MODNAME, &simple_driver_fops);
    if (ret < 0) return ret;

    ret = usb_register(&keyboard_driver);
    if (ret < 0) 
    {
        unregister_chrdev(KB_MAJOR, KBUILD_MODNAME);
        return ret;
    }

    return 0;
}

void dereg_dev(void)
{
    usb_deregister(&keyboard_driver);
    unregister_chrdev(KB_MAJOR, KBUILD_MODNAME);
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