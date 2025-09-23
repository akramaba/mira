#include "../inc/scheduler.h"
#include "../inc/util.h"
#include "../inc/syscalls.h"
#include "../inc/gdt.h"

static int mk_scheduler_current_task = -1;
static mk_cpu_state_t mk_scheduler_task_contexts[MK_TASKS_MAX]; // Array to hold task contexts
static mk_task* mk_last_user_task_ran = NULL; // For Sentient: Track the latest user-mode task

// Mira Kernel Scheduler Get Next Task
int mk_scheduler_get_next_task() {
    int task_count = mk_get_task_count();
    if (task_count == 0) {
        return -1;
    }

    mk_task** all_tasks = mk_get_tasks();
    int current_id = mk_scheduler_current_task;

    // Start searching from the next task in the list
    for (int i = 0; i < task_count; i++) {
        current_id = (current_id + 1) % task_count;
        // * Now able to not just be a round-robin scheduler, but also skip tasks that are not runnable (ex. stopped by Sentient!)
        if (all_tasks[current_id] && all_tasks[current_id]->status != MK_TASKS_NOT_RUNNING) {
             mk_scheduler_current_task = current_id;
             return mk_scheduler_current_task;
        }
    }

    return -1; // No runnable tasks found
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