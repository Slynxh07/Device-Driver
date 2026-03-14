#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "key_handler.h"

#define KB_REPORT_SIZE 8

static int fd;


void* signal_thread(void* arg) {
    sigset_t set;
    int sig;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);

    while (1) {
        sigwait(&set, &sig);
        printf("\nShutting down cleanly...\n");
        close(fd);
        exit(0);
    }

    return NULL;
}

int main()
{
    setbuf(stdout, NULL);
    printf("starting program\n");

    fd = open("/dev/keydriver0", O_RDONLY);

    if (fd < 0)
    {
        perror("open failed");
        return -1;
    }

    unsigned char report[KB_REPORT_SIZE];

    pthread_t tid;
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    pthread_create(&tid, NULL, signal_thread, NULL);

    while (1)
    {
        read(fd, report, KB_REPORT_SIZE);
        char *keys_string = get_keys(report, KB_REPORT_SIZE);
        if (keys_string)
        {
            printf("%s", keys_string);
            free(keys_string);
        }
    }
}