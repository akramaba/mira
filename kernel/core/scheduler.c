#include "../inc/scheduler.h"
#include "../inc/util.h"
#include "../inc/syscalls.h"
#include "../inc/gdt.h"

static int mk_scheduler_current_task = -1;
static mk_cpu_state_t mk_scheduler_task_contexts[MK_TASKS_MAX]; // Array to hold task contexts

// Mira Kernel Scheduler Get Next Task
int mk_scheduler_get_next_task() {
    int task_count = mk_get_task_count();
    if (task_count == 0) {
        return -1;
    }

    // Round-robin scheduler where we just cycle through the tasks
    mk_scheduler_current_task = (mk_scheduler_current_task + 1) % task_count;

    return mk_scheduler_current_task;
}

// Mira Kernel Task Scheduler
// This function decides which task to run next. It saves the state of the current
// task and returns a pointer to the state of the next task.
mk_cpu_state_t* mk_schedule(mk_cpu_state_t* regs) {
    // 1. Save registers of the current task
    int old_task = mk_scheduler_current_task;
    if (old_task >= 0) {
        // * Use Mira's memcpy to avoid compiler optimization issues
        mk_memcpy(&mk_scheduler_task_contexts[old_task], regs, sizeof(mk_cpu_state_t));
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