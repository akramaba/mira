#include "../inc/mem.h"
#include "../inc/tasks.h"

// * Slab Caches * //
// Mira currently has one slab cache for tasks.

mk_slab_cache_t mk_task_cache;

static mk_slab_cache_t* mk_slab_caches[] = { 
    &mk_task_cache
};

#define MK_SLAB_CACHE_COUNT (sizeof(mk_slab_caches) / sizeof(mk_slab_caches[0]))

// Mira Kernel Slab Allocator Setup
// Carves out a chunk of the heap and links it all up.
static void mk_slab_setup(mk_slab_cache_t* cache, const char* name, size_t size, size_t count) {
    cache->name = name;
    cache->obj_size = size;
    cache->capacity = count;
    cache->pool = mk_malloc(size * count);
    cache->freelist = NULL;

    if (!cache->pool) {
        return;
    }

    // Thread the freelist through each slot. The first
    // pointer-sized bytes of each free slot = next pointer.
    uint8_t* base = (uint8_t*)cache->pool;
    for (size_t i = 0; i < count; i++) {
        void* slot = base + (i * size);
        void* next = NULL;
        if (i + 1 < count) {
            next = base + ((i + 1) * size);
        }

        *(void**)slot = next;
    }

    cache->freelist = cache->pool;
}

// Mira Kernel Slab Allocator Initialize
// Sets up the slab caches Mira uses.
void mk_slab_init(void) {
    mk_slab_setup(&mk_task_cache, "task", sizeof(mk_task), MK_TASKS_MAX);
}

// Mira Kernel Slab Allocator Allocate
// Allocate from a specific cache.
void* mk_slab_alloc(mk_slab_cache_t* cache) {
    if (!cache->freelist) {
        return NULL;
    }

    void* obj = cache->freelist;
    cache->freelist = *(void**)obj;

    return obj;
}

// Mira Kernel Slab Allocator Free
// Free to a specific cache.
void mk_slab_free(mk_slab_cache_t* cache, void* ptr) {
    if (!ptr) {
        return;
    }

    uintptr_t base = (uintptr_t)cache->pool;
    uintptr_t end = base + (cache->obj_size * cache->capacity);
    uintptr_t addr = (uintptr_t)ptr;

    if (addr < base || addr >= end) {
        return;
    }

    *(void**)ptr = cache->freelist;
    cache->freelist = ptr;
}

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
// Frees memory to the slab cache if possible, otherwise does nothing.
void mk_free(void* ptr) {
    if (!ptr) {
        return;
    }

    uintptr_t addr = (uintptr_t)ptr;

    for (size_t i = 0; i < MK_SLAB_CACHE_COUNT; i++) {
        uintptr_t base = (uintptr_t)mk_slab_caches[i]->pool;
        uintptr_t end = base + (mk_slab_caches[i]->obj_size * mk_slab_caches[i]->capacity);

        if (addr >= base && addr < end) {
            mk_slab_free(mk_slab_caches[i], ptr);
            return;
        }
    }

    // With a bump allocator, we don't actually free memory
    // This is because we don't track non-slab allocations
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