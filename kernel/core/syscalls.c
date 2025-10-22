#include "../inc/syscalls.h"
#include "../inc/keyboard.h"
#include "../inc/mouse.h"
#include "../inc/win.h"
#include "../inc/scheduler.h"
#include "../inc/idt.h"
#include "../inc/dbg.h"
#include "../inc/util.h"
#include "../inc/mem.h"

// Mira System Call Print Function
long mk_syscall_print(mk_syscall_args *args) {
    const char* string = (const char*)args->arg1;
    
    mk_util_print(string);
    mk_dbg_print(string);

    return 0;
}

// Mira System Call Get Key Function
long mk_syscall_get_key(mk_syscall_args *args) {
    // Note that the function is non-blocking and
    // will return immediately if no key is pressed.
    const char* key = mk_keyboard_get_key();

    if (!key) {
        return (long)(uint8_t)'\0'; // No key pressed, return null character
    }

    return (long)(uint8_t)key[0]; // Return the first character of the key name
}

// Mira System Call Get Mouse State Function
// Copies the current mouse state into a user-provided buffer.
long mk_syscall_get_mouse_state(mk_syscall_args *args) {
    // The first argument is a pointer to the user-space struct.
    mk_mouse_state_t* user_state = (mk_mouse_state_t*)args->arg1;
    
    if (!user_state) {
        return -1; // Invalid pointer
    }

    // Get the current, up-to-date state from the mouse driver.
    mk_mouse_state_t* kernel_state = mk_mouse_get_state();

    // Copy the kernel's state into the user's buffer.
    user_state->x = kernel_state->x;
    user_state->y = kernel_state->y;
    user_state->left_button = kernel_state->left_button;
    user_state->right_button = kernel_state->right_button;
    user_state->middle_button = kernel_state->middle_button;

    return 0;
}

// Mira System Call Create Window
long mk_syscall_create_window(mk_syscall_args *args) {
    int x = (int)args->arg1;
    int y = (int)args->arg2;
    int width = (int)args->arg3;
    int height = (int)args->arg4;

    mk_window* window = mk_create_window(x, y, width, height);

    return (long)window->id;
}

// Mira System Call Update Window
long mk_syscall_update_window(mk_syscall_args *args) {
    int window_id = (int)args->arg1;
    uint32_t* framebuffer = (uint32_t*)args->arg2;

    mk_update_window(window_id, framebuffer);

    return 0;
}

// Mira System Call Execute Task
long mk_syscall_execute_task(mk_syscall_args *args) {
    int (*entry_point)(void) = (int (*)(void))args->arg1;
    const char* name = (const char*)args->arg2;

    mk_task* task = mk_create_task_from_function(entry_point, name);
    mk_execute_task(task);

    return (long)task->id;
}

// Mira System Call Allocate Memory
long mk_syscall_malloc(mk_syscall_args *args) {
    size_t size = (size_t)args->arg1;
    void* ptr = mk_malloc(size);
    
    return (long)ptr;
}

// Mira System Call RDTSC (Read Time-Stamp Counter)
long mk_syscall_rdtsc(mk_syscall_args *args) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

// Mira System Call Read Debug Log
long mk_syscall_read_log(mk_syscall_args *args) {
    char* user_buf = (char*)args->arg1;
    size_t user_buf_len = (size_t)args->arg2;
    size_t bytes_copied = 0;

    if (!user_buf || user_buf_len == 0) return 0;

    // Copy from kernel circular buffer to user buffer
    while (bytes_copied < user_buf_len - 1 && mk_dbg_log_head != mk_dbg_log_tail) {
        user_buf[bytes_copied++] = mk_dbg_log_buffer[mk_dbg_log_head];
        mk_dbg_log_head = (mk_dbg_log_head + 1) % MK_DBG_LOG_BUFFER_SIZE;
    }

    user_buf[bytes_copied] = '\0';
    return bytes_copied;
}

// Mira System Call Sleep
long mk_syscall_sleep(mk_syscall_args *args) {
    uint64_t ms_to_sleep = (uint64_t)args->arg1;
    if (ms_to_sleep == 0) return 0;

    mk_task* current_task = mk_scheduler_get_current_task();
    if (current_task) {
        // * The PIT ticks once per millisecond
        current_task->wakeup_tick = mk_pit_get_tick_count() + ms_to_sleep;
        current_task->status = MK_TASKS_SLEEPING;
    }
    
    return 0;
}

// Mira System Call Get System Info
long mk_syscall_get_system_info(mk_syscall_args *args) {
    uint64_t exceptions = mk_idt_total_exceptions;
    uint32_t task_count = (uint32_t)mk_get_task_count();
    
    // * Combine them into a single 64-bit 'long' return value.
    // * Packing: [ Upper 32 bits: task_count | Lower 32 bits: exceptions ]
    long result = ((long)task_count << 32) | (exceptions & 0xFFFFFFFF);

    return result;
}

// Mira System Call Function Definition & Table
typedef long (*mk_syscall_func)(mk_syscall_args *);
const mk_syscall_func syscall_table[] = {
    NULL, // Unused
    mk_syscall_print, // Print to VGA text buffer
    mk_syscall_get_key, // Get key from keyboard
    mk_syscall_get_mouse_state, // Get mouse state
    mk_syscall_create_window, // Create a new window
    mk_syscall_update_window, // Update an existing window
    mk_syscall_execute_task, // Execute a new task
    mk_syscall_malloc, // Allocate memory
    mk_syscall_rdtsc, // Get RDTSC value
    mk_syscall_read_log, // Read debug log
    mk_syscall_sleep, // Sleep for a set time
    mk_syscall_get_system_info // Get system information
};

// Mira System Call Dispatcher
// This function handles the logic for calling the appropriate syscall function
long mk_syscall_dispatch(uint64_t syscall_number, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    long result = -1; // Default to -1 (invalid syscall or not implemented)

    if (syscall_number < (sizeof syscall_table / sizeof syscall_table[0]) && syscall_table[syscall_number]) {
        mk_syscall_args args = { a1, a2, a3, a4, a5, 0 };
        result = syscall_table[syscall_number](&args);
    }

    return result;
}

// Mira System Call Handler
// This function is purely assembly because it needs to preserve
// registers and handle the syscall interrupt properly.
__attribute__((naked)) void mk_syscall_handler(void) {
    __asm__ __volatile__(
        // Save all registers except RAX (return value)
        "pushq %rax\n\t"
        "pushq %rbx\n\t"
        "pushq %rcx\n\t"
        "pushq %rdx\n\t"
        "pushq %rsi\n\t"
        "pushq %rdi\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %r10\n\t"

        // * 8/31/25: Fixed alignments for the last three arguments.
        "mov 64(%rsp), %rdi\n\t"
        "mov 24(%rsp), %rsi\n\t"
        "mov 32(%rsp), %rdx\n\t"
        "mov 40(%rsp), %rcx\n\t"
        "mov 48(%rsp), %r8\n\t"
        "mov 16(%rsp), %r9\n\t"

        "call mk_syscall_dispatch\n\t" // Call the syscall dispatcher

        // Restore everything except RAX (return value)
        "popq %r10\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rdi\n\t"
        "popq %rsi\n\t"
        "popq %rdx\n\t"
        "popq %rcx\n\t"
        "popq %rbx\n\t"
        "addq $8, %rsp\n\t" // No need to pop RAX, it will be used as return value

        "iretq\n\t" // End of syscall handler, return to user mode
    );
}