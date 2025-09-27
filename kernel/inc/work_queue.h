#ifndef MK_WORK_QUEUE_H
#define MK_WORK_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include "../inc/mem.h"

#define MK_WORK_QUEUE_SIZE 32
#define MK_WORK_QUEUE_EMPTY -1

// A simple circular buffer for passing PIDs (as int).
typedef struct {
    int items[MK_WORK_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} mk_work_queue_t;

// Mira Kernel Work Queue Initialization
void mk_work_queue_init(mk_work_queue_t* queue);

// Mira Kernel Work Queue Enqueue
int mk_work_queue_enqueue(mk_work_queue_t* queue, int item);

// Mira Kernel Work Queue Dequeue
int mk_work_queue_dequeue(mk_work_queue_t* queue);

#endif // MK_WORK_QUEUE_H