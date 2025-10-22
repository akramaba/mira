#ifndef TASKS_H
#define TASKS_H

#include "mem.h"

#define MK_TASKS_MAX 32 // Maximum number of tasks

// Task modes
#define MK_TASKS_KERNEL_MODE 0
#define MK_TASKS_USER_MODE 1

// Task statuses
#define MK_TASKS_NOT_RUNNING 0
#define MK_TASKS_RUNNING 1
#define MK_TASKS_ZOMBIE 2 // Quarantined by Sentient
#define MK_TASKS_SLEEPING 3 // Asleep via syscall

// State per task for the emergency fast-path
typedef struct {
    // Timestamp of the last exception for this task, in milliseconds since boot.
    uint64_t last_exception_tick_ms;

    // A running counter of exceptions within the current time window.
    uint32_t exception_burst_count;
} mk_sentient_task_state_t;

// Priority levels for tasks during scheduling
typedef enum {
    MK_TASK_PRIORITY_NORMAL = 0, // Runs every tick
    MK_TASK_PRIORITY_LOW = 55, // Skips 55 ticks, runs 1
    MK_TASK_PRIORITY_LOWER = 89, // Skips 89 ticks, runs 1
    MK_TASK_PRIORITY_IDLE = 144 // Skips 144 ticks, runs 1
} mk_task_priority_t;

// Structure for all Mira tasks
typedef struct _mk_task {
    int id; // Task ID
    const char* name; // Name of the task
    uintptr_t base; // Base address of the task's code
    uintptr_t stack; // Base address of the task's stack
    uintptr_t stack_ptr; // Pointer to the top of the task's stack
    uintptr_t user_stack_base; // Base address for user stack
    uintptr_t user_stack_ptr; // Pointer to the top of the user stack
    int status; // 0 = Not Running, 1 = Running
    int mode; // 0 = Kernel, 1 = User

    mk_sentient_task_state_t sentient_state; // State for the emergency fast-path
    int kernel_locks_held; // For the critical safety interlock
    uint64_t profiler_fault_count; // Task fault count for the profiler

    volatile mk_task_priority_t priority; // The task's current priority level.
    volatile int skip_counter; // Ticks remaining until this task can run.

    uint64_t wakeup_tick; // Tick count when the task should wake up.
} mk_task;

mk_task* mk_create_task(unsigned char* shellcode, size_t shellcode_size, const char* name);

mk_task* mk_create_task_from_function(int (*entry_point)(void), const char* name);

void mk_execute_task(mk_task* task);

mk_task** mk_get_tasks();

int mk_get_task_count();

#endif