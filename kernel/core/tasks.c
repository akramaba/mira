#include "../inc/tasks.h" 

static mk_task* mk_tasks[MK_TASKS_MAX] = {0};
static int mk_tasks_count = 0;

mk_task* mk_create_task(unsigned char* shellcode, size_t shellcode_size, const char* name) {
    mk_task* new_task = (mk_task*)mk_malloc(sizeof(mk_task));
    
    if (!new_task) {
        return NULL; // Allocation failed
    }

    // Build the task structure
    new_task->id = mk_tasks_count++;
    new_task->name = name;
    new_task->base = (uintptr_t)mk_malloc(shellcode_size);
    new_task->stack = (uintptr_t)mk_malloc(4096); // 4KB stack
    new_task->stack_ptr = new_task->stack + 4096; // Stack start (grows down)
    new_task->status = MK_TASKS_NOT_RUNNING; // Not running
    new_task->mode = MK_TASKS_USER_MODE; // Default to user mode
    
    // Allocate user stack for user mode tasks
    new_task->user_stack_base = (uintptr_t)mk_malloc(4096); // Allocate user stack
    new_task->user_stack_ptr = new_task->user_stack_base + 4096; // User stack start (grows down)

    // Initialize Sentient-related fields
    new_task->sentient_state.last_exception_tick_ms = 0;
    new_task->sentient_state.exception_burst_count = 0;
    new_task->kernel_locks_held = 0;

    // Initialize priority-related fields
    new_task->priority = MK_TASK_PRIORITY_NORMAL;
    new_task->skip_counter = 0;
    
    if (!new_task->base || !new_task->stack) {
        return NULL; // Allocation failed
    }

    // Copy the shellcode to the allocated memory
    unsigned char* dest = (unsigned char*)new_task->base;
    for (size_t i = 0; i < shellcode_size; ++i) {
        dest[i] = shellcode[i];
    }

    mk_tasks[new_task->id] = new_task;
    
    return new_task;
}

// New function for creating tasks from C functions
mk_task* mk_create_task_from_function(int (*entry_point)(void), const char* name) {
    mk_task* new_task = (mk_task*)mk_malloc(sizeof(mk_task));
    if (!new_task) {
        return NULL; // Allocation failed
    }

    // Build the task structure
    new_task->id = mk_tasks_count++;
    new_task->name = name;
    new_task->base = (uintptr_t)entry_point; // Directly use the function's address
    new_task->stack = (uintptr_t)mk_malloc(4096); // 4KB stack
    new_task->stack_ptr = new_task->stack + 4096; // Stack start (grows down)
    new_task->status = MK_TASKS_NOT_RUNNING;
    new_task->mode = MK_TASKS_USER_MODE;

    // Allocate user stack for user mode tasks
    new_task->user_stack_base = (uintptr_t)mk_malloc(4096); // Allocate user stack
    new_task->user_stack_ptr = new_task->user_stack_base + 4096; // User stack start (grows down)

    // Initialize Sentient-related fields
    new_task->sentient_state.last_exception_tick_ms = 0;
    new_task->sentient_state.exception_burst_count = 0;
    new_task->kernel_locks_held = 0;

    // Initialize priority-related fields
    new_task->priority = MK_TASK_PRIORITY_NORMAL;
    new_task->skip_counter = 0;

    if (!new_task->base || !new_task->stack) {
        return NULL; // Allocation failed
    }
    
    mk_tasks[new_task->id] = new_task;
    return new_task;
}

void mk_execute_task(mk_task* task) {
    if (task && task->base) {
        // By setting the status as running, the PIT scheduler will pick it up
        task->status = MK_TASKS_RUNNING;
    }
}

mk_task** mk_get_tasks() {
    return mk_tasks;
}

int mk_get_task_count() {
    return mk_tasks_count;
}

int mk_get_active_task_count() {
    int count = 0;

    for (int i = 0; i < mk_tasks_count; i++) {
        if (mk_tasks[i] && mk_tasks[i]->status == MK_TASKS_RUNNING) {
            count++;
        }
    }

    return count;
}