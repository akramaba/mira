#ifndef MK_ADAPTIVE_H
#define MK_ADAPTIVE_H

#include <stdint.h>
#include "tasks.h"

// * Configuration Constants for the Adaptive System
// * These values are defaults and can be tuned, though
// * they have been chosen to work well in the tests.

// The number of mitigation actions available.
#define MK_ADAPTIVE_MAX_ACTIONS 3

// How often the adaptive profiler runs, in milliseconds.
#define MK_ADAPTIVE_PROFILER_INTERVAL_MS 150

// Exploration probability (epsilon) for action selection.
// ? Value is out of FIXED_POINT_SCALE. 102/1024 ~= 10% (0.1)
#define MK_ADAPTIVE_EPSILON_PROB 102

// Learning rate (eta) for updating action values.
// ? Value is out of FIXED_POINT_SCALE. 102/1024 ~= 10% (0.1)
#define MK_ADAPTIVE_LEARNING_RATE 102

// Short-window EMA alpha value (e.g., 0.3).
#define MK_ADAPTIVE_ALPHA_SHORT 300

// Long-window EMA alpha value (e.g., 0.05).
#define MK_ADAPTIVE_ALPHA_LONG 50

// The threshold (EMA_short - EMA_long) to trigger an anomaly.
#define MK_ADAPTIVE_DETECT_THRESHOLD 1500

// The number of consecutive ticks an anomaly must be detected to trigger action.
#define MK_ADAPTIVE_DETECT_K 2

// The duration of a mitigation action epoch in milliseconds.
#define MK_ADAPTIVE_EPOCH_MS 1000

// The rate at which Q-values decay towards zero over time.
#define MK_ADAPTIVE_Q_DECAY_RATE 5

// Mira Kernel Adaptive System Actions
// These actions correspond to throttling levels.
typedef enum {
    MK_ADAPTIVE_ACTION_NONE = 0,
    MK_ADAPTIVE_ACTION_THROTTLE_LIGHT, // Set priority to LOW
    MK_ADAPTIVE_ACTION_THROTTLE_MEDIUM, // Set priority to LOWER
    MK_ADAPTIVE_ACTION_THROTTLE_HEAVY, // Set priority to IDLE
} mk_adaptive_action_t;

// Mira Kernel Adaptive System Initialization
// Initializes the adaptive controller and its internal state.
void mk_adaptive_init(void);

// Mira Kernel Adaptive System Report Fault
// Sensor function called by the page fault handler to report an event.
void mk_adaptive_report_fault(mk_task *task);

// Mira Kernel Adaptive System Profiler Entry
// The main entry point for the kernel task that runs the adaptive logic.
int mk_adaptive_profiler_entry(void);

#endif // MK_ADAPTIVE_H