#include "../inc/idt.h"
#include "../inc/scheduler.h"
#include "../inc/dbg.h"
#include "../inc/vbe.h"
#include "../inc/sentient.h"

#define MK_IDT_ENTRIES 256
mk_idt_entry_t mk_idt[MK_IDT_ENTRIES];
mk_idt_ptr_t mk_idt_ptr;

// Mira Kernel IDT Set Entry
void mk_idt_set_entry(mk_idt_entry_t *entry, uintptr_t handler, uint16_t segment_selector, uint8_t type_attributes) {
    entry->offset_low = handler & 0xFFFF;
    entry->segment_selector = segment_selector;
    entry->ist = 0; // Use IST 0, the default error stack handler
    entry->reserved0 = 0;
    entry->type_attributes = type_attributes;
    entry->offset_middle = (handler >> 16) & 0xFFFF;
    entry->offset_high = (handler >> 32) & 0xFFFFFFFF;
    entry->reserved1 = 0;
}

// Mira Kernel IDT Loader
void mk_idt_load(mk_idt_ptr_t *idt_ptr) {
    __asm__ volatile (
        "lidt %0"
        : : "m" (*idt_ptr)
    );
}

// Mira IDT Post Handler - Restores registers and returns to the caller
// Without this final step, the system will subsequently crash
void mk_idt_post_handler() {
    // Acknowledge the interrupt
    mk_util_outb(0x20, 0x20);

    __asm__ volatile (
        "pop %r15\n"
        "pop %r14\n"
        "pop %r13\n"
        "pop %r12\n"
        "pop %r11\n"
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %rbp\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rbx\n"
        "pop %rax\n"

        // Re-enable interrupts
        "sti\n"

        // Return from the interrupt
        "iretq"
    );
}

// Mira Kernel IDT Default Handler
void mk_idt_default_handler(void) {
    // Simply acknowledge the interrupt and return
    mk_idt_post_handler();
}

// Mira Kernel IDT Exception Handler
void mk_idt_exception_handler(void) {
    // Stop interrupts -> stops the system
    __asm__ volatile ("cli");

    uint32_t color = 0x0000FF;

    // Paint the screen BSOD blue (for now...)
    for (uint32_t y = 0; y < mk_vbe_get_height(); y++) {
        for (uint32_t x = 0; x < mk_vbe_get_width(); x++) {
            mk_vbe_draw_pixel(x, y, color);
        }
    }

    // Print details to the debug console
    mk_dbg_print("Panic!\n");
    mk_dbg_print("Mira has encountered a fatal error and must halt.\n");
    mk_dbg_print("Current Task Name: ");
    mk_task* current_task = mk_scheduler_get_current_task();
    if (current_task) {
        mk_dbg_print(current_task->name);
        mk_dbg_print("\n");
    } else {
        mk_dbg_print("[base kernel]\n");
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

// Initialize the IDT
void mk_idt_init() {
    mk_idt_ptr.limit = sizeof(mk_idt_entry_t) * MK_IDT_ENTRIES - 1;
    mk_idt_ptr.base = (uint64_t)&mk_idt;

    // Vectors 0-31 are for exceptions
    // Set these to the exception handler
    for (int i = 0; i < 32; i++) {
        mk_idt_set_entry(&mk_idt[i], (uintptr_t)mk_idt_exception_handler, MK_CODE_SELECTOR, 0x8E); // Ring 0
    }

    // Vectors 32-255 are for hardware interrupts and syscalls
    // Set these to the default handler
    for (int i = 32; i < MK_IDT_ENTRIES; i++) {
        mk_idt_set_entry(&mk_idt[i], (uintptr_t)mk_idt_default_handler, MK_CODE_SELECTOR, 0x8E); // Ring 0
    }

    // Set the IDT entry for the syscall interrupt (0x80)
    mk_idt_set_entry(&mk_idt[0x80], (uintptr_t)mk_syscall_handler, MK_CODE_SELECTOR, 0xEE); // Ring 3

    // Set the IDT entry for the PIT timer interrupt (0x20)
    mk_idt_set_entry(&mk_idt[0x20], (uintptr_t)mk_pit_handler, MK_CODE_SELECTOR, 0x8E); // Ring 0

    // Set the IDT entry for the Page Fault interrupt (0x0E) for the Sentient system
    // For control builds, the function will handle the page fault without Sentient.
    mk_idt_set_entry(&mk_idt[0x0E], (uintptr_t)mk_sentient_page_fault_handler, MK_CODE_SELECTOR, 0x8E); // Ring 0

    // Load the IDT
    mk_idt_load(&mk_idt_ptr);
}