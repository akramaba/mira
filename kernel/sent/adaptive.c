#include "../inc/adaptive.h"
#include "../inc/mem.h"
#include "../inc/util.h"
#include "../inc/pit.h"
#include "../inc/dbg.h"
#include "../inc/scheduler.h"

#define MK_ADAPTIVE_FIXED_POINT_SCALE 1024 // 2^10 for fixed-point calculations
#define MK_ADAPTIVE_MAX_TARGETS MK_TASKS_MAX

// Per-task state for the adaptive controller.
// This structure tracks metrics and action values for each monitored task.
typedef struct {
    mk_task *task;
    uint32_t fault_count_period; // Raw fault count in the current profiler interval.

    // Exponential Moving Averages (EMAs) for the fault rate.
    int64_t ema_short;
    int64_t ema_long;
    int64_t cusum;
    int detect_count;

    // Q-values represent the learned effectiveness of each action.
    int64_t q_values[MK_ADAPTIVE_MAX_ACTIONS];

    // State for the currently applied mitigation.
    uint64_t action_until_ms;
    mk_adaptive_action_t current_action;
    int64_t last_fault_rate;
} mk_target_state_t;

// A simple map for storing task states.
static mk_target_state_t mk_targets[MK_ADAPTIVE_MAX_TARGETS];

// Mira Kernel Adaptive System Get Target State
// Finds or allocates a state tracker for a given task.
static mk_target_state_t *mk_adaptive_get_target_state(mk_task *task) {
    if (!task) return NULL;

    // First, search for an existing entry.
    for (int i = 0; i < MK_ADAPTIVE_MAX_TARGETS; i++) {
        if (mk_targets[i].task == task) {
            return &mk_targets[i];
        }
    }

    // If not found, allocate a new one.
    for (int i = 0; i < MK_ADAPTIVE_MAX_TARGETS; i++) {
        if (mk_targets[i].task == NULL) {
            mk_memset(&mk_targets[i], 0, sizeof(mk_target_state_t));
            mk_targets[i].task = task;
            return &mk_targets[i];
        }
    }

    // Map is full!
    return NULL;
}

// Mira Kernel Adaptive System Initialization
void mk_adaptive_init(void) {
    mk_memset(mk_targets, 0, sizeof(mk_targets));
    mk_dbg_print("Adaptive Controller: Initialized.\n");
}

// Mira Kernel Adaptive System Report Fault
void mk_adaptive_report_fault(mk_task *task) {
    mk_target_state_t *state = mk_adaptive_get_target_state(task);
    if (state) {
        state->fault_count_period++;
    }
}

// Mira Kernel Adaptive System Apply Action
// Translates a chosen adaptive action into a concrete scheduling
//  priority, acting as the "effector" of the adaptive feedback loop.
static void mk_adaptive_apply_action(mk_task *task, mk_adaptive_action_t action) {
    char pid_str[10];
    mk_dbg_itoa(task->id, pid_str);

    switch(action) {
        case MK_ADAPTIVE_ACTION_THROTTLE_LIGHT:
            // The lightest throttle: task will run on 1 of every 2 scheduler ticks.
            mk_dbg_print("Adaptive Action: Applying LIGHT THROTTLE to PID ");
            task->priority = MK_TASK_PRIORITY_LOW;
            break;
        case MK_ADAPTIVE_ACTION_THROTTLE_MEDIUM:
            // A moderate throttle: task will run on 1 of every 4 scheduler ticks.
            mk_dbg_print("Adaptive Action: Applying MEDIUM THROTTLE to PID ");
            task->priority = MK_TASK_PRIORITY_LOWER;
            break;
        case MK_ADAPTIVE_ACTION_THROTTLE_HEAVY:
            // The most aggressive throttle: task runs on 1 of every 8 ticks.
            mk_dbg_print("Adaptive Action: Applying HEAVY THROTTLE to PID ");
            task->priority = MK_TASK_PRIORITY_IDLE;
            break;
        default:
            return; // No action needed for NONE.
    }

    mk_dbg_print(pid_str);
    mk_dbg_print("\n");
}

// Mira Kernel Adaptive System Stop Action
// When an action's epoch ends, restore the task to normal priority.
static void mk_adaptive_stop_action(mk_task *task, mk_adaptive_action_t action) {
    if (task) {
        task->priority = MK_TASK_PRIORITY_NORMAL;
    }
}

// Mira Kernel Adaptive System Profiler Entry
int mk_adaptive_profiler_entry(void) {
    mk_dbg_print("Adaptive Profiler: Online and monitoring system state.\n");
    uint64_t last_run_time = mk_pit_get_tick_count();

    while (1) {
        uint64_t now_ms = mk_pit_get_tick_count();

        // ? This sets the profiler interval by skipping iterations.
        // ? This is to avoid using a sleep function in kernel space.
        if (now_ms < last_run_time + MK_ADAPTIVE_PROFILER_INTERVAL_MS) {
            continue;
        }

        last_run_time = now_ms;

        for (int i = 0; i < MK_ADAPTIVE_MAX_TARGETS; i++) {
            mk_target_state_t *state = &mk_targets[i];
            if (!state->task) continue;
            if (state->task->status != MK_TASKS_RUNNING) continue;

            // Step 1: Calculate current fault rate (faults per second)
            int64_t current_rate = (state->fault_count_period * 1000) / MK_ADAPTIVE_PROFILER_INTERVAL_MS;
            int64_t current_rate_fixed = current_rate * MK_ADAPTIVE_FIXED_POINT_SCALE;

            // Step 2: Evaluate reward if an action epoch just ended.
            if (state->current_action != MK_ADAPTIVE_ACTION_NONE && now_ms >= state->action_until_ms) {
                // Reward = improvement in fault rate. Positive is good.
                int64_t reward_fixed = state->last_fault_rate - current_rate_fixed;

                // Update Q-value using: Q_new = (1-eta)*Q_old + eta*Reward
                int action_idx = state->current_action - 1;
                int64_t old_q = state->q_values[action_idx];
                state->q_values[action_idx] = 
                    (((MK_ADAPTIVE_FIXED_POINT_SCALE - MK_ADAPTIVE_LEARNING_RATE) * old_q) / MK_ADAPTIVE_FIXED_POINT_SCALE) + 
                    ((MK_ADAPTIVE_LEARNING_RATE * reward_fixed) / MK_ADAPTIVE_FIXED_POINT_SCALE);

                mk_adaptive_stop_action(state->task, state->current_action);

                state->current_action = MK_ADAPTIVE_ACTION_NONE;
            }

            // Step 3: Update EMAs and CUSUM detector with the new rate.
            if (state->ema_short == 0) {
                state->ema_short = current_rate_fixed;
                state->ema_long = current_rate_fixed;
            }
            
            state->ema_short = (((MK_ADAPTIVE_ALPHA_SHORT * current_rate_fixed) + ((MK_ADAPTIVE_FIXED_POINT_SCALE - MK_ADAPTIVE_ALPHA_SHORT) * state->ema_short))) / MK_ADAPTIVE_FIXED_POINT_SCALE;
            state->ema_long = (((MK_ADAPTIVE_ALPHA_LONG * current_rate_fixed) + ((MK_ADAPTIVE_FIXED_POINT_SCALE - MK_ADAPTIVE_ALPHA_LONG) * state->ema_long))) / MK_ADAPTIVE_FIXED_POINT_SCALE;

            // Step 4: Detect anomaly if not currently mitigated.
            if (state->current_action == MK_ADAPTIVE_ACTION_NONE) {
                int64_t diff = state->ema_short - state->ema_long;

                if (diff > (MK_ADAPTIVE_DETECT_THRESHOLD * MK_ADAPTIVE_FIXED_POINT_SCALE)) {
                    state->detect_count++;
                } else {
                    state->detect_count = 0;
                }

                if (state->detect_count >= MK_ADAPTIVE_DETECT_K) {
                    // * Anomaly detected! * //

                    state->detect_count = 0; // Reset detector

                    // Select action using epsilon-greedy.
                    mk_adaptive_action_t action_to_apply;

                    // Uses a random number generator to decide exploration vs exploitation.
                    // This is important because it allows the system to try different actions
                    // and learn their effectiveness rather than getting stuck in a suboptimal choice.
                    if ((mk_util_rand() % MK_ADAPTIVE_FIXED_POINT_SCALE) < MK_ADAPTIVE_EPSILON_PROB) {
                        mk_dbg_print("Adaptive Profiler: Exploring new action.\n");
                        
                        // Exploration: Pick a random action
                        action_to_apply = (mk_adaptive_action_t)((mk_util_rand() % MK_ADAPTIVE_MAX_ACTIONS) + 1);
                    } else {
                        mk_dbg_print("Adaptive Profiler: Exploiting best-known action.\n");
                        
                        // Exploitation: Pick the best-known action
                        int64_t max_q = state->q_values[0];
                        int best_action_idx = 0;

                        for (int j = 1; j < MK_ADAPTIVE_MAX_ACTIONS; j++) {
                            if (state->q_values[j] > max_q) {
                                max_q = state->q_values[j];
                                best_action_idx = j;
                            }
                        }

                        action_to_apply = (mk_adaptive_action_t)(best_action_idx + 1);
                    }
                    
                    // Apply the chosen action.
                    mk_adaptive_apply_action(state->task, action_to_apply);
                    state->current_action = action_to_apply;
                    state->action_until_ms = now_ms + MK_ADAPTIVE_EPOCH_MS;
                    state->last_fault_rate = current_rate_fixed;
                }
            }
            
            // Step 5: Decay all Q-values slightly to forget outdated info.
            for (int j = 0; j < MK_ADAPTIVE_MAX_ACTIONS; j++) {
                state->q_values[j] = (state->q_values[j] * (MK_ADAPTIVE_FIXED_POINT_SCALE - MK_ADAPTIVE_Q_DECAY_RATE)) / MK_ADAPTIVE_FIXED_POINT_SCALE;
            }

            // Step 6: Reset period counter for the next interval.
            state->fault_count_period = 0;
        }
    }
    
    return 0; // Should not be reached
}