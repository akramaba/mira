#ifndef MIRA_H
#define MIRA_H

#include <stdint.h>
#include <stddef.h>

// User-space definition of the mouse state structure.
// ! Must match the kernel's mk_mouse_state_t.
typedef struct {
    int32_t x;
    int32_t y;
    uint8_t left_button;
    uint8_t right_button;
    uint8_t middle_button;
} mira_mouse_state_t;

// Mira User Mode System Calls //

// Mira Print Function
// TODO: Support more attributes
void mira_print(const char* string, uint8_t attribute) {
    __asm__ volatile (
        "mov $1, %%rax\n\t"
        "mov %0, %%rdi\n\t"
        "mov %1, %%rsi\n\t"
        "int $0x80"
        : : "r"(string), "r"((uint64_t)attribute) : "rax","rdi","rsi"
    );
}

// Mira Get Keyboard Key Function
char mira_get_key(void) {
    uint64_t key = 0;

    __asm__ volatile (
        "mov $2, %%rax\n\t"
        "int $0x80\n\t"
        : "=a"(key) ::
          "rcx","rsi","rdi","rdx","r8","r9","r10","r11");
          
    return (char)key;
}

// Mira Get Mouse State Function
// Takes a pointer to a mira_mouse_state_t struct to be filled by the kernel.
static inline void mira_get_mouse_state(mira_mouse_state_t* state_ptr) {
    __asm__ volatile (
        "mov $3, %%rax\n\t"  // Syscall number for getting mouse state
        "mov %0, %%rdi\n\t"  // Pass the pointer as the first argument
        "int $0x80"
        : : "r"(state_ptr) : "rax", "rdi"
    );
}

// Mira Create Window Function
static inline int mira_create_window(int x, int y, int width, int height) {
    uint64_t ret = 0;

    // Arguments match the same order as this function's parameters
    __asm__ volatile (
        "mov $4, %%rax\n\t"
        "mov %1, %%rdi\n\t"
        "mov %2, %%rsi\n\t"
        "mov %3, %%rdx\n\t"
        "mov %4, %%rcx\n\t"
        "int $0x80\n\t"
        : "=a"(ret)
        : "r"((uint64_t)x), "r"((uint64_t)y), "r"((uint64_t)width), "r"((uint64_t)height)
        : "rdi","rsi","rdx","rcx","memory" // ? Clobbers needed because with multiple parameters, compiler may
                                           // ? otherwise put values in these registers, breaking the manual movs.
    );

    return ret;
}

// Mira Update Window Function
static inline int mira_update_window(int window_id, uint32_t* framebuffer) {
    uint64_t ret = 0;

    // * In this snippet, we explicitly set the registers for the syscall,
    // * where the window ID goes to rdi (D) and the framebuffer goes to rsi (S).
    // * This prevents the compiler from using any other registers for these values.
    // * Note that mira_create_window doesn't have any issues despite not explicitly
    // * setting registers because there are more parameters; the compiler is forced
    // * to "spill" some operands to other registers or the stack, so the manual movs
    // * actually load the correct values into the syscall registers.
    __asm__ volatile (
        "mov $5, %%rax\n\t"
        "int $0x80\n\t"
        : "=a"(ret)
        : "D"((uint64_t)window_id), "S"((uint64_t)framebuffer)
        : "memory"
    );

    return (int)ret;
}

// Mira Execute Task Function
static inline int mira_execute_task(int (*entry_point)(void), const char* name) {
    uint64_t ret = 0;
    
    __asm__ volatile (
        "mov $6, %%rax\n\t"
        "int $0x80\n\t"
        : "=a"(ret)
        : "D"(entry_point), "S"(name)
        : "memory"
    );

    return (int)ret;
}

// Mira Memory Allocate Function
void* mira_malloc(size_t size) {
    uint64_t ret_ptr;
    
    __asm__ volatile (
        "mov $7, %%rax\n\t"
        "int $0x80\n\t"
        : "=a"(ret_ptr)
        : "D"(size)
        : "memory"
    );

    return (void*)ret_ptr;
}

// Mira Get RDTSC Function
static inline uint64_t mira_rdtsc(void) {
    uint64_t ret;

    __asm__ volatile (
        "mov $8, %%rax\n\t"
        "int $0x80"
        : "=a"(ret)
        :
        : "rcx", "r11", "memory"
    );

    return ret;
}

#endif