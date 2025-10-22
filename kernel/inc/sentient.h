#ifndef MK_SENTIENT_H
#define MK_SENTIENT_H

#include "tasks.h"
#include <stdint.h>

// * Configuration Constants * //

// Mira Kernel Sentient Profiler Interval
// How often the profiler runs, in milliseconds.
// 250ms is a good balance between responsiveness and low overhead.
#define MK_SENTIENT_PROFILER_INTERVAL_MS 250

// Mira Kernel Sentient Critical Exception Threshold
// The "fever" threshold in exceptions per second.
#define MK_SENTIENT_CRITICAL_EXCEPTION_THRESHOLD 15000

// * Emergency Fast-Path Configuration * //

// The time window in milliseconds for detecting a high-frequency burst.
#define MK_SENTIENT_EMERG_WINDOW_MS 10

// The number of exceptions within EMERG_WINDOW_MS that triggers the fast-path.
// 2000 faults / 10 ms = 200,000 faults/sec rate.
#define MK_SENTIENT_EMERG_BURST_THRESHOLD 2000

// Initialize the Sentient system
void mk_sentient_init(void);

// * The Nociceptor (Sensor) * //

// Mira Kernel Sentient Page Fault Handler
// The interrupt handler for Page Faults.
// This function replaces the default panic handler. It simply increments a
// global counter and returns, allowing the faulting instruction to re-execute.
void mk_sentient_page_fault_handler(void);

// * The Homeostatic Profiler (Brain) * //

// Mira Kernel Profiler Entry
// The entry point for the mk_profiler kernel task.
// This function contains the main monitoring loop of the Sentient Kernel.
int mk_profiler_entry(void);

// * The Apoptosis Protocol (Immune Response) * //

// Entry point for the apoptosis worker task.
int mk_apoptosis_worker_entry(void);

// Mira Kernel Sentient Apoptosis
// Triggers the "immune response" to terminate a pathological task.
void mk_sentient_apoptosis(mk_task* task);

// Mira Kernel Sentient Isolate and Park CPU
// Parks the current CPU core in case of an
// unrecoverable fault with kernel lock held.
void mk_sentient_isolate_and_park_cpu(void);

// Mira Kernel Apoptosis Worker Enqueue
// Enqueues a PID for the apoptosis worker to clean up.
void mk_apoptosis_worker_enqueue(int pid);

#endif // MK_SENTIENT_H