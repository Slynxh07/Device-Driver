#ifndef MYFIFO_IOCTL_U_H
#define MYFIFO_IOCTL_U_H

#include <sys/ioctl.h>
#include <stdint.h>

struct myfifo_stats {
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t blocked_writers;
    uint64_t blocked_readers;
};

#define MYFIFO_IOC_MAGIC 'k'
#define MYFIFO_IOC_GET_STATS  _IOR(MYFIFO_IOC_MAGIC, 1, struct myfifo_stats)

#endif