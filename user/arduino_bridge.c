#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

struct myfifo_stats {
    unsigned long bytes_written;
    unsigned long bytes_read;
    unsigned long blocked_writers;
    unsigned long blocked_readers;
};

#define MYFIFO_IOC_MAGIC 'k'
#define MYFIFO_IOC_GET_STATS _IOR(MYFIFO_IOC_MAGIC, 1, struct myfifo_stats)

static const char *fifo_dev    = "/dev/myfifo";
static const char *arduino_dev = "/dev/ttyACM0";  // override via argv[1] if needed

static int fd_fifo  = -1;
static int fd_ardu  = -1;
static volatile int stop_flag = 0;

static void fatal(const char *msg)
{
    perror(msg);
    exit(1);
}

static void setup_arduino_serial(int fd)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0)
        fatal("tcgetattr");

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);

    if (tcsetattr(fd, TCSANOW, &tio) < 0)
        fatal("tcsetattr");
}

/* Thread: FIFO -> Arduino */
static void *fifo_to_arduino(void *arg)
{
    char buf[256];

    while (!stop_flag) {
        ssize_t n = read(fd_fifo, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("fifo_to_arduino: read");
            break;
        } else if (n == 0) {
            continue;
        }

        ssize_t off = 0;
        while (off < n && !stop_flag) {
            ssize_t w = write(fd_ardu, buf + off, n - off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                perror("fifo_to_arduino: write");
                stop_flag = 1;
                break;
            }
            off += w;
        }
    }
    return NULL;
}

/* Thread: Arduino -> FIFO */
static void *arduino_to_fifo(void *arg)
{
    char buf[256];

    while (!stop_flag) {
        ssize_t n = read(fd_ardu, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("arduino_to_fifo: read");
            break;
        } else if (n == 0) {
            continue;
        }

        ssize_t off = 0;
        while (off < n && !stop_flag) {
            ssize_t w = write(fd_fifo, buf + off, n - off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN)
                    continue;
                perror("arduino_to_fifo: write");
                stop_flag = 1;
                break;
            }
            off += w;
        }
    }
    return NULL;
}

static void show_stats(void)
{
    struct myfifo_stats s;
    if (ioctl(fd_fifo, MYFIFO_IOC_GET_STATS, &s) < 0) {
        perror("ioctl MYFIFO_IOC_GET_STATS");
        return;
    }

    printf("\n[myfifo stats]\n");
    printf("  bytes_written   = %lu\n", s.bytes_written);
    printf("  bytes_read      = %lu\n", s.bytes_read);
    printf("  blocked_writers = %lu\n", s.blocked_writers);
    printf("  blocked_readers = %lu\n", s.blocked_readers);
}

int main(int argc, char **argv)
{
    if (argc >= 2)
        arduino_dev = argv[1];

    printf("Opening FIFO device: %s\n", fifo_dev);
    fd_fifo = open(fifo_dev, O_RDWR);
    if (fd_fifo < 0)
        fatal("open fifo_dev");

    printf("Opening Arduino serial: %s\n", arduino_dev);
    fd_ardu = open(arduino_dev, O_RDWR | O_NOCTTY);
    if (fd_ardu < 0)
        fatal("open arduino_dev");

    setup_arduino_serial(fd_ardu);

    printf("Started bridge.\n");
    printf(" - Anything written to /dev/myfifo goes to Arduino.\n");
    printf(" - Anything Arduino sends appears in /dev/myfifo.\n");
    printf("Press Ctrl+C to stop this terminal, or use 'q' in the menu.\n");

    pthread_t t1, t2;
    if (pthread_create(&t1, NULL, fifo_to_arduino, NULL) != 0)
        fatal("pthread_create fifo_to_arduino");
    if (pthread_create(&t2, NULL, arduino_to_fifo, NULL) != 0)
        fatal("pthread_create arduino_to_fifo");

    while (!stop_flag) {
        char line[256];
        printf("\nCommands:\n");
        printf("  s - show stats\n");
        printf("  w - write test line to FIFO (goes to Arduino)\n");
        printf("  q - quit\n");
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        if (line[0] == 's') {
            show_stats();
        } else if (line[0] == 'w') {
            const char *msg = "test from user via FIFO\n";
            ssize_t n = write(fd_fifo, msg, strlen(msg));
            if (n < 0)
                perror("write to FIFO");
            else
                printf("Wrote %zd bytes to FIFO\n", n);
        } else if (line[0] == 'q') {
            stop_flag = 1;
        }
    }

    stop_flag = 1;
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    close(fd_ardu);
    close(fd_fifo);

    return 0;
}
