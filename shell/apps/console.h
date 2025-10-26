#ifndef MIRA_CONSOLE_H
#define MIRA_CONSOLE_H

#include "../inc/mira2d.h"
#include "../inc/font.h"
#include "../inc/font_24.h"
#include "../inc/string.h"

// Console Design
#define CONSOLE_X 610
#define CONSOLE_Y 57
#define CONSOLE_WIDTH 650
#define CONSOLE_HEIGHT 645
#define CONSOLE_RADIUS 18
#define CONSOLE_PADDING 12
#define CONSOLE_BG_COLOR 0x171717
#define CONSOLE_FONT_HEIGHT 22

// Calculated constants
#define CONSOLE_TEXT_AREA_HEIGHT (CONSOLE_HEIGHT - (CONSOLE_PADDING * 2))
#define CONSOLE_VISIBLE_LINES (CONSOLE_TEXT_AREA_HEIGHT / CONSOLE_FONT_HEIGHT)

// Text buffer constants
#define CONSOLE_BUFFER_LINES 4096
#define CONSOLE_LINE_CHARS 61

// Internal state for the console modul
static m2d_context* g_console_ctx = NULL;
static char g_console_buffer[CONSOLE_BUFFER_LINES][CONSOLE_LINE_CHARS];
static int g_console_current_line = 0;
static char g_partial_line_buffer[CONSOLE_LINE_CHARS] = {0};
static int g_partial_len = 0;

// Internal helper to commit the currently buffered line
static inline void _console_commit_line(void) {
    g_partial_line_buffer[g_partial_len] = '\0';
    memcpy(g_console_buffer[g_console_current_line], g_partial_line_buffer, g_partial_len + 1);
    g_console_current_line = (g_console_current_line + 1) % CONSOLE_BUFFER_LINES;
    g_console_buffer[g_console_current_line][0] = '\0';
    g_partial_len = 0;
}

// Initializes the console
static inline void console_init(m2d_context* context) {
    g_console_ctx = context;
    memset(g_console_buffer, 0, sizeof(g_console_buffer));
    g_console_current_line = 0;
    g_partial_len = 0;
}

// Logs a buffer of text to the console, with wrapping
static inline void console_log(const char* text) {
    if (!g_console_ctx) return;

    for (int i = 0; text[i] != '\0'; ++i) {
        char c = text[i];
        
        if (c == '\n') {
            _console_commit_line();
        } else {
            if (g_partial_len >= CONSOLE_LINE_CHARS - 1) {
                _console_commit_line();
            }

            g_partial_line_buffer[g_partial_len++] = c;
        }
    }
}

// Draws the current state of the console to its graphical context
static inline void console_draw(void) {
    if (!g_console_ctx) return;

    // Title
    ms_font_24_draw_string(g_console_ctx, "Debug Logs", 661, 11, M2D_COLOR_WHITE);
    // Icon
    m2d_draw_image(g_console_ctx, "MiraDebugLogs.mi", 610, 10);

    // Draw the new rounded rectangle background
    m2d_draw_rounded_rect(g_console_ctx, CONSOLE_X, CONSOLE_Y, CONSOLE_WIDTH, CONSOLE_HEIGHT, CONSOLE_RADIUS, CONSOLE_BG_COLOR);

    // Draw the visible portion of the text buffer
    for (int i = 0; i < CONSOLE_VISIBLE_LINES; ++i) {
        int buffer_index = (g_console_current_line - CONSOLE_VISIBLE_LINES + i + CONSOLE_BUFFER_LINES) % CONSOLE_BUFFER_LINES;

        if (g_console_buffer[buffer_index][0] != '\0') {
            // Calculate text position with padding
            int text_x = CONSOLE_X + CONSOLE_PADDING;
            int text_y = CONSOLE_Y + CONSOLE_PADDING + (i * CONSOLE_FONT_HEIGHT);
            
            // Only draw text if it's within the padded vertical area
            if (text_y < (CONSOLE_Y + CONSOLE_HEIGHT - CONSOLE_PADDING)) {
                ms_font_draw_string(g_console_ctx, g_console_buffer[buffer_index], text_x, text_y, M2D_COLOR_WHITE);
            }
        }
    }
}

#endif