#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd = open("/dev/keydriver0", O_RDONLY);
    unsigned char report[8];

    while (1)
    {
        read(fd, report, 8);
        printf("Keycode: %d\n", report[2]);
    }
}