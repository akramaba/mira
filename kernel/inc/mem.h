#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>

#define MK_HEAP_START 0x910000 // Starts 40KiB after the 24KiB page tables
#define MK_HEAP_SIZE (1024 * 1024 * 128)

// * Slab Allocator * //

typedef struct {
    const char* name;
    size_t obj_size;
    size_t capacity;
    void* pool;
    void* freelist;
} mk_slab_cache_t;

extern mk_slab_cache_t mk_task_cache;

void mk_slab_init(void);

void* mk_slab_alloc(mk_slab_cache_t* cache);

void mk_slab_free(mk_slab_cache_t* cache, void* ptr);

// * Memory * //

void* mk_malloc(size_t size);

void mk_free(void* ptr);

void* mk_memset(void* ptr, int value, size_t num);

void* mk_memcpy(void* dest, const void* src, size_t num);

#endif