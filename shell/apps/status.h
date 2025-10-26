#ifndef MIRA_STATUS_H
#define MIRA_STATUS_H

#include "../inc/mira2d.h"
#include "../inc/font_24.h"
#include "../inc/string.h"
#include "../inc/util.h"
#include "../inc/mira.h"

// Status Panel Design
#define STATUS_X 67           
#define STATUS_Y 193           
#define STATUS_WIDTH 475      
#define STATUS_HEIGHT 355     
#define STATUS_RADIUS 18
#define STATUS_PADDING 15
#define STATUS_BG_COLOR 0x171717
#define STATUS_FONT_HEIGHT 22

// Global latency value (from shell.c)
extern volatile uint64_t g_last_benign_latency;

// Internal state for the status module
static m2d_context* g_status_ctx = NULL;

// Data variables updated by the monitor task and displayed by the draw task
static uint64_t g_status_uptime_ms = 0;
static uint32_t g_status_task_count = 0;
static uint32_t g_status_total_exceptions = 0;

// Initializes the status monitor context
static inline void status_init(m2d_context* context) {
    g_status_ctx = context;
    g_status_uptime_ms = 0;
    g_status_task_count = 0;
    g_status_total_exceptions = 0;
}

// Updates the internal status variables from the kernel
static inline void status_update(uint64_t uptime_ms, uint32_t tasks, uint32_t exceptions) {
    // Update the values received from the kernel status call,
    // which is done right before this in the draw function.
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
    g_status_uptime_ms += 687; // Found to be ~687ms between draws at 60FPS
    status_update(g_status_uptime_ms, tasks, exceptions);
    
    // Buffer for number formatting and display
    char buffer[30];

    // Dashboard Title
    ms_font_24_draw_string(g_status_ctx, "Mira Sentient System", 71, 17, M2D_COLOR_WHITE);
    // Dashboard Icon
    m2d_draw_image(g_status_ctx, "MiraSentientSystem.mi", 20, 13);

    // Draw the rounded rectangle background
    m2d_draw_rounded_rect(g_status_ctx, STATUS_X, STATUS_Y, STATUS_WIDTH, STATUS_HEIGHT, STATUS_RADIUS, STATUS_BG_COLOR);

    // Status Header
    ms_font_24_draw_string(g_status_ctx, "System Status", 133, 208, M2D_COLOR_WHITE);
    // Status Icon
    m2d_draw_image(g_status_ctx, "MiraSystemStatus.mi", 83, 206);

    // Uptime Row
    ms_font_24_draw_string(g_status_ctx, "Uptime:", 131, 273, M2D_COLOR_WHITE);
    
    uint64_t uptime_s = g_status_uptime_ms / 1000;
    uint32_t uptime_ms = g_status_uptime_ms % 1000;
    int current_x = 240;

    // Seconds
    u64toa(uptime_s, buffer);
    ms_font_24_draw_string(g_status_ctx, buffer, current_x, 273, 0x0096FF /* Blue */);

    // Dot
    current_x += strlen(buffer) * 14;
    ms_font_24_draw_string(g_status_ctx, ".", current_x, 273, 0x0096FF);
    
    // Milliseconds
    current_x += 14;
    buffer[0] = (uptime_ms / 100) + '0';
    buffer[1] = ((uptime_ms / 10) % 10) + '0';
    buffer[2] = (uptime_ms % 10) + '0';
    buffer[3] = '\0';
    ms_font_24_draw_string(g_status_ctx, buffer, current_x, 273, 0x0096FF);

    // " seconds" text
    current_x += 3 * 14;
    ms_font_24_draw_string(g_status_ctx, " seconds", current_x, 273, M2D_COLOR_WHITE);
    m2d_draw_line(g_status_ctx, 83, 318, 527, 318, 2, M2D_COLOR_WHITE);

    // Tasks Row
    ms_font_24_draw_string(g_status_ctx, "Tasks:", 131, 273+62, M2D_COLOR_WHITE);
    u64toa(g_status_task_count, buffer);
    ms_font_24_draw_string(g_status_ctx, buffer, 225, 273+62, 0x00B050 /* Green */);
    m2d_draw_line(g_status_ctx, 83, 318+62, 527, 318+62, 2, M2D_COLOR_WHITE);

    // Exceptions Row
    ms_font_24_draw_string(g_status_ctx, "Exceptions:", 131, 273+124, M2D_COLOR_WHITE);
    u64toa(g_status_total_exceptions, buffer);
    ms_font_24_draw_string(g_status_ctx, buffer, 295, 273+124, M2D_COLOR_RED);
    m2d_draw_line(g_status_ctx, 83, 318+124, 527, 318+124, 2, M2D_COLOR_WHITE);

    // Latency Row
    ms_font_24_draw_string(g_status_ctx, "Latency:", 131, 273+186, M2D_COLOR_WHITE);
    
    if (g_last_benign_latency == 0) {
        buffer[0] = '.';
        buffer[1] = '.';
        buffer[2] = '.';
        buffer[3] = '\0';
    } else {
        u64toa(g_last_benign_latency, buffer);
    }
    
    ms_font_24_draw_string(g_status_ctx, buffer, 255, 273+186, M2D_COLOR_YELLOW);
    ms_font_24_draw_string(g_status_ctx, " ticks", 255 + strlen(buffer) * 14, 273+186, M2D_COLOR_WHITE);
    m2d_draw_line(g_status_ctx, 83, 318+186, 527, 318+186, 2, M2D_COLOR_WHITE);

    // All heartbeat logos for each row
    m2d_draw_image(g_status_ctx, "MiraHeartbeat.mi", 83, 271);
    m2d_draw_image(g_status_ctx, "MiraHeartbeat.mi", 83, 271 + 62);
    m2d_draw_image(g_status_ctx, "MiraHeartbeat.mi", 83, 271 + 124);
    m2d_draw_image(g_status_ctx, "MiraHeartbeat.mi", 83, 271 + 186);
}

#endif