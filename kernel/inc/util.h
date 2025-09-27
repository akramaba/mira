#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include "pit.h"

// Mira Kernel Input Byte
static inline uint8_t mk_util_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Mira Kernel Input Byte
static inline void mk_util_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Mira Kernel Sleep Function
static inline void mk_util_sleep(uint64_t ms) {
    uint64_t start = mk_pit_get_tick_count();
    while (mk_pit_get_tick_count() < start + ms);
}

// Mira Kernel Print Function
// Scrolling and newlines are handled.
static inline void mk_util_print(const char* str) {
    static uint16_t cursor = 0;
    static uint16_t* video_memory = (uint16_t*)0xb8000;
    const size_t total_chars = 80 * 25;
    const size_t row_length = 80;

    for (size_t i = 0; str[i] != '\0'; i++) {
        // Check for a newline character
        if (str[i] == '\n') {
            cursor = ((cursor / row_length) + 1) * row_length;
        } else {
            video_memory[cursor] = (video_memory[cursor] & 0xFF00) | str[i];
            cursor++;
        }

        // Check if we need to scroll
        if (cursor >= total_chars) {
            // Scroll the screen up by one row
            for (size_t j = 0; j < row_length * (25 - 1); j++) {
                video_memory[j] = video_memory[j + row_length];
            }

            // Clear the last row
            for (size_t j = row_length * (25 - 1); j < total_chars; j++) {
                video_memory[j] = (video_memory[j] & 0xFF00) | ' ';
            }

            // Reset cursor to the last row
            cursor = row_length * (25 - 1);
        }
    }
}

// Mira Kernel Random Number Generator
// Uses RDRAND if available, otherwise falls back to RDTSC.
static inline uint64_t mk_util_rand(void) {
    static int checked = 0;
    static int has_rdrand = 0;

    // Checks for RDRAND support only once by using CPUID
    // and checking for the RDRAND feature bit (ECX bit 30).
    if (!checked) {
        uint32_t ecx;
        __asm__ volatile("cpuid" : "=c"(ecx) : "a"(1), "c"(0) : "ebx", "edx");
        has_rdrand = (ecx & (1 << 30));
        checked = 1;
    }

    // Use RDRAND if it is supported.
    if (has_rdrand) {
        uint64_t val;
        unsigned char success;

        // Retry up to 10 times as recommended by Intel.
        for (int i = 0; i < 10; ++i) {
            __asm__ volatile("rdrand %0; setc %1" : "=r"(val), "=q"(success));
            if (success) {
                return val;
            }
        }
    }

    // Fallback to the Time Stamp Counter if RDRAND is unavailable or fails.
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    return tsc;
}

// Mira Kernel Clear Screen Function
static inline void mk_util_clear_screen() {
    static uint16_t* video_memory = (uint16_t*)0xb8000;
    for (size_t i = 0; i < 80 * 25; i++) {
        video_memory[i] = (video_memory[i] & 0xFF00) | ' ';
    }
}

#endif