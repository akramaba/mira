#include "../inc/work_queue.h"

// Mira Kernel Work Queue Initialization
void mk_work_queue_init(mk_work_queue_t* queue) {
    mk_memset(queue->items, 0, sizeof(queue->items));

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

// Mira Kernel Work Queue Enqueue
// Returns 0 on success, -1 on failure (queue full).
int mk_work_queue_enqueue(mk_work_queue_t* queue, int item) {
    if (queue->count >= MK_WORK_QUEUE_SIZE) {
        return -1; // Queue is full
    }

    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1) % MK_WORK_QUEUE_SIZE;
    queue->count++;

    return 0;
}

// Mira Kernel Work Queue Dequeue
// Returns the item, or MK_WORK_QUEUE_EMPTY if empty.
int mk_work_queue_dequeue(mk_work_queue_t* queue) {
    if (queue->count == 0) {
        return MK_WORK_QUEUE_EMPTY;
    }

    int item = queue->items[queue->head];

    queue->head = (queue->head + 1) % MK_WORK_QUEUE_SIZE;
    queue->count--;
    
    return item;
}