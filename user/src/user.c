#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <string.h>

#include "key_handler.h"

#define KB_MAGIC 'k'
#define KB_IOCTL_RESET    _IO(KB_MAGIC, 1)
#define KB_IOCTL_ENABLE   _IO(KB_MAGIC, 2)
#define KB_IOCTL_DISABLE  _IO(KB_MAGIC, 3)

#define KB_REPORT_SIZE 8
#define BUFFER_SIZE 4096

static int fd;
static char text_buffer[BUFFER_SIZE] = {0};
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

void* reader_thread(void* arg)
{
    unsigned char report[KB_REPORT_SIZE];

    while (1)
    {
        int n = read(fd, report, KB_REPORT_SIZE);
        if (n > 0)
        {
            char *keys = get_keys(report, KB_REPORT_SIZE);
            if (keys)
            {
                pthread_mutex_lock(&buffer_mutex);

                if (strlen(text_buffer) + strlen(keys) < BUFFER_SIZE) strcat(text_buffer, keys);

                pthread_mutex_unlock(&buffer_mutex);
                free(keys);
            }
        }
    }
    return NULL;
}

int main(void)
{
    fd = open("/dev/keydriver0", O_RDWR);
    if (fd < 0)
    {
        perror("open failed");
        return -1;
    }

    pthread_t tid;
    pthread_create(&tid, NULL, reader_thread, NULL);

    const int W = 1200, H = 750;
    InitWindow(W, H, "Keyboard User Space");
    SetTargetFPS(60);

    const int PAD = 12;
    const int BTN_W = 220;
    const int BTN_H = 80;

    while (!WindowShouldClose())
    {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        Rectangle txt_panel = { PAD, PAD, sw - BTN_W - PAD*3, sh - PAD*2 };

        int btn_x = txt_panel.x + txt_panel.width + PAD;
        int total_btn_h = BTN_H * 3 + PAD * 2;
        int btn_y = (sh - total_btn_h) / 2;

        Rectangle btn_reset   = { btn_x, btn_y, BTN_W, BTN_H };
        Rectangle btn_enable  = { btn_x, btn_y + BTN_H + PAD, BTN_W, BTN_H };
        Rectangle btn_disable = { btn_x, btn_y + (BTN_H + PAD)*2, BTN_W, BTN_H };

        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawRectangleLinesEx(txt_panel, 2, BLACK);

        pthread_mutex_lock(&buffer_mutex);

        DrawText(text_buffer, txt_panel.x + 10, txt_panel.y + 10, 20, BLACK);

        pthread_mutex_unlock(&buffer_mutex);

        if (GuiButton(btn_reset, "Reset proc"))
        {
            ioctl(fd, KB_IOCTL_RESET);
            pthread_mutex_lock(&buffer_mutex);
            text_buffer[0] = '\0';
            pthread_mutex_unlock(&buffer_mutex);
        }

        if (GuiButton(btn_enable, "Enable Logging"))
        {
            ioctl(fd, KB_IOCTL_ENABLE);
        }

        if (GuiButton(btn_disable, "Disable Logging"))
        {
            ioctl(fd, KB_IOCTL_DISABLE);
        }

        EndDrawing();
    }

    close(fd);
    CloseWindow();
    return 0;
}