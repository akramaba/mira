#include "../inc/mem.h"

// Mira Kernel Memory Bump Allocator
void* mk_malloc(size_t size) {
    static uintptr_t current_heap = MK_HEAP_START;

    if (current_heap + size > MK_HEAP_START + MK_HEAP_SIZE) {
        // Out of memory
        return NULL;
    }

    uintptr_t allocated_address = current_heap;
    current_heap += size;
    
    return (void*)allocated_address;
}

// Mira Kernel Memory Free
void mk_free(void* ptr) {
    // With a bump allocator, we don't actually free memory
    // This is because we don't track individual allocations
    // ...for now.
}

// Mira Kernel Memory Set
void* mk_memset(void* ptr, int value, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
    return ptr;
}

// Mira Kernel Memory Copy
void* mk_memcpy(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < num; i++) {
        d[i] = s[i];
    }
    return dest;
}