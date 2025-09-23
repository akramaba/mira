#ifndef MK_SENTIENT_H
#define MK_SENTIENT_H

#include "tasks.h"
#include <stdint.h>

// Configuration Constants //

// Mira Kernel Sentient Profiler Interval
// How often the profiler runs, in milliseconds.
// 250ms is a good balance between responsiveness and low overhead.
#define MK_SENTIENT_PROFILER_INTERVAL_MS 250

// Mira Kernel Sentient Critical Exception Threshold
// The "fever" threshold. The number of exceptions per
// second that we consider to be a pathological state.
#define MK_SENTIENT_CRITICAL_EXCEPTION_THRESHOLD 15000

// The Nociceptor (Sensor) //

// Mira Kernel Sentient Page Fault Handler
// The interrupt handler for Page Faults.
// This function replaces the default panic handler. It simply increments a
// global counter and returns, allowing the faulting instruction to re-execute.
void mk_sentient_page_fault_handler(void);

// Mira Kernel Sentient Get and Reset Exception Count
// Atomically reads and resets the global exception counter.
// Called by the profiler to get the number of exceptions in the last interval.
// Returns the number of exceptions that occurred since the last call.
uint64_t mk_sentient_get_and_reset_exception_count(void);

// The Homeostatic Profiler (Brain) //

// Mira Kernel Profiler Entry
// The entry point for the mk_profiler kernel task.
// This function contains the main monitoring loop of the Sentient Kernel.
int mk_profiler_entry(void);

// The Apoptosis Protocol (Immune Response) //

// Mira Kernel Sentient Apoptosis
// Triggers the "immune response" to terminate a pathological task.
void mk_sentient_apoptosis(mk_task* task);

#endif // MK_SENTIENT_H