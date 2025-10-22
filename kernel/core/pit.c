#include "../inc/pit.h"
#include "../inc/idt.h"
#include "../inc/mouse.h"
#include "../inc/scheduler.h"
#include "../inc/vbe.h"
#include "../inc/syscalls.h"
#include "../inc/dbg.h"

static uint64_t mk_pit_ticks = 0;

// Mira Kernel C-Level PIT Handler
// This function is called from the assembly stub in order
// to return the context of the next task to run.
mk_syscall_registers* mk_pit_c_handler(mk_syscall_registers* regs) {
    // Acknowledge the interrupt
    mk_util_outb(0x20, 0x20);

    // Variable used for timing functions (e.g., sleep)
    // Ideally, the PIT should tick at one millisecond intervals
    mk_pit_ticks++;
    
    // Call the scheduler to get the context of the next task to run
    return mk_schedule(regs);
}

// Mira Kernel PIT Initialization
void mk_pit_init() {
    // Remaps IRQ0 to the PIT
    mk_util_outb(0x20, 0x11);  // Start init on master PIC
    mk_util_outb(0xA0, 0x11);  // Start init on slave PIC
    mk_util_outb(0x21, 0x20);  // Master PIC vector offset = 0x20
    mk_util_outb(0xA1, 0x28);  // Slave PIC vector offset = 0x28
    mk_util_outb(0x21, 0x04);  // Tell master PIC there is a slave at IRQ2
    mk_util_outb(0xA1, 0x02);  // Tell slave PIC its cascade identity
    mk_util_outb(0x21, 0x01);  // 8086/88 (MCS-80/85) mode
    mk_util_outb(0xA1, 0x01);  // 8086/88 (MCS-80/85) mode

    // Set the PIT to the desired frequency
    uint16_t divisor = (uint16_t)(1193180 / MK_PIT_FREQUENCY);

    mk_util_outb(0x43, 0x36); // Square Wave Mode for the PIT 

    // Send the divisor to the PIT (based on the frequency)
    mk_util_outb(0x40, (uint8_t)(divisor & 0xFF));
    mk_util_outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    // Unmask IRQ0 on the PIC to allow PIT interrupts
    uint8_t mask = mk_util_inb(0x21);
    mask |= (1 << 1); // Set bit 1 to disable keyboard interrupts
    mask &= ~(1 << 0); // Clear bit 0 to allow PIT interrupts
    mk_util_outb(0x21, mask);

    // Enable interrupts, the PIT now runs
    __asm__ volatile ("sti");
}

// Mira Kernel PIT Handler
// This naked function is the raw entry point for the interrupt.
// It saves the current task's state, calls the C handler, and then
// restores the state of the next task for the context switch.
__attribute__((naked)) void mk_pit_handler() {
    __asm__ volatile (
        // Save all general-purpose registers
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

        // Call the C handler for getting the next task's context
        "movq %rsp, %rdi\n\t"
        "call mk_pit_c_handler\n\t"

        // We switch stacks by moving this pointer into RSP.
        "movq %rax, %rsp\n\t"

        // Restore all registers from the new stack
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

        // Return from interrupt
        "iretq\n\t"
    );
}

// Mira Kernel Get PIT Tick Count
uint64_t mk_pit_get_tick_count() {
    return mk_pit_ticks;
}