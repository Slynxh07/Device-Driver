/*

Test program that opens /dev/myfifo in blocking and non-blocking mode, 
runs multiple reader and writer threads to stress the FIFO, 
and finally uses ioctl to print driver statistics.

*/ 

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "myfifo_ioctl.h"

#define DEVICE_PATH "/dev/myfifo"
#define BUF_SIZE 128

int fd_blocking;
int fd_nonblocking;

void *writer_blocking_thread(void *arg)
{
    char buf[BUF_SIZE];
    memset(buf, 'A', sizeof(buf));
    // Now buf is filled with 'A' characters

    // Infinite loop to continuously write to the FIFO
    while (1) {
        // 'ret' stores the number of bytes successfully stored, or -1 for an error
        ssize_t ret = write(fd_blocking, buf, sizeof(buf));
        if (ret < 0) {
            perror("blocking writer: write");
            break;
        }
        usleep(50000);
    }
    return NULL;
}

void *writer_nonblocking_thread(void *arg)
{
    char buf[BUF_SIZE];
    memset(buf, 'B', sizeof(buf));

    while (1) {
        ssize_t ret = write(fd_nonblocking, buf, sizeof(buf));
        if (ret < 0) {
            if (errno == EAGAIN) {
                fprintf(stderr, "nonblocking writer: EAGAIN (buffer full)\n");
                usleep(50000);
                continue;
            } else {
                perror("nonblocking writer: write");
                break;
            }
        }
        usleep(50000);
    }
    return NULL;
}

void *reader_thread(void *arg)
{
    char buf[BUF_SIZE];

    while (1) {
        ssize_t ret = read(fd_blocking, buf, sizeof(buf));
        if (ret < 0) {
            perror("reader: read");
            break;
        }
        printf("reader: got %zd bytes\n", ret);
        usleep(100000);
    }
    return NULL;
}

int main(void)
{
    pthread_t twb, twnb, tr1, tr2;
    struct myfifo_stats st;

    fd_blocking = open(DEVICE_PATH, O_RDWR);
    if (fd_blocking < 0) {
        perror("open blocking");
        return 1;
    }

    fd_nonblocking = open(DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (fd_nonblocking < 0) {
        perror("open nonblocking");
        close(fd_blocking);
        return 1;
    }

    pthread_create(&twb,  NULL, writer_blocking_thread,   NULL);
    pthread_create(&twnb, NULL, writer_nonblocking_thread, NULL);
    pthread_create(&tr1,  NULL, reader_thread,            NULL);
    pthread_create(&tr2,  NULL, reader_thread,            NULL);

    sleep(10);

    if (ioctl(fd_blocking, MYFIFO_IOC_GET_STATS, &st) == -1) {
        perror("ioctl get stats");
    } else {
        printf("Stats from ioctl:\n");
        printf("bytes_written: %lu\n", (unsigned long)st.bytes_written);
        printf("bytes_read: %lu\n", (unsigned long)st.bytes_read);
        printf("blocked_writers: %lu\n", (unsigned long)st.blocked_writers);
        printf("blocked_readers: %lu\n", (unsigned long)st.blocked_readers);
    }

    close(fd_nonblocking);
    close(fd_blocking);
    return 0;
}