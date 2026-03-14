#ifndef KEYHANDLER_H
#define KEYHANDLER_H

#include <stdlib.h>

char *get_keys(const unsigned char *report, size_t size);

typedef enum Keys
{
    KEY_NONE = 0x00,

    KEY_A = 0x04,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,

    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,

    KEY_ENTER = 0x28,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_SPACE,

    KEY_LSHIFT = 0xE1,
    KEY_RSHIFT = 0xE5
} Keys;

#endif