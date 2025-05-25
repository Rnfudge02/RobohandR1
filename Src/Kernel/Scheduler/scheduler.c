/**
* @file scheduler.c
* @brief Scheduler with proper task scheduling
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "log_manager.h"
#include "scheduler.h"
#include "scheduler_mpu.h"
#include "scheduler_tz.h"
#include "spinlock_manager.h"
#include "usb_shell.h"

#include "pico/time.h"
#include "hardware/timer.h"
#include "pico/multicore.h"

//Scheduler configuration
#define SCHEDULER_TICK_MS         10     //10ms tick

/** Core synchronization structure */
static core_sync_t core_sync;

/** Current running task on each core */
static task_control_block_t *current_task[2] = {NULL, NULL};

/** Next task ID counter */
static volatile uint32_t next_task_id = 1;

//Scheduler command definitions
static const shell_command_t scheduler_commands[] = {
    {cmd_deadline, "deadline", "Configure task deadlines"},
    {cmd_ps, "ps", "List all tasks"},
    {cmd_scheduler, "scheduler", "Control the scheduler (start|stop|status)"},
    {cmd_stats, "stats", "Show scheduler statistics"},
    {cmd_task, "task", "Create a test task (create <n> <priority> <core>)"},
    {cmd_trace, "trace", "Enable/disable scheduler tracing (on|off)"},
    
};

/** Scheduler timer */
struct repeating_timer scheduler_timer;

/** Scheduler statistics */
static scheduler_stats_t stats;

/** Task list for each core */
static task_control_block_t tasks[2][MAX_TASKS];

/** Scheduler tracing enabled flag */
static volatile bool tracing_enabled = false;

void run_handle_deadline(task_control_block_t* task, uint64_t end_time);
static void run_task(task_control_block_t *task);

int scheduler_get_next_deadline(task_control_block_t* task, task_control_block_t** next_task);
void scheduler_get_next_multicore(uint8_t core, task_control_block_t** next_task);

//Forward declarations
static bool scheduler_timer_callback(struct repeating_timer *t);

static int cmd_deadline_info(int argc, char* argv[]);
static int cmd_deadline_set(int argc, char* argv[]);

void run_handle_deadline(task_control_block_t* task, uint64_t end_time) {
    uint64_t period_start = task->deadline.last_start_time - 
        (task->deadline.last_start_time % (task->deadline.period_ms * 1000));

    uint64_t absolute_deadline = period_start + (task->deadline.deadline_ms * 1000);
                
    if (end_time > absolute_deadline) {
        // Deadline missed
        task->deadline.deadline_misses++;
                    
        if (tracing_enabled) {
            log_message(LOG_LEVEL_ERROR, "Scheduler","Task %s missed deadline.", task->name);
        }
                    
            // Handle deadline miss based on type
        if (task->deadline.type == DEADLINE_HARD && task->deadline.deadline_miss_handler) {
            task->deadline.deadline_miss_handler(task->task_id);
        }
    }
}

/**
 * @brief Run a task
 * @note This function should be placed in RAM
 */

static void run_task(task_control_block_t *task) {
    if (task && task->function) {
        uint64_t start_time = time_us_64();
        task->state = TASK_STATE_RUNNING;
        task->run_count++;
        
        // Record start time for deadline tracking
        if (task->deadline.type != DEADLINE_NONE) {
            task->deadline.last_start_time = start_time;
        }
        
        // Execute the task
        task->function(task->params);
        
        // Task completed
        uint64_t end_time = time_us_64();
        uint8_t core = (uint8_t) (get_core_num() & 0xFF);
        task->state = (task->type == TASK_TYPE_ONESHOT) ? TASK_STATE_COMPLETED : TASK_STATE_READY;
        
        // Check execution time against budget
        uint64_t execution_time = end_time - start_time;
        task->total_runtime += execution_time;
        
        // Record completion time
        if (task->deadline.type != DEADLINE_NONE) {
            task->deadline.last_completion_time = end_time;

            bool overrun = false;
            
            // Check for execution budget overrun
            if (task->deadline.execution_budget_us > 0 && 
                execution_time > task->deadline.execution_budget_us) {
                task->deadline_overrun = true;
                
                overrun = true;
            }

            if (overrun && tracing_enabled) {
                log_message(LOG_LEVEL_WARN, "Scheduler", "Task %s exceeded execution budget (%lu us).", 
                    task->name, execution_time);
            }

            if (task->deadline.period_ms > 0 && task->deadline.deadline_ms > 0) {
                // Check for deadline miss
                run_handle_deadline(task, end_time);
            }
            
        }
        
        if (task->type == TASK_TYPE_ONESHOT) {
            current_task[core] = NULL;
        }
        
        if (tracing_enabled) {
            log_message(LOG_LEVEL_DEBUG, "Scheduler", "Task %s completed in %lu us.", task->name, execution_time);
        }
    }
}

/**
 * @brief Core 1 entry point
 */

void scheduler_core1_entry(void) {
    core_sync.core1_started = true;
    
    log_message(LOG_LEVEL_INFO, "Scheduler", "Core 1 started.");
    
    while (1) {
        scheduler_run_pending_tasks();
        tight_loop_contents();
    }
}

/**
 * @note This function should be placed in RAM
**/

int scheduler_create_task(task_func_t function, void *params, uint32_t stack_size,
    task_priority_t priority, const char *name, uint8_t core_affinity, task_type_t task_type) {

    if (!function || (core_affinity > 1 && core_affinity != 0xFF)) {
        return -1;
    }
    
    uint8_t target_core = (core_affinity == 0xFF) ? 0 : core_affinity;
    
    uint32_t save = hw_spinlock_acquire(core_sync.task_list_lock_num, scheduler_get_current_task());
    
    //Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[target_core][i].state == TASK_STATE_INACTIVE) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        hw_spinlock_release(core_sync.task_list_lock_num, save);
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
    
    hw_spinlock_release(core_sync.task_list_lock_num, save);

    if (scheduler_mpu_is_enabled()) {
        // Set up MPU protection for this task
        scheduler_set_mpu_protection(task->task_id, task->stack_base, task->stack_size,
            (void*)function, stack_size);  // Assume 4KB code size for now
    }
    
    if (tracing_enabled) {
        log_message(LOG_LEVEL_INFO, "Scheduler", "Created task %s (ID:%lu) on core %d.", task->name, task->task_id, target_core);
    }
    
    return task->task_id;
}

__attribute__((aligned(32)))
void scheduler_delay(uint32_t ms) {
    sleep_ms(ms);
}

__attribute__((aligned(32)))
bool scheduler_delete_task(int task_id) {
    (void)task_id;
    return false;
}

__attribute__((aligned(32)))
void scheduler_enable_tracing(bool enable) {
    tracing_enabled = enable;
    log_message(LOG_LEVEL_INFO, "Scheduler", "Tracing %s.", enable ? "enabled" : "disabled");
}

/**
 * @note This function should be placed in RAM
**/

int scheduler_get_current_task(void) {
    uint8_t core = (uint8_t) (get_core_num() & 0xFF);
    return current_task[core] ? current_task[core]->task_id : -1;
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

// Implementation of scheduler_get_deadline_info function
bool scheduler_get_deadline_info(int task_id, deadline_info_t *info) {
    if (task_id < 0 || !info) return false;
    
    uint32_t save = hw_spinlock_acquire(core_sync.task_list_lock_num, scheduler_get_current_task());
    
    // Find the task
    task_control_block_t* task = NULL;  // NOSONAR - task is reassigned in the for loop below
    bool found = false;
    
    // Search both cores for the task
    for (int core = 0; core < 2; core++) {
        if (found) {
            break;
        }

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
        hw_spinlock_release(core_sync.task_list_lock_num, save);
        return false;
    }
    
    // Copy deadline info
    *info = task->deadline;
    
    hw_spinlock_release(core_sync.task_list_lock_num, save);
    return true;
}

/**
 * @brief Get next ready task for a core
 * @note This function should be placed in RAM
 */

task_control_block_t* scheduler_get_next_task(uint8_t core) {
    static uint8_t last_scheduled_index[2] = {0, 0};
    task_control_block_t *next_task = NULL;
    int highest_priority = -1;
    
    // Find any task with a hard deadline that needs to run
    for (int i = 0; i < MAX_TASKS; i++) {
        task_control_block_t *task = &tasks[core][i];
        
        if (task->state == TASK_STATE_READY &&
            (task->core_affinity == core || task->core_affinity == 0xFF) &&
            task->deadline.type == DEADLINE_HARD && task->deadline.period_ms > 0 && 
            task->deadline.deadline_ms > 0) {
            
            highest_priority = scheduler_get_next_deadline(task, &next_task);
        }
    }
    
    // If we found a hard deadline task that needs attention, return it
    if (next_task != NULL) {
        return next_task;
    }
    
    // Otherwise, continue with normal priority-based scheduling
    // First pass: find the highest priority level with ready tasks
    for (int i = 0; i < MAX_TASKS; i++) {
        const task_control_block_t *task = &tasks[core][i];
        
        if (task->state == TASK_STATE_READY &&
            task->priority > highest_priority &&
            (task->core_affinity == core || task->core_affinity == 0xFF)) {
            
            highest_priority = task->priority;
        }
    }
    
    // Second pass: round-robin within the highest priority level
    if (highest_priority >= 0) {
        uint8_t start_index = (last_scheduled_index[core] + 1) % MAX_TASKS;
        uint8_t i = start_index;
        
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
    
    // If still no task found in this core's task list, check for any-core tasks 
    // from the other list
    if (!next_task) {
        scheduler_get_next_multicore(core, &next_task);
    }
    
    return next_task;
}

int scheduler_get_next_deadline(task_control_block_t* task, task_control_block_t** next_task) {
    int highest_priority = -1;
    uint64_t current_time = time_us_64();
    uint64_t period_start = 0;
                
    if (task->deadline.last_start_time > 0) {
        period_start = task->deadline.last_start_time - (task->deadline.last_start_time % 
            (task->deadline.period_ms * 1000));
        period_start += task->deadline.period_ms * 1000;  // Next period
    } else {
        // First execution
        period_start = current_time;
    }
                
    uint64_t absolute_deadline = period_start + (task->deadline.deadline_ms * 1000);
                
    // If we're getting close to deadline (within 25% of deadline time),
    // prioritize this task
    uint64_t deadline_margin = task->deadline.deadline_ms * 250;  // 25% in us
            
    // This task is approaching its deadline
    if ((absolute_deadline - current_time <= deadline_margin) && (*next_task == NULL || 
        task->priority > (*next_task)->priority)) {
        *next_task = task;
        highest_priority = task->priority;
    }

    return highest_priority;
}

void scheduler_get_next_multicore(uint8_t core, task_control_block_t** next_task) {
    uint8_t other_core = (core == 0) ? 1 : 0;
        
        for (int i = 0; i < MAX_TASKS; i++) {
            task_control_block_t *task = &tasks[other_core][i];
            
            if ((task->state == TASK_STATE_READY && task->core_affinity == 0xFF) &&
                (*next_task == NULL || task->priority > (*next_task)->priority)) {
                *next_task = task;
            }
        }
}

/**
 * @note This function should be placed in RAM
**/

bool scheduler_get_stats(scheduler_stats_t *stats_out) {
    if (!stats_out) return false;

    uint32_t save = hw_spinlock_acquire(core_sync.scheduler_lock_num, scheduler_get_current_task());
    
    memcpy(stats_out, &stats, sizeof(scheduler_stats_t));
    
    //Calculate runtime
    if (core_sync.scheduler_running) {
        stats_out->total_runtime = time_us_64() - stats.total_runtime;
    }
    
    hw_spinlock_release(core_sync.scheduler_lock_num, save);
    return true;
}

/**
 * @note This function should be placed in RAM
**/

bool scheduler_get_task_info(int task_id, task_control_block_t *tcb) {
    if (!tcb || task_id < 0) return false;
    
    uint32_t save = hw_spinlock_acquire(core_sync.task_list_lock_num, scheduler_get_current_task());
    
    bool found = false;
    
    //Search both cores for the task
    for (int core = 0; core < 2; core++) {
        if (found) {
            break;
        }

        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[core][i].task_id == (uint32_t)task_id && 
                tasks[core][i].state != TASK_STATE_INACTIVE) {
                memcpy(tcb, &tasks[core][i], sizeof(task_control_block_t));
                found = true;
                break;
            }
        }
    }
    
    hw_spinlock_release(core_sync.task_list_lock_num, save);
    return found;
}

__attribute__((aligned(32)))
bool scheduler_init(void) {
    log_message(LOG_LEVEL_INFO, "Scheduler Init","Initializing scheduler.");
    
    //Initialize synchronization objects
    core_sync.task_list_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SCHEDULER, "scheduler_task_list");
    core_sync.scheduler_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SCHEDULER, "scheduler_core");
    core_sync.core1_started = false;
    core_sync.scheduler_running = false;
    
    //Clear task lists and stats
    memset(tasks, 0, sizeof(tasks));
    memset(&stats, 0, sizeof(stats));
    
    log_message(LOG_LEVEL_INFO, "Scheduler Init","Initialized scheduler.");
    return true;
}

__attribute__((aligned(32)))
bool scheduler_resume_task(int task_id) {
    (void)task_id;
    return false;
}

/**
 * @note This function should be placed in RAM
**/

void scheduler_run_pending_tasks(void) {
    if (!core_sync.scheduler_running) {
        return;
    }
    
    // First find the current task for this core
    uint8_t core = (uint8_t) (get_core_num() & 0xFF);
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

bool scheduler_set_deadline(int task_id, deadline_type_t type, 
    uint32_t period_ms, uint32_t deadline_ms, uint32_t execution_budget_us) {
    if (task_id < 0) return false;
    
    // Acquire lock for task list access
    uint32_t save = hw_spinlock_acquire(core_sync.task_list_lock_num, scheduler_get_current_task());
    
    // Find the task
    task_control_block_t *task = NULL;
    bool found = false;
    
    // Search both cores for the task
    for (int core = 0; core < 2; core++) {
        if (found) {
            break;
        }

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
        hw_spinlock_release(core_sync.task_list_lock_num, save);
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
    
    // When a task has a hard deadline, prioritize it further
    if (type == DEADLINE_HARD && task->priority < TASK_PRIORITY_HIGH) {
        // Increase priority for tasks with hard deadlines
        task->priority = TASK_PRIORITY_HIGH;
    }
    
    hw_spinlock_release(core_sync.task_list_lock_num, save);
    return true;
}

// Implementation of scheduler_set_deadline_miss_handler function
bool scheduler_set_deadline_miss_handler(int task_id, void (*handler)(uint32_t task_id)) {
    if (task_id < 0) return false;
    
    // Acquire lock for task list access
    uint32_t save = hw_spinlock_acquire(core_sync.task_list_lock_num, scheduler_get_current_task());
    
    // Find the task
    task_control_block_t *task = NULL;
    bool found = false;
    
    // Search both cores for the task
    for (int core = 0; core < 2; core++) {
        if (found) {
            break;
        }

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
        hw_spinlock_release(core_sync.task_list_lock_num, save);
        return false;
    }
    
    // Set handler
    task->deadline.deadline_miss_handler = handler;
    
    hw_spinlock_release(core_sync.task_list_lock_num, save);
    return true;
}



bool scheduler_set_mpu_protection(int task_id, void *stack_start, size_t stack_size,
    void *code_start, size_t code_size) {

    // Create default MPU configuration
    task_mpu_config_t config;
    if (!scheduler_mpu_create_default_config(task_id, stack_start, stack_size,
    code_start, code_size, &config)) {
        return false;
    }

    // Apply the configuration
    return scheduler_mpu_configure_task(&config);
}

__attribute__((aligned(32)))
bool scheduler_start(void) {
    if (core_sync.scheduler_running) {
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "Scheduler", "Starting.");

    core_sync.scheduler_running = true;
    stats.total_runtime = time_us_64();
    
    //Start core 1
    multicore_launch_core1(scheduler_core1_entry);
    
    //Wait for core 1
    while (!core_sync.core1_started) {
        tight_loop_contents();
    }
    
    //Start scheduler timer
    if (!add_repeating_timer_ms(SCHEDULER_TICK_MS, scheduler_timer_callback, NULL, &scheduler_timer)) {
        log_message(LOG_LEVEL_WARN, "Scheduler", "Failed to start scheduler timer.");
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "Scheduler", "Scheduler running.");
    return true;
}

__attribute__((aligned(32)))
void scheduler_stop(void) {
    core_sync.scheduler_running = false;
    cancel_repeating_timer(&scheduler_timer);
    multicore_reset_core1();
    log_message(LOG_LEVEL_WARN, "Scheduler", "Scheduler stopped.");
}

__attribute__((aligned(32)))
bool scheduler_suspend_task(int task_id) { (void)task_id; return false; }

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

static bool scheduler_timer_callback(struct repeating_timer *t) {
    static uint64_t tick_count = 0;
    (void)t;
    
    tick_count++;
    
    if (!core_sync.scheduler_running) {
        return true;
    }
    
    //Only show minimal debug output if tracing is enabled
    if (tracing_enabled && tick_count % 1000 == 0) {
        log_message(LOG_LEVEL_DEBUG, "Scheduler", "Active (tick %llu).", tick_count);
    }
    
    //Schedule tasks for both cores, schedules both cores in the same loop
    for (uint8_t core = 0; core < 2; core++) {
        // Force current running task to READY state to allow switching
        if ((current_task[core] && current_task[core]->state == TASK_STATE_RUNNING) && (current_task[core]->type == TASK_TYPE_PERSISTENT)) {
            current_task[core]->state = TASK_STATE_READY;
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
                log_message(LOG_LEVEL_DEBUG, "Scheduler","[Scheduler] Core %d: switching to %s.", 
                    core, next_task->name);
            }
        }
    }
    
    return true;  //Keep timer running
}

/**
 * @note This function should be placed in RAM
**/

void scheduler_yield(void) {
    //Force a reschedule on next timer tick
    if (current_task[get_core_num()]) {
        current_task[get_core_num()]->state = TASK_STATE_READY;
    }
}

/**
 * @brief Test task function
 */
__attribute__((aligned(32)))
void test_task(void *params) {
    int task_num = (int)params;
    static int iteration[10] = {0};  //Track iterations per task
    
    //Run for one iteration each time called
    log_message(LOG_LEVEL_INFO, "Test Task", "[Task %d] Running iteration %d on core %d.", 
        task_num, iteration[task_num]++, get_core_num());
    
    //Simulate work
    sleep_ms(500);
    
    //Tasks complete after 5 iterations
    if (iteration[task_num] >= 5) {
        log_message(LOG_LEVEL_INFO, "Test Task", "[Task %d] Completed!", task_num);
        //Task will be marked as COMPLETED by the scheduler
    }
}

int cmd_deadline(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: deadline <set|handler|info> ...\n\r");
        printf("  set <task_id> <type> <period_ms> <deadline_ms> <budget_us>\n\r");
        printf("    type: 0=none, 1=soft, 2=hard\n\r");
        printf("  handler <task_id> <set|clear>\n\r");
        printf("  info <task_id>\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "set") == 0) {
        return cmd_deadline_set(argc, argv);
    }
    else if (strcmp(argv[1], "handler") == 0) {
        if (argc < 4) {
            printf("Usage: deadline handler <task_id> <set|clear>\n\r");
            return 1;
        }
        
        int task_id = atoi(argv[2]);
        
        if (strcmp(argv[3], "set") == 0) {
            // Use a default handler - in a real system, you'd register a callback
            if (scheduler_set_deadline_miss_handler(task_id, NULL)) {
                printf("Default deadline miss handler set for task %d\n\r", task_id);
            } else {
                printf("Failed to set deadline miss handler for task %d\n\r", task_id);
                return 1;
            }
        }
        else if (strcmp(argv[3], "clear") == 0) {
            if (scheduler_set_deadline_miss_handler(task_id, NULL)) {
                printf("Deadline miss handler cleared for task %d\n\r", task_id);
            } else {
                printf("Failed to clear deadline miss handler for task %d\n\r", task_id);
                return 1;
            }
        }
        else {
            printf("Invalid handler command: %s (use 'set' or 'clear')\n\r", argv[3]);
            return 1;
        }
    }
    else if (strcmp(argv[1], "info") == 0) {
        return cmd_deadline_info(argc, argv);
    }
    else {
        printf("Unknown deadline command: %s\n\r", argv[1]);
        return 1;
    }
    
    return 0;
}

static int cmd_deadline_info(int argc, char* argv[]) {
    if (argc < 3) {
            printf("Usage: deadline info <task_id>\n\r");
            return 1;
        }
        
    int task_id = atoi(argv[2]);
    deadline_info_t info;
        
        
    if (scheduler_get_deadline_info(task_id, &info)) {
        char deadline_type[5] = "None";

        if (info.type == DEADLINE_NONE) {
            strncpy(deadline_type, "None", 5);
        } else if (info.type == DEADLINE_SOFT) {
            strncpy(deadline_type, "Soft", 5);
        } else {
            strncpy(deadline_type, "Hard", 5);
        }

        printf("Deadline info for task %d:\n\r", task_id);
        printf("  Type: %s\n\r", deadline_type);
        printf("  Period: %lu ms\n\r", info.period_ms);
        printf("  Deadline: %lu ms\n\r", info.deadline_ms);
        printf("  Execution budget: %lu us\n\r", info.execution_budget_us);
        printf("  Deadline misses: %lu\n\r", info.deadline_misses);
        printf("  Last start time: %llu us\n\r", info.last_start_time);
        printf("  Last completion time: %llu us\n\r", info.last_completion_time);
    } else {
        printf("Failed to get deadline info for task %d\n\r", task_id);
        return 1;
    }

    return 0;
}

static int cmd_deadline_set(int argc, char* argv[]) {
    if (argc < 7) {
        printf("Usage: deadline set <task_id> <type> <period_ms> <deadline_ms> <budget_us>\n\r");
        return 1;
    }
        
    int task_id = atoi(argv[2]);
    int type = atoi(argv[3]);
    uint32_t period_ms = atoi(argv[4]);
    uint32_t deadline_ms = atoi(argv[5]);
    uint32_t budget_us = atoi(argv[6]);
        
    if (type < 0 || type > 2) {
        printf("Invalid type: %d (must be 0-2)\n\r", type);
        return 1;
    }
        
    if (scheduler_set_deadline(task_id, (deadline_type_t)type, period_ms, deadline_ms, budget_us)) {
        printf("Deadline set for task %d: type=%d, period=%lu ms, deadline=%lu ms, budget=%lu us\n\r",
            task_id, type, period_ms, deadline_ms, budget_us);
    } else {
        printf("Failed to set deadline for task %d\n\r", task_id);
        return 1;
    }

    return 0;
}

//List tasks command - fixed version
int cmd_ps(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("Task List:\n\r");
    printf("ID  | Name           | State    | Priority | Core | Run Count\n\r");
    printf("----+----------------+----------+----------+------+----------\n\r");
    
    //Check all possible task IDs (a bit inefficient but works)
    for (int id = 1; id < 100; id++) {
        task_control_block_t tcb;
        if (scheduler_get_task_info(id, &tcb)) {
            const char *state_str;
            switch (tcb.state) {
                case TASK_STATE_INACTIVE:  state_str = "INACTIVE"; break;
                case TASK_STATE_READY:     state_str = "READY"; break;
                case TASK_STATE_RUNNING:   state_str = "RUNNING"; break;
                case TASK_STATE_BLOCKED:   state_str = "BLOCKED"; break;
                case TASK_STATE_SUSPENDED: state_str = "SUSPENDED"; break;
                case TASK_STATE_COMPLETED: state_str = "COMPLETED"; break;  //<-- Added
                default:                   state_str = "UNKNOWN"; break;
            }

            char core_n;

            if (tcb.core_affinity == 0) {
                core_n = '0';
            } else if (tcb.core_affinity == 1) {
                core_n = '1';
            } else {
                core_n = ' ';
            }
            
            printf("%-3lu | %-14s | %-8s | %-8d | %c | %lu\n\r",
                tcb.task_id, tcb.name, state_str,
                tcb.priority, core_n, tcb.run_count);
        }
    }
    
    return 0;
}

//Scheduler control command
int cmd_scheduler(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: scheduler <start|stop|status>\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "start") == 0) {
        printf("Starting scheduler...\n\r");
        if (scheduler_start()) {
            printf("Scheduler started successfully\n\r");
        } else {
            printf("Failed to start scheduler\n\r");
        }
    }

    else if (strcmp(argv[1], "stop") == 0) {
        printf("Stopping scheduler...\n\r");
        scheduler_stop();
        printf("Scheduler stopped\n\r");
    }

    else if (strcmp(argv[1], "status") == 0) {
        scheduler_stats_t tmp_stats;
        if (scheduler_get_stats(&tmp_stats)) {
            printf("Scheduler Status:\n\r");
            //Check if scheduler is actually running by looking at runtime
            bool running = (tmp_stats.total_runtime > 0) || (tmp_stats.context_switches > 0);
            printf("  Running: %s\n\r", running ? "Yes" : "No");
            printf("  Context switches: %lu\n\r", tmp_stats.context_switches);
            printf("  Tasks created: %lu\n\r", tmp_stats.task_creates);
            printf("  Core 0 switches: %lu\n\r", tmp_stats.core0_switches);
            printf("  Core 1 switches: %lu\n\r", tmp_stats.core1_switches);
            if (running) {
                printf("  Runtime: %llu us\n\r", tmp_stats.total_runtime);
            }
        } else {
            printf("Failed to get scheduler status\n\r");
        }
    }

    else {
        printf("Unknown scheduler command: %s\n\r", argv[1]);
        return 1;
    }
    
    return 0;
}

//Show statistics command
int cmd_stats(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    scheduler_stats_t tmp_stats;
    
    if (!scheduler_get_stats(&tmp_stats)) {
        printf("Failed to get scheduler statistics\n\r");
        return 1;
    }

    else {
        printf("Scheduler Statistics:\n\r");
        printf("  Total context switches: %lu\n", tmp_stats.context_switches);
        printf("  Core 0 switches: %lu\n", tmp_stats.core0_switches);
        printf("  Core 1 switches: %lu\n", tmp_stats.core1_switches);
        printf("  Tasks created: %lu\n", tmp_stats.task_creates);
        printf("  Tasks deleted: %lu\n", tmp_stats.task_deletes);
        printf("  Total runtime: %llu us\n", tmp_stats.total_runtime);
    
        return 0;
    }
}

//Trace control command
int cmd_trace(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: trace <on|off>\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "on") == 0) {
        scheduler_enable_tracing(true);
    } else if (strcmp(argv[1], "off") == 0) {
        scheduler_enable_tracing(false);
    } else {
        printf("Invalid option: %s (use 'on' or 'off')\n\r", argv[1]);
        return 1;
    }
    
    return 0;
}

//Task creation command
int cmd_task(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: task create <n> <priority> <core> [type]\n\r");
        printf("  n: task number\n\r");
        printf("  priority: 0-4 (idle-critical)\n\r");
        printf("  core: 0, 1, or -1 (any)\n\r");
        printf("  type: oneshot or persistent (default: oneshot)\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "create") != 0) {
        printf("Unknown task command: %s\n\r", argv[1]);
        return 1;
    }
    
    int task_num = atoi(argv[2]);
    int priority = atoi(argv[3]);
    int core = atoi(argv[4]);
    
    //Validate parameters
    if (priority < 0 || priority > 4) {
        printf("Invalid priority: %d (must be 0-4)\n\r", priority);
        return 1;
    }
    
    if (core < -1 || core > 1) {
        printf("Invalid core: %d (must be 0, 1, or -1)\n\r", core);
        return 1;
    }
    
    //Convert core value
    uint8_t core_affinity = (core == -1) ? 0xFF : (uint8_t)core;
    
    //Create task name
    char task_name[TASK_NAME_LEN];
    snprintf(task_name, sizeof(task_name), "test_%d", task_num);
    
    //Create the task
    task_type_t task_type = TASK_TYPE_ONESHOT;
    if (argc >= 6 && strcmp(argv[5], "persistent") == 0) {
        task_type = TASK_TYPE_PERSISTENT;
    }
    
    //Create the task
    int task_id = scheduler_create_task(test_task, (void *)&task_num,
        0, (task_priority_t)priority, task_name, core_affinity, task_type);

    char core_n;

    if (core == 0) {
        core_n = '0';
    } else if (core == 1) {
        core_n = '1';
    } else {
        core_n = ' ';
    }
    
    printf("Created %s task %s (ID: %d) with priority %d on core %c\n\r", task_type == TASK_TYPE_PERSISTENT ? "persistent" : "oneshot",
        task_name, task_id, priority, core_n);

    return 0;
}

//Register scheduler commands with the shell
void register_scheduler_commands(void) {
    for (int i = 0; i < sizeof(scheduler_commands) / sizeof(scheduler_commands[0]); i++) {
        shell_register_command(&scheduler_commands[i]);
    }
}
