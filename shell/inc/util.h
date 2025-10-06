#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

// Unsigned 64-bit integer to ASCII conversion
static void u64toa(uint64_t n, char* str) {
    int i = 0;

    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    while (n != 0) {
        int rem = n % 10;
        str[i++] = rem + '0';
        n = n / 10;
    }

    str[i] = '\0';

    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
}

#endif