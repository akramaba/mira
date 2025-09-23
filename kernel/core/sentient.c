#include "../inc/sentient.h"
#include "../inc/pit.h"
#include "../inc/scheduler.h"
#include "../inc/dbg.h"

// Global counter for exceptions
static volatile uint64_t mk_exception_count = 0;

// Mira Kernel Sentient Page Fault Handler
// This is the core of the sensor (in this case, a nociceptor).
__attribute__((naked)) void mk_sentient_page_fault_handler(void) {
    __asm__ volatile (
        // Kernel safety guard.
        // ? If the faulting code was in the kernel (CS = 0x08), this is a real kernel bug.
        // ? Do not return. Jump to the standard panic handler instead.
        // ? The CS selector is at RSP+16 in the interrupt frame pushed by the CPU.
        "movw 16(%%rsp), %%ax\n\t"
        "cmpw $0x08, %%ax\n\t"
        "je jmp_to_panic\n\t"

        // Increment the global counter.
        // ? Locks are used to ensure atomicity in multi-core systems.
        // ? You'll also find it in mk_sentient_get_and_reset_exception_count.
        // ? Mira is currently single-core, but this is future-proofing.
        "lock incq %0\n\t"

        // Discard the error code pushed by the CPU.
        "add $8, %%rsp\n\t"

        // Re-enable interrupts. This lets the PIT run, which
        // then lets the scheduler and profiler also run.
        "sti\n\t"

        // Return from the interrupt.
        "iretq\n\t"

        : "+m"(mk_exception_count)
        :
        : "memory"
    );

    // TODO: Combine the above asm block with the below to avoid multiple asm blocks.
    __asm__ volatile (
        "jmp_to_panic:\n\t"
        "jmp mk_idt_exception_handler\n\t"
    );
}

// Mira Kernel Sentient Get and Reset Exception Count
// The function reads the current value and, at the same time,
// writes 0 into the variable in a single atomic operation.
// This guarantees Mira never misses an exception for counting.
uint64_t mk_sentient_get_and_reset_exception_count(void) {
    uint64_t value = 0;

    __asm__ volatile (
        // * The 'lock' prefix ensures this operation is indivisible,
        // * even on multi-core systems. The 'xchg' instruction swaps
        // * the register and memory location in one atomic step.
        "lock xchgq %0, %1"
        : "+m"(mk_exception_count), "=r"(value)
        : "1"(value)
        : "memory"
    );

    return value;
}

// Mira Kernel Profiler Entry
int mk_profiler_entry(void) {
    uint64_t last_run_time = 0;

    mk_dbg_print("Mira Profiler: Homeostatic monitor initialized.\n");

    while (1) {
        uint64_t current_time = mk_pit_get_tick_count();

        // Check if enough time has passed to run our analysis
        if (current_time > last_run_time + MK_SENTIENT_PROFILER_INTERVAL_MS) {
            last_run_time = current_time;

            // Step 1: Get the data from our sensor
            uint64_t count = mk_sentient_get_and_reset_exception_count();
            if (count == 0) continue;

            // Step 2: Analyze the data. Calculate rate in exceptions/sec.
            uint64_t rate = (count * 1000) / MK_SENTIENT_PROFILER_INTERVAL_MS;

            // Step 3: Make a diagnosis. Do we have a fever?
            if (rate > MK_SENTIENT_CRITICAL_EXCEPTION_THRESHOLD) {
                mk_dbg_print("Mira Profiler: High exception rate detected. System has a fever!\n");

                // ? The culprit is the last user-mode task that ran.
                // ? We wouldn't want to terminate a kernel task because
                // ? that could destabilize the entirety of Mira.
                mk_task* culprit_task = mk_scheduler_get_last_user_task();

                if (culprit_task) {
                    // Step 4: Trigger apoptosis on the culprit task (termination).
                    mk_sentient_apoptosis(culprit_task);
                } else {
                    mk_dbg_print("Mira Profiler: Could not identify a user-mode culprit. No action taken.\n");
                }
            }
        }
    }

    return 0; // Should never be reached
}

// Mira Kernel Sentient Apoptosis
void mk_sentient_apoptosis(mk_task* task) {
    mk_dbg_print("Mira Apoptosis: Triggered. Initiating controlled termination.\n");

    // We mark the task as NOT_RUNNING. The scheduler will
    // respect this and never try to schedule it again.
    task->status = MK_TASKS_NOT_RUNNING;

    // For this proof-of-concept, we are leaking the task's memory.
    // In a production system, we would want to free the memory.
    mk_dbg_print("Mira Apoptosis: Task neutralized. System stability restored.\n");

    // * The next PIT tick will automatically call the scheduler, which will
    // * now skip this quarantined task. Sentient has done its job.
}