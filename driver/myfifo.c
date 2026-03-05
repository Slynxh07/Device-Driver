#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/ioctl.h>

#define DEVICE_NAME "myfifo"
#define CLASS_NAME  "myfifo_class"
#define FIFO_SIZE   4096
#define PROC_NAME   "myfifo_stats"

struct myfifo_stats {
    unsigned long bytes_written;
    unsigned long bytes_read;
    unsigned long blocked_writers;
    unsigned long blocked_readers;
};

#define MYFIFO_IOC_MAGIC     'k'
#define MYFIFO_IOC_GET_STATS _IOR(MYFIFO_IOC_MAGIC, 1, struct myfifo_stats)

/* static globals are zero-initialized */
static dev_t dev_num;
static struct cdev my_cdev;
static struct class *my_class;

static char fifo_buffer[FIFO_SIZE];
static size_t fifo_head;  /* next write */
static size_t fifo_tail;  /* next read */
static size_t fifo_count; /* bytes stored */

static struct mutex fifo_mutex;
static wait_queue_head_t read_queue;
static wait_queue_head_t write_queue;

static struct myfifo_stats stats;
static struct proc_dir_entry *proc_entry;

/* --- FIFO helpers --- */
static size_t fifo_space_available(void)
{
    return FIFO_SIZE - fifo_count;
}

static size_t fifo_data_available(void)
{
    return fifo_count;
}

static bool fifo_has_data(void)
{
    return fifo_data_available() > 0;
}

static bool fifo_has_space(void)
{
    return fifo_space_available() > 0;
}

/* --- From FIFO to user --- */
static ssize_t fifo_read_bytes(char __user *buf, size_t len)
{
    size_t avail = fifo_data_available();
    size_t first, second;

    if (len > avail)
        len = avail;

    if (len == 0)
        return 0;

    if (fifo_tail + len <= FIFO_SIZE) {
        if (copy_to_user(buf, &fifo_buffer[fifo_tail], len))
            return -EFAULT;
    } else {
        first = FIFO_SIZE - fifo_tail;
        second = len - first;

        if (copy_to_user(buf, &fifo_buffer[fifo_tail], first))
            return -EFAULT;
        if (copy_to_user(buf + first, &fifo_buffer[0], second))
            return -EFAULT;
    }

    fifo_tail = (fifo_tail + len) % FIFO_SIZE;
    fifo_count -= len;
    stats.bytes_read += len;

    return len;
}

/* --- From user to FIFO --- */
static ssize_t fifo_write_bytes(const char __user *buf, size_t len)
{
    size_t space = fifo_space_available();
    size_t first, second;

    if (len > space)
        len = space;

    if (len == 0)
        return 0;

    if (fifo_head + len <= FIFO_SIZE) {
        if (copy_from_user(&fifo_buffer[fifo_head], buf, len))
            return -EFAULT;
    } else {
        first = FIFO_SIZE - fifo_head;
        second = len - first;

        if (copy_from_user(&fifo_buffer[fifo_head], buf, first))
            return -EFAULT;
        if (copy_from_user(&fifo_buffer[0], buf + first, second))
            return -EFAULT;
    }

    fifo_head = (fifo_head + len) % FIFO_SIZE;
    fifo_count += len;
    stats.bytes_written += len;

    return len;
}

/* --- Blocking helper --- */
static int wait_for_condition(wait_queue_head_t *q,
                              bool (*cond)(void),
                              unsigned long *blocked_counter,
                              struct file *file)
{
    int ret;

    while (!cond()) {
        mutex_unlock(&fifo_mutex);

        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        if (blocked_counter)
            (*blocked_counter)++;

        ret = wait_event_interruptible(*q, cond());
        if (ret)
            return -ERESTARTSYS;

        if (mutex_lock_interruptible(&fifo_mutex))
            return -ERESTARTSYS;
    }

    return 0;
}

/* --- File operations --- */

static int my_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t my_read(struct file *file, char __user *buf,
                       size_t len, loff_t *ppos)
{
    ssize_t ret;

    if (len == 0)
        return 0;

    if (mutex_lock_interruptible(&fifo_mutex))
        return -ERESTARTSYS;

    ret = wait_for_condition(&read_queue, fifo_has_data,
                             &stats.blocked_readers, file);
    if (ret) {
        mutex_unlock(&fifo_mutex);
        return ret;
    }

    ret = fifo_read_bytes(buf, len);

    mutex_unlock(&fifo_mutex);

    if (ret > 0)
        wake_up_interruptible(&write_queue);

    return ret;
}

static ssize_t my_write(struct file *file, const char __user *buf,
                        size_t len, loff_t *ppos)
{
    ssize_t ret;

    if (len == 0)
        return 0;

    if (mutex_lock_interruptible(&fifo_mutex))
        return -ERESTARTSYS;

    ret = wait_for_condition(&write_queue, fifo_has_space,
                             &stats.blocked_writers, file);
    if (ret) {
        mutex_unlock(&fifo_mutex);
        return ret;
    }

    ret = fifo_write_bytes(buf, len);

    mutex_unlock(&fifo_mutex);

    if (ret > 0)
        wake_up_interruptible(&read_queue);

    return ret;
}

static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct myfifo_stats kstats;

    switch (cmd) {
    case MYFIFO_IOC_GET_STATS:
        if (mutex_lock_interruptible(&fifo_mutex))
            return -ERESTARTSYS;

        kstats = stats;

        mutex_unlock(&fifo_mutex);

        if (copy_to_user((void __user *)arg, &kstats, sizeof(kstats)))
            return -EFAULT;

        return 0;

    default:
        return -ENOTTY;
    }
}

/* --- /proc support --- */

static int myfifo_proc_show(struct seq_file *m, void *v)
{
    struct myfifo_stats kstats;

    if (mutex_lock_interruptible(&fifo_mutex))
        return -ERESTARTSYS;

    kstats = stats;

    mutex_unlock(&fifo_mutex);

    seq_printf(m,
               "bytes_written: %lu\n"
               "bytes_read: %lu\n"
               "blocked_writers: %lu\n"
               "blocked_readers: %lu\n",
               kstats.bytes_written, kstats.bytes_read,
               kstats.blocked_writers, kstats.blocked_readers);
    return 0;
}

static int myfifo_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, myfifo_proc_show, NULL);
}

static const struct proc_ops myfifo_proc_ops = {
    .proc_open    = myfifo_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* --- File operations table --- */

static const struct file_operations my_fops = {
    .owner          = THIS_MODULE,
    .open           = my_open,
    .release        = my_release,
    .read           = my_read,
    .write          = my_write,
    .unlocked_ioctl = my_ioctl,
};

/* --- Module init/exit --- */

static int __init mydrv_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&my_cdev, &my_fops);
    my_cdev.owner = THIS_MODULE;

    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    my_class = class_create(CLASS_NAME);
    if (IS_ERR(my_class)) {
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(my_class);
    }

    if (IS_ERR(device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME))) {
        class_destroy(my_class);
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }

    mutex_init(&fifo_mutex);
    init_waitqueue_head(&read_queue);
    init_waitqueue_head(&write_queue);

    fifo_head = fifo_tail = fifo_count = 0;
    memset(&stats, 0, sizeof(stats));

    proc_entry = proc_create(PROC_NAME, 0, NULL, &myfifo_proc_ops);
    if (!proc_entry) {
        device_destroy(my_class, dev_num);
        class_destroy(my_class);
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }

    printk(KERN_INFO "myfifo: loaded, major=%d minor=%d\n",
           MAJOR(dev_num), MINOR(dev_num));
    return 0;
}

static void __exit mydrv_exit(void)
{
    if (proc_entry)
        proc_remove(proc_entry);

    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);

    printk(KERN_INFO "myfifo: unloaded\n");
}

module_init(mydrv_init);
module_exit(mydrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BigJimmy");
MODULE_DESCRIPTION("FIFO char driver used as a message queue and stats source");
