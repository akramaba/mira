#include "../inc/scheduler.h"
#include "../inc/util.h"
#include "../inc/syscalls.h"
#include "../inc/gdt.h"

static int mk_scheduler_current_task = -1;
static mk_cpu_state_t mk_scheduler_task_contexts[MK_TASKS_MAX]; // Array to hold task contexts
static mk_task* mk_last_user_task_ran = NULL; // For Sentient: Track the latest user-mode task
volatile int mk_eviction_ack_pid = -1; // Eviction handshake acknowledgment (-1 = free)

// Mira Kernel Scheduler Get Next Task
int mk_scheduler_get_next_task() {
    int task_count = mk_get_task_count();
    if (task_count == 0) {
        return -1;
    }

    mk_task** all_tasks = mk_get_tasks();

    // Loop indefinitely until a runnable task is found. This is guaranteed
    // as long as at least one task has a priority allowing it to run.
    for (;;) {
        // Advance to the next task in the round-robin cycle.
        mk_scheduler_current_task = (mk_scheduler_current_task + 1) % task_count;
        mk_task* candidate = all_tasks[mk_scheduler_current_task];

        // Skip invalid tasks or tasks that are not in a runnable state (e.g., zombies).
        if (!candidate || candidate->status != MK_TASKS_RUNNING) {
            continue;
        }

        // * Core Skip Logic * //
        if (candidate->skip_counter > 0) {
            // This task is being throttled. Decrement its counter and skip it for this tick.
            candidate->skip_counter--;
            continue;
        }

        // This task is ready to run. Reset its skip counter based on its priority,
        // which determines how many ticks it must skip *after* this run.
        candidate->skip_counter = candidate->priority;
        
        // Return the index of the chosen task.
        return mk_scheduler_current_task;
    }
}

// Mira Kernel Task Scheduler
// This function decides which task to run next. It saves the state of the current
// task and returns a pointer to the state of the next task.
mk_cpu_state_t* mk_schedule(mk_cpu_state_t* regs) {
    // 1. Save registers of the current task
    int old_task_id = mk_scheduler_current_task;
    if (old_task_id >= 0) {
        // ? Needed for Sentient: Track the last user-mode task that ran
        // ? in case we need to terminate it for too many exceptions.
        mk_task* old_task_ptr = mk_get_tasks()[old_task_id];
        if (old_task_ptr && old_task_ptr->mode == MK_TASKS_USER_MODE) {
            mk_last_user_task_ran = old_task_ptr;
        }

        // ? This is the "handshake" signal. If the task we just switched away
        // ? from was a zombie, we write its PID to the acknowledgment slot.
        if (old_task_ptr && old_task_ptr->status == MK_TASKS_ZOMBIE) {
            mk_eviction_ack_pid = old_task_ptr->id;
        }

        // * Use Mira's memcpy to avoid compiler optimization issues
        mk_memcpy(&mk_scheduler_task_contexts[old_task_id], regs, sizeof(mk_cpu_state_t));
    }

    // 2. Get the next task to run
    int next_task_index = mk_scheduler_get_next_task();
    if (next_task_index < 0) {
        return regs; // It's the only task, keep it running
    }

    mk_task** all_tasks = mk_get_tasks();
    mk_task* next_task = all_tasks[next_task_index];
    
    // 3. If the next task has never run before, its context will be empty.
    // We must initialize its register state for the first time.
    if (mk_scheduler_task_contexts[next_task_index].rip == 0) {
        if (next_task->mode == MK_TASKS_USER_MODE) {
            mk_scheduler_task_contexts[next_task_index].rip = (uintptr_t)next_task->base;
            mk_scheduler_task_contexts[next_task_index].rsp = (uintptr_t)next_task->user_stack_ptr;
            mk_scheduler_task_contexts[next_task_index].cs = MK_USER_CODE_SELECTOR;
            mk_scheduler_task_contexts[next_task_index].ss = MK_USER_DATA_SELECTOR;
            mk_scheduler_task_contexts[next_task_index].rflags = 0x202;
        } else {
            // Similar setup for kernel mode, but using kernel stack pointers and selectors
            mk_scheduler_task_contexts[next_task_index].rip = (uintptr_t)next_task->base;
            mk_scheduler_task_contexts[next_task_index].rsp = (uintptr_t)next_task->stack_ptr;
            mk_scheduler_task_contexts[next_task_index].cs = MK_KERNEL_CODE_SELECTOR;
            mk_scheduler_task_contexts[next_task_index].ss = MK_KERNEL_DATA_SELECTOR;
            mk_scheduler_task_contexts[next_task_index].rflags = 0x202;
        }
    }

    // 4. Install the next task’s kernel stack in the TSS.
    // This is necessary for ring transitions.
    extern mk_tss_t mk_tss;
    mk_tss.rsp0 = (uint64_t)next_task->stack_ptr;

    // 5. Return a pointer to the next task's saved context. The interrupt
    // handler will use this to perform the actual context switch.
    return &mk_scheduler_task_contexts[next_task_index];
}

// Mira Kernel Scheduler Get Current Task
mk_task* mk_scheduler_get_current_task() {
    if (mk_scheduler_current_task < 0) {
        return NULL;
    }

    mk_task** all_tasks = mk_get_tasks();
    return all_tasks[mk_scheduler_current_task];
}

// Mira Kernel Scheduler Get Last User Task
mk_task* mk_scheduler_get_last_user_task() {
    return mk_last_user_task_ran;
}