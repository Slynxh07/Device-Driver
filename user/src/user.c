#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "key_handler.h"

#define KB_REPORT_SIZE 8

int main()
{
    setbuf(stdout, NULL);
    printf("starting program\n");

    int fd = open("/dev/keydriver0", O_RDONLY);

    if (fd < 0)
    {
        perror("open failed");
        return -1;
    }

    unsigned char report[KB_REPORT_SIZE];

    while (1)
    {
        read(fd, report, KB_REPORT_SIZE);
        //printf("Report: ");
        //for (int i = 0; i < KB_REPORT_SIZE; i++) printf("%02x ", report[i]);
        char *keys_string = get_keys(report, KB_REPORT_SIZE);
        if (keys_string)
        {
            printf("%s", keys_string);
            free(keys_string);
        }
    }
    printf("\n");
}