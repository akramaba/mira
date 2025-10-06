#ifndef MIRA2D_H
#define MIRA2D_H

#include "mira.h"

#define M2D_COLOR_RED         0x00FF0000
#define M2D_COLOR_ORANGE      0x00FFA500
#define M2D_COLOR_YELLOW      0x00FFFF00
#define M2D_COLOR_GREEN       0x0000FF00
#define M2D_COLOR_BLUE        0x000000FF
#define M2D_COLOR_PURPLE      0x800080
#define M2D_COLOR_PINK        0xFFC0CB
#define M2D_COLOR_BLACK       0x00000000
#define M2D_COLOR_WHITE       0x00FFFFFF

// Core //

typedef struct {
    uint32_t* framebuffer;
    int width;
    int height;
    int window_id;
} m2d_context;

static inline m2d_context* m2d_create_context(int width, int height) {
    m2d_context* ctx = (m2d_context*)mira_malloc(sizeof(m2d_context));

    ctx->framebuffer = (uint32_t*)mira_malloc(width * height * sizeof(uint32_t));
    ctx->width = width;
    ctx->height = height;

    return ctx;
}

static inline void m2d_set_window(m2d_context* ctx, int window_id) {
    if (window_id >= 0) {
        // Set the window ID for the drawing context
        ctx->window_id = window_id;
    }
}

static inline void m2d_clear(m2d_context* ctx, uint32_t color) {
    // Clear framebuffer
    for (int i = 0; i < ctx->width * ctx->height; i++) {
        ctx->framebuffer[i] = color;
    }
}

static inline void m2d_present(m2d_context* ctx) {
    if (ctx->window_id >= 0) {
        // Present the framebuffer to the screen
        mira_update_window(ctx->window_id, ctx->framebuffer);
    }
}

// Basics //

static inline void m2d_draw_pixel(m2d_context* ctx, int x, int y, uint32_t color) {
    if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height) {
        ctx->framebuffer[y * ctx->width + x] = color;
    }
}

static inline void m2d_draw_line(m2d_context* ctx, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int abs_dx = dx > 0 ? dx : -dx;
    int abs_dy = dy > 0 ? dy : -dy;
    int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    int sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    int err = (abs_dx > abs_dy ? abs_dx : -abs_dy) / 2;
    int e2;

    while (1) {
        m2d_draw_pixel(ctx, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -abs_dx) { err -= abs_dy; x0 += sx; }
        if (e2 < abs_dy) { err += abs_dx; y0 += sy; }
    }
}

// Rectangles //

static inline void m2d_draw_rect(m2d_context* ctx, int x, int y, int width, int height, uint32_t color) {
    for (int j = y; j < y + height; j++) {
        for (int i = x; i < x + width; i++) {
            if (i >= 0 && i < ctx->width && j >= 0 && j < ctx->height) {
                ctx->framebuffer[j * ctx->width + i] = color;
            }
        }
    }
}

static inline void _m2d_blend_color_on_black(uint32_t* dst, uint32_t src_color, int alpha) {
    // ? Fully transparent
    if (alpha <= 0) {
        return;
    }

    // ? Fully opaque
    if (alpha >= 255) {
        *dst = src_color;
        return;
    }

    // Extract source color components
    uint32_t sr = (src_color >> 16) & 0xFF;
    uint32_t sg = (src_color >> 8)  & 0xFF;
    uint32_t sb =  src_color        & 0xFF;

    // Scale color components by alpha
    uint32_t dr = (sr * alpha) >> 8;
    uint32_t dg = (sg * alpha) >> 8;
    uint32_t db = (sb * alpha) >> 8;

    *dst = (dr << 16) | (dg << 8) | db;
}

static inline void m2d_draw_rounded_rect(m2d_context* ctx, int x, int y, int width, int height, int radius, uint32_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (radius <= 0) {
        m2d_draw_rect(ctx, x, y, width, height, color);
        return;
    }

    // Pre-calculate boundaries
    int scale = 4; // ? Use 4x supersampling for anti-aliasing.
                  // ? This provides good quality without being too slow.
    uint64_t r2_scaled = (uint64_t)(radius * scale) * (radius * scale);

    // Calculate the boundaries of the inner rectangle (the non-rounded part)
    uint64_t left_in_s = (uint64_t)(x + radius) * scale;
    uint64_t right_in_s = (uint64_t)(x + width - radius) * scale;
    uint64_t top_in_s = (uint64_t)(y + radius) * scale;
    uint64_t bot_in_s = (uint64_t)(y + height - radius) * scale;

    // Clip drawing loops to the window
    int clip_x0 = x < 0 ? 0 : x;
    int clip_y0 = y < 0 ? 0 : y;
    int clip_x1 = (x + width) > ctx->width ? ctx->width : (x + width);
    int clip_y1 = (y + height) > ctx->height ? ctx->height : (y + height);

    // * We use 4 samples per pixel in a 2x2 grid (X, Y).
    // * These are the offsets within each pixel we'll test
    // * to determine how much of the pixel is covered by the shape.
    // * This provides us with higher quality anti-aliasing.
    int samples[4][2] = {{1, 1}, {3, 1}, {1, 3}, {3, 3}};

    // Loop over each pixel
    for (int j = clip_y0; j < clip_y1; ++j) {
        for (int i = clip_x0; i < clip_x1; ++i) {
            int hits = 0;
            
            // Supersampling: Test 4 sub-pixels to determine transparency
            for (int s = 0; s < 4; ++s) {
                uint64_t sx = (uint64_t)i * scale + samples[s][0];
                uint64_t sy = (uint64_t)j * scale + samples[s][1];

                // ? Check if the sample point is inside the main cross shape.
                // ? This is a fast check that covers most pixels.
                if ((sx >= left_in_s && sx < right_in_s) || (sy >= top_in_s && sy < bot_in_s)) {
                    hits++;
                    continue;
                }

                // ? If not in the cross, it must be in a corner region.
                // ? Find the distance to the center of the nearest corner's circle.
                uint64_t cx = (sx < left_in_s) ? left_in_s : right_in_s;
                uint64_t cy = (sy < top_in_s) ? top_in_s : bot_in_s;
                uint64_t dx = sx - cx;
                uint64_t dy = sy - cy;

                if (dx * dx + dy * dy <= r2_scaled) {
                    hits++;
                }
            }

            // * Hits are for the 4 sub-pixels we tested.
            // * Whenever we have at least one hit, we draw the pixel
            // * by blending the color based on how many hits we had.
            if (hits > 0) {
                int alpha = (hits * 255) / 4;
                _m2d_blend_color_on_black(&ctx->framebuffer[j * ctx->width + i], color, alpha);
            }
        }
    }
}

// Images //

// Text //

#endif