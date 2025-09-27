#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "syscalls.h"
#include "tasks.h"

#define MK_USER_CODE_SELECTOR 0x1B
#define MK_USER_DATA_SELECTOR 0x23

// Acknowledgment slot for the eviction handshake.
// The scheduler writes a PID here when a zombie task is fully switched out.
extern volatile int mk_eviction_ack_pid;

// * This is the full CPU state saved during an interrupt.
// * The order of the fields must match the order of the registers from the CPU.
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} mk_cpu_state_t;

// Function to step the task scheduler
mk_cpu_state_t* mk_schedule(mk_cpu_state_t* regs);

// Function to get the current task
mk_task* mk_scheduler_get_current_task();

// Function to get the last user-mode task that was executed
mk_task* mk_scheduler_get_last_user_task();

#endif