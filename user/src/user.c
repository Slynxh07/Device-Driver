#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd = open("/dev/keydriver0", O_RDONLY);
    unsigned char key;

    while (1)
    {
        int n = read(fd, &key, 1);
        if (n == 1)
            printf("Keycode: %d\n", key);
    }
}