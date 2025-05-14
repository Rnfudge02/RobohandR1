/**
* @file scheduler_fixed.c
* @brief Fixed scheduler with proper task scheduling
*/

#include "scheduler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/time.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

//Scheduler configuration
#define SCHEDULER_TICK_MS         10     //10ms tick

/** Task list for each core */
static task_control_block_t tasks[2][MAX_TASKS];

/** Current running task on each core */
static task_control_block_t *current_task[2] = {NULL, NULL};

/** Next task ID counter */
static volatile uint32_t next_task_id = 1;

/** Core synchronization structure */
static core_sync_t core_sync;

/** Scheduler statistics */
static scheduler_stats_t stats;

/** Scheduler tracing enabled flag */
static volatile bool tracing_enabled = false;

/** Scheduler timer */
struct repeating_timer scheduler_timer;

//Forward declarations
static bool scheduler_timer_callback(struct repeating_timer *t);
static void run_task(task_control_block_t *task);

/**
 * @brief Test task function
 */
__attribute__((aligned(32)))
void test_task(void *params) {
    int task_num = (int)(intptr_t)params;
    static int iteration[10] = {0};  //Track iterations per task
    
    //Run for one iteration each time called
    printf("\n[Task %d] Running iteration %d on core %d\n", 
           task_num, iteration[task_num]++, get_core_num());
    
    //Simulate work
    sleep_ms(500);
    
    //Tasks complete after 5 iterations
    if (iteration[task_num] >= 5) {
        printf("[Task %d] Completed!\n", task_num);
        //Task will be marked as COMPLETED by the scheduler
    }
}

/**
 * @brief Get next ready task for a core
 * @note This function should be placed in RAM
 */
__attribute__((section(".time_critical")))
task_control_block_t* scheduler_get_next_task(uint8_t core) {
    static uint8_t last_scheduled_index[2] = {0, 0};
    task_control_block_t *next_task = NULL;
    int highest_priority = -1;
    
    //First pass: find the highest priority level with ready tasks
    for (int i = 0; i < MAX_TASKS; i++) {
        task_control_block_t *task = &tasks[core][i];
        
        if (task->state == TASK_STATE_READY &&
            task->priority > highest_priority &&
            (task->core_affinity == core || task->core_affinity == 0xFF)) {
            
            highest_priority = task->priority;
        }
    }
    
    //Second pass: round-robin within the highest priority level
    if (highest_priority >= 0) {
        int start_index = (last_scheduled_index[core] + 1) % MAX_TASKS;
        int i = start_index;
        
        do {
            task_control_block_t *task = &tasks[core][i];
            
            if (task->state == TASK_STATE_READY &&
                task->priority == highest_priority &&
                (task->core_affinity == core || task->core_affinity == 0xFF)) {
                
                next_task = task;
                last_scheduled_index[core] = i;
                break;
            }
            
            i = (i + 1) % MAX_TASKS;
        } while (i != start_index);
    }
    
    return next_task;
}

/**
 * @brief Run a task
 * @note This function should be placed in RAM
 */
__attribute__((section(".time_critical")))
static void run_task(task_control_block_t *task) {
    if (task && task->function) {
        task->state = TASK_STATE_RUNNING;
        task->run_count++;
        
        if (tracing_enabled) {
            printf("Running task %s on core %d\n", task->name, get_core_num());
        }
        
        task->function(task->params);
        
        //Task completed
        uint8_t core = get_core_num();
        task->state = TASK_STATE_COMPLETED;
        current_task[core] = NULL;
        
        if (tracing_enabled) {
            printf("Task %s completed\n", task->name);
        }
    }
}

/**
 * @brief Timer callback for scheduler tick
 * 
 * Called periodically to perform task scheduling decisions.
 * Handles context switching and task state updates.
 * 
 * @param t Pointer to repeating timer structure
 * @return true to continue timer, false to stop
 * @note This function should be placed in RAM
 */
__attribute__((section(".time_critical")))
static bool scheduler_timer_callback(struct repeating_timer *t) {
    static uint64_t tick_count = 0;
    (void)t;
    
    tick_count++;
    
    if (!core_sync.scheduler_running) {
        return true;
    }
    
    //Only show minimal debug output if tracing is enabled
    if (tracing_enabled && tick_count % 1000 == 0) {
        printf("[Scheduler] Active (tick %llu)\n", tick_count);
    }
    
    //Schedule tasks for both cores, schedules both cores in the same loop
    for (uint8_t core = 0; core < 2; core++) {
        // Force current running task to READY state to allow switching
        if (current_task[core] && current_task[core]->state == TASK_STATE_RUNNING) {
            // For persistent tasks, move back to READY periodically
            if (current_task[core]->type == TASK_TYPE_PERSISTENT) {
                current_task[core]->state = TASK_STATE_READY;
            }
        }
        
        // Now find the highest priority ready task
        task_control_block_t *next_task = scheduler_get_next_task(core);
        
        if (next_task && next_task != current_task[core]) {
            current_task[core] = next_task;
            stats.context_switches++;
            
            if (core == 0) {
                stats.core0_switches++;
            } else {
                stats.core1_switches++;
            }
            
            if (tracing_enabled) {
                printf("[Scheduler] Core %d: switching to %s\n", 
                       core, next_task->name);
            }
        }
    }
    
    return true;  //Keep timer running
}

/**
 * @brief Core 1 entry point
 */
__attribute__((section(".time_critical")))
void scheduler_core1_entry(void) {
    core_sync.core1_started = true;
    
    printf("Core 1 started\n");
    
    while (1) {
        scheduler_run_pending_tasks();
        tight_loop_contents();
    }
}

//Public API Implementation

__attribute__((aligned(32)))
bool scheduler_init(void) {
    printf("scheduler_init: Starting\n");
    
    //Initialize synchronization objects
    core_sync.task_list_lock_num = spin_lock_claim_unused(true);
    core_sync.scheduler_lock_num = spin_lock_claim_unused(true);
    core_sync.core1_started = false;
    core_sync.scheduler_running = false;
    
    //Clear task lists and stats
    memset(tasks, 0, sizeof(tasks));
    memset(&stats, 0, sizeof(stats));
    
    printf("scheduler_init: Complete\n");
    return true;
}

__attribute__((aligned(32)))
bool scheduler_start(void) {
    if (core_sync.scheduler_running) {
        return false;
    }
    
    printf("scheduler_start: Starting\n");
    
    //Start core 1
    multicore_launch_core1(scheduler_core1_entry);
    
    //Wait for core 1
    while (!core_sync.core1_started) {
        tight_loop_contents();
    }
    
    //Start scheduler timer
    if (!add_repeating_timer_ms(SCHEDULER_TICK_MS, scheduler_timer_callback, NULL, &scheduler_timer)) {
        printf("Failed to start scheduler timer\n");
        return false;
    }
    
    core_sync.scheduler_running = true;
    stats.total_runtime = time_us_64();
    
    printf("scheduler_start: Scheduler running\n");
    return true;
}

__attribute__((aligned(32)))
void scheduler_stop(void) {
    core_sync.scheduler_running = false;
    cancel_repeating_timer(&scheduler_timer);
    multicore_reset_core1();
    printf("Scheduler stopped\n");
}

/**
 * @note This function should be placed in RAM
**/
__attribute__((section(".time_critical")))
int scheduler_create_task(task_func_t function, void *params, uint32_t stack_size,
                         task_priority_t priority, const char *name, uint8_t core_affinity, task_type_t task_type) {
    if (!function || (core_affinity > 1 && core_affinity != 0xFF)) {
        return -1;
    }
    
    uint8_t target_core = (core_affinity == 0xFF) ? 0 : core_affinity;
    
    spin_lock_t *lock = spin_lock_instance(core_sync.task_list_lock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    //Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[target_core][i].state == TASK_STATE_INACTIVE) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        spin_unlock(lock, save);
        return -1;
    }
    
    task_control_block_t *task = &tasks[target_core][slot];
    
    //Initialize task
    task->state = TASK_STATE_READY;
    task->priority = priority;
    task->function = function;
    task->params = params;
    task->core_affinity = core_affinity;
    task->type = task_type;
    task->task_id = next_task_id++;
    task->run_count = 0;
    strncpy(task->name, name, TASK_NAME_LEN - 1);
    task->name[TASK_NAME_LEN - 1] = '\0';
    
    stats.task_creates++;
    
    spin_unlock(lock, save);
    
    if (tracing_enabled) {
        printf("Created task %s (ID:%lu) on core %d\n", task->name, task->task_id, target_core);
    }
    
    return task->task_id;
}

/**
 * @note This function should be placed in RAM
**/
__attribute__((section(".time_critical")))
void scheduler_yield(void) {
    //Force a reschedule on next timer tick
    if (current_task[get_core_num()]) {
        current_task[get_core_num()]->state = TASK_STATE_READY;
    }
}

__attribute__((aligned(32)))
void scheduler_delay(uint32_t ms) {
    sleep_ms(ms);
}

/**
 * @note This function should be placed in RAM
**/
__attribute__((section(".time_critical")))
int scheduler_get_current_task(void) {
    uint8_t core = get_core_num();
    return current_task[core] ? current_task[core]->task_id : -1;
}

/**
 * @note This function should be placed in RAM
**/
__attribute__((section(".time_critical")))
bool scheduler_get_stats(scheduler_stats_t *stats_out) {
    if (!stats_out) return false;
    
    spin_lock_t *lock = spin_lock_instance(core_sync.scheduler_lock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    memcpy(stats_out, &stats, sizeof(scheduler_stats_t));
    
    //Calculate runtime
    if (core_sync.scheduler_running) {
        stats_out->total_runtime = time_us_64() - stats.total_runtime;
    }
    
    spin_unlock(lock, save);
    return true;
}

/**
 * @note This function should be placed in RAM
**/
__attribute__((section(".time_critical")))
bool scheduler_get_task_info(int task_id, task_control_block_t *tcb) {
    if (!tcb || task_id < 0) return false;
    
    spin_lock_t *lock = spin_lock_instance(core_sync.task_list_lock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    bool found = false;
    
    //Search both cores for the task
    for (int core = 0; core < 2 && !found; core++) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[core][i].task_id == (uint32_t)task_id && 
                tasks[core][i].state != TASK_STATE_INACTIVE) {
                memcpy(tcb, &tasks[core][i], sizeof(task_control_block_t));
                found = true;
                break;
            }
        }
    }
    
    spin_unlock(lock, save);
    return found;
}

/**
 * @note This function should be placed in RAM
**/
__attribute__((section(".time_critical")))
void scheduler_run_pending_tasks(void) {
    if (!core_sync.scheduler_running) {
        return;
    }
    
    // First find the current task for this core
    uint8_t core = get_core_num();
    task_control_block_t *task = current_task[core];
    
    // If there's no current task or it's not in READY state, find a new task
    if (!task || task->state != TASK_STATE_READY) {
        task = scheduler_get_next_task(core);
        current_task[core] = task;
    }
    
    // Run the task if we have one
    if (task && task->state == TASK_STATE_READY) {
        // Mark as running
        task->state = TASK_STATE_RUNNING;
        task->run_count++;
        
        // Run the task
        if (task->function) {
            task->function(task->params);
        }
        
        // Handle based on task type
        if (task->type == TASK_TYPE_PERSISTENT) {
            // Persistent tasks go back to READY
            task->state = TASK_STATE_READY;
        } else {
            // One-shot tasks complete
            task->state = TASK_STATE_COMPLETED;
            current_task[core] = NULL;
        }
    }
}

__attribute__((aligned(32)))
void scheduler_enable_tracing(bool enable) {
    tracing_enabled = enable;
    printf("Scheduler tracing %s\n", enable ? "enabled" : "disabled");
}

/**
 * @brief Get the current task for a specific core
 * 
 * @param core Core number (0 or 1)
 * @return Pointer to current task, or NULL if no task running
 */
task_control_block_t* scheduler_get_current_task_ptr(uint8_t core) {
    if (core < 2) {
        return current_task[core];
    }
    return NULL;
}

/**
 * @brief Set the current task for a specific core
 * 
 * @param core Core number (0 or 1)
 * @param task Pointer to task to set as current
 * @return true if successful, false otherwise
 */
bool scheduler_set_current_task_ptr(uint8_t core, task_control_block_t* task) {
    if (core < 2) {
        current_task[core] = task;
        return true;
    }
    return false;
}

// Add to scheduler.c

bool scheduler_set_deadline(int task_id, deadline_type_t type, 
                          uint32_t period_ms, uint32_t deadline_ms,
                          uint32_t execution_budget_us) {
    if (task_id < 0) return false;
    
    // Acquire lock for task list access
    spin_lock_t *lock = spin_lock_instance(core_sync.task_list_lock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    // Find the task
    task_control_block_t *task = NULL;
    bool found = false;
    
    // Search both cores for the task
    for (int core = 0; core < 2 && !found; core++) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[core][i].task_id == (uint32_t)task_id && 
                tasks[core][i].state != TASK_STATE_INACTIVE) {
                task = &tasks[core][i];
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        spin_unlock(lock, save);
        return false;
    }
    
    // Configure deadline parameters
    task->deadline.type = type;
    task->deadline.period_ms = period_ms;
    task->deadline.deadline_ms = deadline_ms;
    task->deadline.execution_budget_us = execution_budget_us;
    task->deadline.deadline_misses = 0;
    task->deadline.last_start_time = 0;
    task->deadline.last_completion_time = 0;
    task->deadline_overrun = false;
    
    spin_unlock(lock, save);
    return true;
}

bool scheduler_set_deadline_miss_handler(int task_id, void (*handler)(uint32_t task_id)) {
    if (task_id < 0) return false;
    
    // Acquire lock for task list access
    spin_lock_t *lock = spin_lock_instance(core_sync.task_list_lock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    // Find the task
    task_control_block_t *task = NULL;
    bool found = false;
    
    // Search both cores for the task
    for (int core = 0; core < 2 && !found; core++) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[core][i].task_id == (uint32_t)task_id && 
                tasks[core][i].state != TASK_STATE_INACTIVE) {
                task = &tasks[core][i];
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        spin_unlock(lock, save);
        return false;
    }
    
    // Set handler
    task->deadline.deadline_miss_handler = handler;
    
    spin_unlock(lock, save);
    return true;
}

bool scheduler_get_deadline_info(int task_id, deadline_info_t *info) {
    if (task_id < 0 || !info) return false;
    
    // Acquire lock for task list access
    spin_lock_t *lock = spin_lock_instance(core_sync.task_list_lock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    // Find the task
    task_control_block_t *task = NULL;
    bool found = false;
    
    // Search both cores for the task
    for (int core = 0; core < 2 && !found; core++) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[core][i].task_id == (uint32_t)task_id && 
                tasks[core][i].state != TASK_STATE_INACTIVE) {
                task = &tasks[core][i];
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        spin_unlock(lock, save);
        return false;
    }
    
    // Copy deadline info
    *info = task->deadline;
    
    spin_unlock(lock, save);
    return true;
}

//Stubs for unimplemented functions
__attribute__((aligned(32)))
bool scheduler_delete_task(int task_id) { (void)task_id; return false; }
__attribute__((aligned(32)))
bool scheduler_suspend_task(int task_id) { (void)task_id; return false; }
__attribute__((aligned(32)))
bool scheduler_resume_task(int task_id) { (void)task_id; return false; }