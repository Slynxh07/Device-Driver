#ifndef KEYHANDLER_H
#define KEYHANDLER_H

#include <stdlib.h>

char *get_keys(const unsigned char *report, size_t size);

typedef enum Keys
{
    S_KEY_NONE = 0x00,

    S_KEY_A = 0x04,
    S_KEY_B,
    S_KEY_C,
    S_KEY_D,
    S_KEY_E,
    S_KEY_F,
    S_KEY_G,
    S_KEY_H,
    S_KEY_I,
    S_KEY_J,
    S_KEY_K,
    S_KEY_L,
    S_KEY_M,
    S_KEY_N,
    S_KEY_O,
    S_KEY_P,
    S_KEY_Q,
    S_KEY_R,
    S_KEY_S,
    S_KEY_T,
    S_KEY_U,
    S_KEY_V,
    S_KEY_W,
    S_KEY_X,
    S_KEY_Y,
    S_KEY_Z,

    S_KEY_1,
    S_KEY_2,
    S_KEY_3,
    S_KEY_4,
    S_KEY_5,
    S_KEY_6,
    S_KEY_7,
    S_KEY_8,
    S_KEY_9,
    S_KEY_0,

    S_KEY_ENTER = 0x28,
    S_KEY_ESC,
    S_KEY_BACKSPACE,
    S_KEY_TAB,
    S_KEY_SPACE,

    S_KEY_LSHIFT = 0xE1,
    S_KEY_RSHIFT = 0xE5
} Keys;

#endif