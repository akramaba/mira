#include "inc/mira.h"
#include "inc/mira2d.h"

// Example of creating a window and updating it
// Soon, the shell will essentially be a desktop!

/*
 * TODOs:
 * - Create a system call for getting an asset from assets.bin into user space
 * - Implement the shell, a basic desktop with a taskbar and app icons
 * - Implement opening new applications through the app icons
 * - Create a system call for moving windows
 * - Implement window dragging, where the shell manages mouse input
 * - Add frames to windows with borders, title bars, and buttons
 */

void ms_entry(void) {
    int window_id = mira_create_window(0, 0, 1280, 720);
    m2d_context* ctx = m2d_create_context(1280, 720);
    m2d_set_window(ctx, window_id);

    // Draw some rounded rectangles with anti-aliasing
    m2d_clear(ctx, M2D_COLOR_BLACK);
    m2d_draw_rounded_rect(ctx,3.37*96,6.8*96,6.6*96,0.56*96,16,0x00262626);
    m2d_draw_rounded_rect(ctx,2.46*96,1.61*96,3.27*96,2.67*96,15,0x00D1D1D1);
    m2d_present(ctx);

    while (1) {};
}
