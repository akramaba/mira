#ifndef MIRA_STATUS_H
#define MIRA_STATUS_H

#include "../inc/mira2d.h"
#include "../inc/font.h"
#include "../inc/string.h"
#include "../inc/util.h"
#include "../inc/mira.h"

// Status Panel Design
#define STATUS_X 18           
#define STATUS_Y 18           
#define STATUS_WIDTH 684      
#define STATUS_HEIGHT 300     
#define STATUS_RADIUS 18
#define STATUS_PADDING 12
#define STATUS_BG_COLOR 0x171717
#define STATUS_FONT_HEIGHT 22

// Internal state for the status module
static m2d_context* g_status_ctx = NULL;

// Data variables updated by the monitor task and displayed by the draw task
static uint64_t g_status_uptime_ms = 0;
static uint32_t g_status_task_count = 0;
static uint32_t g_status_total_exceptions = 0;
static uint64_t g_status_last_rdtsc = 0;
static uint64_t g_status_last_latency = 0;

// Initializes the status monitor context
static inline void status_init(m2d_context* context) {
    g_status_ctx = context;
    g_status_uptime_ms = 0;
    g_status_task_count = 0;
    g_status_total_exceptions = 0;
    g_status_last_rdtsc = 0;
    g_status_last_latency = 0;
}

// Updates the internal status variables from the kernel
static inline void status_update(uint64_t uptime_ms, uint32_t tasks, uint32_t exceptions) {
    // * The difference between the previous RDTSC and now is a measure 
    // * of the monitor task's scheduling latency (including the sleep time).
    uint64_t now_rdtsc = mira_rdtsc();
    if (g_status_last_rdtsc != 0) {
        g_status_last_latency = now_rdtsc - g_status_last_rdtsc;
    }
    g_status_last_rdtsc = now_rdtsc;

    // Update the values received from the kernel status call
    g_status_uptime_ms = uptime_ms;
    g_status_task_count = tasks;
    g_status_total_exceptions = exceptions;
}

// Draws the current state of the status panel to its graphical context
static inline void status_draw(void) {
    if (!g_status_ctx) return;

    // Self-update before drawing
    uint32_t tasks = 0, exceptions = 0;
    mira_get_system_info(&tasks, &exceptions);
    g_status_uptime_ms += 16; // Renders every 16ms
    status_update(g_status_uptime_ms, tasks, exceptions);

    // Draw the rounded rectangle background
    m2d_draw_rounded_rect(g_status_ctx, STATUS_X, STATUS_Y, STATUS_WIDTH, STATUS_HEIGHT, STATUS_RADIUS, STATUS_BG_COLOR);

    int text_x = STATUS_X + STATUS_PADDING;
    int text_y = STATUS_Y + STATUS_PADDING;
    int line_offset = STATUS_FONT_HEIGHT + 4;
    char buffer[30];

    // Title
    ms_font_draw_string(g_status_ctx, "--- Kernel Status ---", text_x, text_y, M2D_COLOR_WHITE);
    text_y += line_offset * 2;

    // Uptime (Formatted: S.MS)
    uint64_t uptime_s = g_status_uptime_ms / 1000;
    uint32_t uptime_ms = g_status_uptime_ms % 1000;
    u64toa(uptime_s, buffer);
    ms_font_draw_string(g_status_ctx, "Uptime: ", text_x, text_y, M2D_COLOR_WHITE);
    ms_font_draw_string(g_status_ctx, buffer, text_x + 120, text_y, M2D_COLOR_WHITE);
    
    // Print milliseconds (format as 3 digits)
    buffer[0] = (uptime_ms / 100) + '0';
    buffer[1] = ((uptime_ms / 10) % 10) + '0';
    buffer[2] = (uptime_ms % 10) + '0';
    buffer[3] = '\0';
    ms_font_draw_string(g_status_ctx, ".", text_x + 120 + strlen(u64toa(uptime_s, buffer)), text_y, M2D_COLOR_WHITE);
    ms_font_draw_string(g_status_ctx, buffer, text_x + 120 + strlen(u64toa(uptime_s, buffer)) + 5, text_y, 0xAAFFFF);
    text_y += line_offset;

    // Task Count
    u64toa(g_status_task_count, buffer);
    ms_font_draw_string(g_status_ctx, "# Tasks: ", text_x, text_y, M2D_COLOR_WHITE);
    ms_font_draw_string(g_status_ctx, buffer, text_x + 120, text_y, 0x00FF00); // Green
    text_y += line_offset;

    // Total Exceptions
    u64toa(g_status_total_exceptions, buffer);
    ms_font_draw_string(g_status_ctx, "Exceptions: ", text_x, text_y, M2D_COLOR_WHITE);
    ms_font_draw_string(g_status_ctx, buffer, text_x + 120, text_y, 0xFF0000); // Red
    text_y += line_offset;
    
    // Monitor Task Latency
    u64toa(g_status_last_latency, buffer);
    ms_font_draw_string(g_status_ctx, "Monitor Latency: ", text_x, text_y, M2D_COLOR_WHITE);
    ms_font_draw_string(g_status_ctx, buffer, text_x + 200, text_y, M2D_COLOR_YELLOW);
    ms_font_draw_string(g_status_ctx, " ticks", text_x + 200 + strlen(buffer) * 10, text_y, M2D_COLOR_WHITE);
    text_y += line_offset;
}

#endif