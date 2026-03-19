#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define KB_MAGIC 'k'
#define KB_IOCTL_RESET    _IO(KB_MAGIC, 1)
#define KB_IOCTL_ENABLE   _IO(KB_MAGIC, 2)
#define KB_IOCTL_DISABLE  _IO(KB_MAGIC, 3)

int main()
{
    int fd = open("/dev/keydriver0", O_RDWR);
    int input;

    if (fd < 0)
    {
        perror("open failed");
        return -1;
    }

    printf("Select IOCTL: \n1 (RESET)\n2 (ENABLE LOGGING)\n3 (DISABLE LOGGING)\n");
    scanf("%d", &input);

    switch (input)
    {
    case 1:
        ioctl(fd, KB_IOCTL_RESET);
        printf("Reset sent\n");
        break;

    case 2:
        ioctl(fd, KB_IOCTL_ENABLE);
        printf("Enable sent\n");
        break;

    case 3:
        ioctl(fd, KB_IOCTL_DISABLE);
        printf("Disable sent\n");
        break;

    default:
        printf("Invalid option\n");
        break;
    }

    close(fd);
    return 0;
}