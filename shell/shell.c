#include "inc/mira.h"
#include "inc/mira2d.h"

// Mira Shell Entry Point for Sentient Test
void ms_entry(void) {
    mira_print("Mira Shell: Sentient OS Test Initializing...\n", 0);
    mira_print("The system should remain stable, but this shell task will be terminated.\n", 0);
    mira_print("Monitor the debug (serial) output for messages from the Profiler and Apoptosis systems.\n", 0);
    mira_print("\nIntentionally causing infinite page faults now.\n", 0);

    // This is the core of the test. We are writing to address 0x0 in an
    // infinite loop. Since this address is invalid for a user-mode process,
    // it will trigger a page fault (#PF) exception on every iteration.
    // The Sentient page fault handler will catch this, increment a counter,
    // and return, causing the instruction to be tried again at extremely
    // high speed. Quickly, the exception rate will exceed the threshold
    // set in sentient.h, and the profiler will trigger apoptosis on this task.
    while (1) {
        *((volatile int*)0) = 1;
    }

    // We should never reach this point, as the task will be terminated
    // by the Sentient system before it can complete.
    mira_print("Mira Shell: Sentient Test Completed (this should not happen!)\n", 0);

    while (1) {}; // Halt the shell task if it somehow escapes termination
}