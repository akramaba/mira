#include "../inc/sentient.h"
#include "../inc/pit.h"
#include "../inc/scheduler.h"
#include "../inc/idt.h"
#include "../inc/adaptive.h"
#include "../inc/work_queue.h"
#include "../inc/dbg.h"

// * This struct represents the interrupt frame pushed by the CPU for a page fault.
// * It's different from the one we save for scheduled tasks.
typedef struct {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} mk_interrupt_frame_t;

// Apoptosis work queue for deferred cleanup.
static mk_work_queue_t mk_apoptosis_queue;

// Mira Kernel Sentient Initialization
void mk_sentient_init(void) {
    mk_work_queue_init(&mk_apoptosis_queue);
}

// Forward declaration for the C-level handler.
void mk_sentient_page_fault_c_handler(mk_interrupt_frame_t *frame);

// Mira Kernel Sentient Page Fault Handler
// This is the core of the sensor (in this case, a nociceptor).
// It's a naked assembly stub that calls a C handler for the complex logic.
// * Rewritten to be similar to the PIT handler, where
// * unexpected crashes will be solved for Sentient.
__attribute__((naked)) void mk_sentient_page_fault_handler(void) {
    __asm__ volatile (
        // 1. Save all general purpose registers that the C code might clobber.
        "pushq %rax\n\t"
        "pushq %rbx\n\t"
        "pushq %rcx\n\t"
        "pushq %rdx\n\t"
        "pushq %rsi\n\t"
        "pushq %rdi\n\t"
        "pushq %rbp\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %r10\n\t"
        "pushq %r11\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"

        // 2. Pass a pointer to the CPU's interrupt frame to the C handler.
        // The frame is now 120 bytes down the stack
        // due to our 15 8-byte register pushes.
        "movq %rsp, %rdi\n\t"
        "addq $120, %rdi\n\t"
        "call mk_sentient_page_fault_c_handler\n\t"

        // 3. Restore all registers.
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %r11\n\t"
        "popq %r10\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rbp\n\t"
        "popq %rdi\n\t"
        "popq %rsi\n\t"
        "popq %rdx\n\t"
        "popq %rcx\n\t"
        "popq %rbx\n\t"
        "popq %rax\n\t"

        // 4. Pop the error code.
        "add $8, %rsp\n\t"

        // 5. Return from the interrupt.
        "iretq\n\t"
    );
}

// Mira Sentient Page Fault C Handler
// This function implements the multi-layer defense logic for handling page faults.
void mk_sentient_page_fault_c_handler(mk_interrupt_frame_t *frame) {
    mk_idt_total_exceptions++;

    mk_task* current_task = mk_scheduler_get_current_task();

    // Check if it's a zombie. If so, do nothing, as it 
    // is already quarantined and should not run again.
    if (current_task && current_task->status == MK_TASKS_ZOMBIE) {
        return;
    }

    // Safety check: if fault occurs in kernel mode, then we panic.
    // With our testing implementation, we only handle user-mode faults.
    if (!current_task || current_task->mode != MK_TASKS_USER_MODE) {
        mk_dbg_print("KERNEL PANIC: Page fault in kernel context!\n");
       __asm__ volatile("cli; hlt");
    }

    // Get the instruction length for advancing
    size_t instruction_length = mk_util_get_instruction_length((uint8_t*)frame->rip);

#ifndef CONFIG_SENTIENT
    // For a non-Sentient (control) build, we skip all detection logic.
    // We simply advance the instruction pointer and return, which
    // demonstrates the Computational Livelock vulnerability.
    frame->rip += instruction_length;
    return;
#endif

    // Phase 1: Report the fault to the adaptive system profiler.
    // This increments the task's fault counter for adaptive profiling.
    mk_adaptive_report_fault(current_task);

    // Increment the task's fault counter for the profiler,
    // so that it can identify high-fault, pathological tasks.
    if (current_task) {
        current_task->profiler_fault_count++;
    }

    // Phase 2: Emergency Burst Detection Logic
    uint64_t now_ms = mk_pit_get_tick_count(); // Get current time
    mk_sentient_task_state_t* state = &current_task->sentient_state;

    // Check if the current fault is part of an ongoing burst.
    if (now_ms - state->last_exception_tick_ms <= MK_SENTIENT_EMERG_WINDOW_MS) {
        state->exception_burst_count++; // It is, increment the counter.
    } else {
        // It's the first fault in a new window. Reset the counter and timestamp.
        state->exception_burst_count = 1;
        state->last_exception_tick_ms = now_ms;
    }

    // Phase 3: Trigger Immediate Quarantine if Threshold is Exceeded
    if (state->exception_burst_count >= MK_SENTIENT_EMERG_BURST_THRESHOLD) {
        // * Emergency Fast-Path Triggered! * //

        // 3a. Safety Interlock: Check for held kernel locks. This is critical!
        if (current_task->kernel_locks_held > 0) {
            // Escalate: Terminating now could deadlock the kernel.
            // The safest action is to park the core to prevent further damage.
            mk_sentient_isolate_and_park_cpu();
            // This core is now halted and will not return.
        }

        // 3b. Quarantine the task by marking it as a ZOMBIE.
        // The scheduler will see this flag and never run this task again.
        current_task->status = MK_TASKS_ZOMBIE;

        // 3c. Advance RIP to break the pathological loop.
        // using the calculated instruction length.
        frame->rip += instruction_length;

        // 3d. Enqueue the task's PID for the deferred cleanup worker.
        mk_apoptosis_worker_enqueue(current_task->id);

        // 3e. Log the event for analysis and return from the exception.
        mk_dbg_print("Mira Apoptosis: Fast-path quarantine for PID ");
        char pid_str[10];
        mk_dbg_itoa(current_task->id, pid_str);
        mk_dbg_print(pid_str);
        mk_dbg_print("\n");
        
        return; // Return to the assembly stub, which will iretq.
    }

    // Allows for the faulting instruction to still 
    // be re-executed when a threshold is not reached.
    frame->rip += instruction_length;

    // If not a burst, return. The assembly stub will iretq, and the faulting
    // instruction will be re-executed, continuing the cycle.
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
            
            mk_task** all_tasks = mk_get_tasks();
            int task_count = mk_get_task_count();

            // Iterate through all tasks to check their individual fault counts
            for (int i = 0; i < task_count; i++) {
                mk_task* task = all_tasks[i];

                // Skip kernel tasks, non-running tasks, or already-zombied tasks
                if (!task || task->mode != MK_TASKS_USER_MODE || task->status != MK_TASKS_RUNNING) {
                    continue;
                }

                uint64_t count = 0;
                
                // Atomically swap the task's fault counter with our zeroed 'count' variable.
                // The original value is returned in 'count', and the task's counter is reset.
                __asm__ volatile (
                    "lock xchgq %0, %1"
                    : "+m" (task->profiler_fault_count), "=r" (count)
                    : "1" (count)
                    : "memory"
                );

                if (count == 0) continue;

                // Step 2: Analyze the data. Calculate rate in exceptions/sec for this task.
                uint64_t rate = (count * 1000) / MK_SENTIENT_PROFILER_INTERVAL_MS;

                // Step 3: Make a diagnosis. Does this task have a fever?
                if (rate > MK_SENTIENT_CRITICAL_EXCEPTION_THRESHOLD) {
                    char pid_str[10];
                    mk_dbg_itoa(task->id, pid_str);
                    mk_dbg_print("Mira Profiler: High exception rate from PID ");
                    mk_dbg_print(pid_str);
                    mk_dbg_print(". System has a fever!\n");
                    
                    // Step 4: Trigger apoptosis on the actual culprit task.
                    mk_sentient_apoptosis(task);
                }
            }
        }
    }

    return 0; // Should never be reached
}

// Mira Kernel Apoptosis Worker Entry
// This is the entry point for the k_apoptosis_worker kernel thread.
int mk_apoptosis_worker_entry(void) {
    mk_dbg_print("Apoptosis Worker: Initialized and waiting for tasks.\n");

    while (1) {
        // Dequeue the next PID that needs cleanup.
        int pid_to_clean = mk_work_queue_dequeue(&mk_apoptosis_queue);

        if (pid_to_clean != MK_WORK_QUEUE_EMPTY) {
            // This is a critical safety step. It waits for the scheduler to formally
            // acknowledge that the task is no longer scheduled and its state is fully
            // saved. This prevents a race where we deallocate the stack the CPU
            // is currently using to save the task's state. (Eviction Handshake)
            mk_dbg_print("Apoptosis Worker: Waiting for eviction handshake for PID ");
            char pid_str_wait[10];
            mk_dbg_itoa(pid_to_clean, pid_str_wait);
            mk_dbg_print(pid_str_wait);
            mk_dbg_print("...\n");

            while (mk_eviction_ack_pid != pid_to_clean) {
                // Spin-wait for the scheduler's acknowledgment.
            }

            // Acknowledgment received. Reset the slot for the next task.
            mk_eviction_ack_pid = -1;

            // Safe to clean up now.
            mk_dbg_print("Apoptosis Worker: Eviction acknowledged. Beginning cleanup for PID ");
            char pid_str_clean[10];
            mk_dbg_itoa(pid_to_clean, pid_str_clean);
            mk_dbg_print(pid_str_clean);
            mk_dbg_print(".\n");
            
            // In this proof-of-concept, task memory is intentionally leaked to ensure
            // deterministic performance and avoid complexities of a memory free-list.
            // A production system would reclaim the task's memory pages here.
        }
    }

    return 0;
}

// Mira Kernel Sentient Apoptosis
void mk_sentient_apoptosis(mk_task* task) {
    mk_dbg_print("Mira Apoptosis: Triggered. Initiating controlled termination.\n");

    // We mark the task as MK_TASKS_ZOMBIE
    // so the scheduler will skip it.
    task->status = MK_TASKS_ZOMBIE;

    // For this proof-of-concept, we are leaking the task's memory.
    // In a production system, we would want to free the memory.
    mk_dbg_print("Mira Apoptosis: Task neutralized. System stability restored.\n");

    // Print the task's PID for information
    mk_dbg_print("Mira Apoptosis: Quarantined PID ");
    char pid_str[10];
    mk_dbg_itoa(task->id, pid_str);
    mk_dbg_print(pid_str);
    mk_dbg_print("\n");

    // * The next PIT tick will automatically call the scheduler, which will
    // * now skip this quarantined task. Sentient has done its job.
}

// Stub implementation for the escalation protocol.
void mk_sentient_isolate_and_park_cpu(void) {
    mk_dbg_print("CRITICAL: CPU parked due to unrecoverable fault with kernel lock held.\n");
    __asm__ volatile("cli; hlt");
}

// Stub implementation for the apoptosis worker queue.
void mk_apoptosis_worker_enqueue(int pid) {
    if (mk_work_queue_enqueue(&mk_apoptosis_queue, pid) != 0) {
        mk_dbg_print("Apoptosis Worker: WORK QUEUE FULL! Cannot enqueue PID.\n");
    }
}