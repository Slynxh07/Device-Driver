#include "key_handler.h"
#include <stdio.h>

char *keys_to_string(int modifier, int *keycodes, int num_keys);

char *get_keys(const unsigned char *report, size_t size)
{
    if (size < 2) return NULL;
    int modifier = report[0];
    int max_keycodes = size - 2;
    int *keycodes = malloc((sizeof(int)) * max_keycodes);
    if (!keycodes) return NULL;
    int num_keys = 0;
    for (int i = 0; i < max_keycodes; i++)
    {
        if (report[2 + i] != 0)
        {
            keycodes[num_keys++] = report[2 + i];
        }
    }
    char *str = keys_to_string(modifier, keycodes, num_keys);
    free(keycodes);
    return str;
}

char *keys_to_string(int modifier, int *keycodes, int num_keys)
{
    char *key_str = malloc(num_keys + 1);
    if (!key_str) return NULL;

    int shift = modifier & (0x02 | 0x20);
    int j = 0;

    for (int i = 0; i < num_keys; i++)
    {
        int key = keycodes[i];

        if (key >= S_KEY_A && key <= S_KEY_Z)
            key_str[j++] = (shift ? 'A' : 'a') + (key - S_KEY_A);

        else if (key == S_KEY_SPACE)
            key_str[j++] = ' ';

        else if (key == S_KEY_ENTER)
            key_str[j++] = '\n';

        else if (key >= S_KEY_1 && key <= S_KEY_9)
            key_str[j++] = '1' + (key - S_KEY_1);

        else if (key == S_KEY_0)
            key_str[j++] = '0';
    }

    key_str[j] = '\0';
    return key_str;
}