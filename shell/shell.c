#include "inc/font.h"
#include "inc/string.h"
#include "inc/util.h"

// Graphical context for displaying information
static m2d_context* ctx = NULL;

// Test Case 1: A harmless, long-running application.
// The Sentient Kernel should never terminate this task.
int ms_benign_task_entry(void) {
    uint64_t start_time, end_time, latency;
    char latency_buffer[21];

    ms_font_draw_string(ctx, "...", 10, 50, 0xFFFFFF);
    m2d_present(ctx);

    while (1) {
        start_time = mira_rdtsc();

        // This task prints a message roughly every second to show it's alive.
        for (volatile int i = 0; i < 50000000; i++);
        end_time = mira_rdtsc();

        // Calculate and print the latency in raw CPU ticks.
        latency = end_time - start_time;
        u64toa(latency, latency_buffer);
        mira_print("Benign Task: Still running... (Latency: ", 0);
        mira_print(latency_buffer, 0);
        mira_print(" ticks)\n", 0);

        // Update the graphical panel with the latest latency.
        m2d_clear(ctx, 0x000000);
        ms_font_draw_string(ctx, "Mira Sentient System", 10, 10, 0xFFFFFF);
        ms_font_draw_string(ctx, "Benign Task Latency (ticks): ", 10, 50, 0xFFFFFF);
        ms_font_draw_string(ctx, latency_buffer, 300, 50, 0x00FF00);
        m2d_draw_line(ctx, 10, 80, 400, 80, 0xFFFFFF);
        m2d_present(ctx);
    }

    return 0;
}

// Test Case 2: The brute-force pf_virus.
// This is designed to be caught by the fast-path handler in Sentient.
int ms_pf_virus_entry(void) {
    // This infinite loop generates page faults at the maximum possible rate.
    // It should be detected and quarantined in about 20 milliseconds by the
    // Emergency Fast-Path in the page fault handler.
    while (1) {
        *((volatile int*)0x140000000) = 1;
    }

    return 0;
}

// Test Case 3: The "low-and-slow" pf_virus_stealth.
// This is designed to evade the fast-path and be caught by the Sentient profiler.
int ms_pf_stealth_virus_entry(void) {
    // This loop generates page faults at a rate designed to be below the
    // fast-path's burst threshold but well above the profiler's sustained
    // threshold. The profiler should detect this within a few of its cycles.
    while (1) {
        *((volatile int*)0x140000000) = 1;
        for (volatile int i = 0; i < 5000; i++);
    }

    return 0;
}

// Test Case 4: The "moderate" adaptive_virus.
// This is designed to trigger the new Adaptive Controller.
int ms_adaptive_virus_entry(void) {
    long heartbeat_counter = 0;

    // This loop generates faults at a "moderate" rate. It's too slow to
    // trigger Sentient's emergency fast-path, but fast enough to be a clear
    // anomaly for the adaptive system's EMA-based detector. The goal is to
    // observe the adaptive system applying and learning from different
    // throttling levels rather than sticking to one that may be suboptimal.
    while (1) {
        *((volatile int*)0x140000000) = 1;

        // Every 50 loops, print a period to the console.
        // This gives us a visible signal of the task's execution rate.
        if (++heartbeat_counter > 50) {
            mira_print(".", 0);
            heartbeat_counter = 0;
        }

        for (volatile int i = 0; i < 80000; i++);
    }

    return 0;
}

// Mira Shell Entry Point
// The shell now acts as a test group for the Sentient and Adaptive Kernel systems.
// It also displays a panel for relevant information during the testing.
void ms_entry(void) {
    // Graphics setup
    int window = mira_create_window(0, 0, 1280, 720);
    ctx = m2d_create_context(1280, 720);
    m2d_set_window(ctx, window);

    // Render the sticky parts of the panel once before starting tests.
    m2d_clear(ctx, 0x000000); // Only clear one time to keep previous text!
    ms_font_draw_string(ctx, "Mira Sentient System", 10, 10, 0xFFFFFF);
    m2d_present(ctx);

    mira_print("--- Mira OS Adaptive Defense Test ---\n", 0);
    mira_print("Spawning tasks to validate the multi-layer defense system.\n\n", 0);

    // 1. Launch the benign task. It should not be interrupted.
    mira_print("Launching benign task (PID 4)...\n", 0);
    mira_execute_task(ms_benign_task_entry, "Benign Task");

    // 2. Launch the brute-force virus. Should be quarantined by the Sentient Fast-Path.
    mira_print("Launching brute-force pf_virus (PID 5)...\n", 0);
    mira_execute_task(ms_pf_virus_entry, "PF Virus (Brute Force)");

    // 3. Launch the stealth virus. Should be quarantined by the Sentient Profiler.
    mira_print("Launching pf_virus_stealth (PID 6)...\n", 0);
    mira_execute_task(ms_pf_stealth_virus_entry, "PF Virus (Stealth)");

    // 4. Launch the moderate virus. Should be handled by the Adaptive Controller.
    mira_print("Launching adaptive_virus (PID 7)...\n", 0);
    mira_execute_task(ms_adaptive_virus_entry, "Adaptive Virus");

    // 5. Launch the fork bomb. Should all be stopped by the Sentient Fast-Path.
    mira_print("Launching fork_bomb (PID 8 - 31)...\n", 0);
    for (int i = 0; i < 24; i++) {
        mira_execute_task(ms_pf_virus_entry, "Fork Bomb Instance");
    }

    mira_print("\nAll test tasks launched. Monitoring output...\n", 0);
    mira_print("------------------------------------------\n", 0);

    // The shell's work is done. It now idles while the tasks run.
    while (1) {};
}