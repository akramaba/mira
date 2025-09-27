#include "inc/mira.h"
#include "inc/string.h"

// Test Case 1: A harmless, long-running application.
// The Sentient Kernel should never terminate this task.
int ms_benign_task_entry(void) {
    while (1) {
        // This task prints a message roughly every second to show it's alive.
        for (volatile int i = 0; i < 50000000; i++);
        mira_print("Benign Task: Still running...\n", 0);
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

        for (volatile int i = 0; i < 40000; i++);
    }

    return 0;
}

// Mira Shell Entry Point
// The shell now acts as a test group for the Sentient and Adaptive Kernel systems.
void ms_entry(void) {
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

    mira_print("\nAll test tasks launched. Monitoring output...\n", 0);
    mira_print("------------------------------------------\n", 0);

    // The shell's work is done. It now idles while the other tasks run.
    while (1) {};
}