#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/usb.h>
#include <linux/hid.h>

#define KB_MINOR_BASE 192
#define KB_REPORT_SIZE 8

#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "keydriver"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean");
MODULE_DESCRIPTION("Keyboard Driver");

int reg_dev(void);
void dereg_dev(void);

static struct usb_driver keyboard_driver;
static struct usb_class_driver keyboard_class;
static struct file_operations simple_driver_fops;

typedef struct keyboard_dev
{
    struct usb_device *dev;
    struct urb *urb;
    unsigned char *buffer;

    unsigned char keycode;
    int key_available;

    wait_queue_head_t wait;
} keyboard_dev_t;

static int dev_open(struct inode *inode, struct file *file)
{
    struct usb_interface *interface = NULL;
    keyboard_dev_t *kbd = NULL;
    int subminor;

    subminor = iminor(inode);

    interface = usb_find_interface(&keyboard_driver, subminor);
    if (!interface) return -ENODEV;

    kbd = usb_get_intfdata(interface);
    if (!kbd) return -ENODEV;

    file->private_data = kbd;

    printk(KERN_INFO "Device Opened\n");
    return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    keyboard_dev_t *kbd = file->private_data;

    if(copy_to_user(buf, kbd->buffer, 8)) return -EFAULT;

    printk(KERN_INFO "Read called\n");
    return 8;
}

static void keyboard_irq(struct urb *urb)
{
    keyboard_dev_t *kbd = urb->context;
    unsigned char *data = urb->transfer_buffer;

    kbd->keycode = data[2];
    kbd->key_available = 1;

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

    kbd->dev = interface_to_usbdev(interface);

    kbd->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!kbd->urb)
    {
        kfree(kbd);
        return -ENOMEM;
    }

    kbd->buffer = kmalloc(KB_REPORT_SIZE, GFP_KERNEL);
    if (!kbd->buffer) 
    {
        usb_free_urb(kbd->urb);
        kfree(kbd);
        return -ENOMEM;
    }

    usb_set_intfdata(interface, kbd);

    usb_fill_int_urb(kbd->urb, kbd->dev, usb_rcvintpipe(kbd->dev, endpoint->bEndpointAddress), kbd->buffer, KB_REPORT_SIZE, keyboard_irq, kbd, endpoint->bInterval);

    int ret;

    ret = usb_submit_urb(kbd->urb, GFP_KERNEL);
    if (ret)
    {
        printk(KERN_ERR "Failed to submit URB\n");
        usb_kill_urb(kbd->urb);
    kfree(kbd->buffer);
    usb_free_urb(kbd->urb);
    kfree(kbd);
        return ret;
    }

    ret = usb_register_dev(interface, &keyboard_class);
    if (ret)
    {
        printk(KERN_ERR "Failed to register device\n");
        kfree(kbd->buffer);
        usb_free_urb(kbd->urb);
        kfree(kbd);
        return ret;
    }

    printk(KERN_INFO "Keyboard connected\n");
    return 0;
}

static void keyboard_disconnect(struct usb_interface *interface)
{
    keyboard_dev_t *kbd = usb_get_intfdata(interface);
    usb_deregister_dev(interface, &keyboard_class);
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

static struct usb_class_driver keyboard_class = 
{
    .name = "keydriver%d",
    .fops = &simple_driver_fops,
    .minor_base = KB_MINOR_BASE,
};

int reg_dev(void)
{
    return usb_register(&keyboard_driver);
}

void dereg_dev(void)
{
    usb_deregister(&keyboard_driver);
}

static int __init keyboard_driver_init(void) {
    if (reg_dev() < 0) return -1;
    printk(KERN_INFO "Driver loaded\n");
    return 0;
}

static void __exit keyboard_driver_exit(void) {
    dereg_dev();
    printk(KERN_INFO "Driver unloaded\n");
}

module_init(keyboard_driver_init);
module_exit(keyboard_driver_exit);