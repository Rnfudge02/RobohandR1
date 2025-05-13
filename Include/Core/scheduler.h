/**
* @file scheduler.h
* @brief Multi-core cooperative/preemptive scheduler for Raspberry Pi Pico 2W
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [Current Date]
* @version 1.0
* 
* This scheduler provides both cooperative and preemptive multitasking with 
* dual-core support, priority-based scheduling, and proper synchronization 
* between cores. It supports both one-shot and persistent tasks.
* 
* @section features Features
* - Dual-core support (RP2040/RP2350)
* - Priority-based scheduling (5 levels)
* - Task types: one-shot and persistent
* - Core affinity settings
* - Thread-safe operations
* - Runtime statistics
* 
* @section usage Basic Usage
* @code
* //Initialize scheduler
* scheduler_init();
* 
* //Create a persistent task (runs forever)
* scheduler_create_task(my_task, NULL, 0, TASK_PRIORITY_NORMAL, 
*                      "mytask", 0, TASK_TYPE_PERSISTENT);
* 
* //Start scheduler
* scheduler_start();
* 
* //Main loop
* while(1) {
*     scheduler_run_pending_tasks();
* }
* @endcode
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

/**
 * @defgroup scheduler_constants Scheduler Configuration Constants
 * @{
 */

/** Maximum number of tasks per core */
#define MAX_TASKS 16

/** Default stack size per task (in 32-bit words) */
#define STACK_SIZE 2048

/** Maximum task name length including null terminator */
#define TASK_NAME_LEN 16

/** @} */

/**
 * @enum task_state_t
 * @brief Task states in the scheduler lifecycle
 * 
 * Tasks transition through these states during execution.
 * The scheduler uses these states to determine which tasks to run.
 */
typedef enum {
    TASK_STATE_INACTIVE = 0,  /**< Task slot is empty/unused */
    TASK_STATE_READY,         /**< Task is ready to be scheduled */
    TASK_STATE_RUNNING,       /**< Task is currently executing */
    TASK_STATE_BLOCKED,       /**< Task is waiting for a resource */
    TASK_STATE_SUSPENDED,     /**< Task is temporarily suspended */
    TASK_STATE_COMPLETED      /**< Task has finished execution */
} task_state_t;

/**
 * @enum task_priority_t
 * @brief Task priority levels
 * 
 * Higher priority tasks preempt lower priority tasks.
 * Tasks of equal priority are scheduled round-robin.
 */
typedef enum {
    TASK_PRIORITY_IDLE = 0,   /**< Lowest priority - runs when system idle */
    TASK_PRIORITY_LOW,        /**< Low priority background tasks */
    TASK_PRIORITY_NORMAL,     /**< Default priority for most tasks */
    TASK_PRIORITY_HIGH,       /**< High priority tasks (e.g., UI) */
    TASK_PRIORITY_CRITICAL    /**< Highest priority - time critical tasks */
} task_priority_t;

/**
 * @enum task_type_t
 * @brief Task execution behavior types
 * 
 * Determines how the scheduler handles task completion.
 */
typedef enum {
    TASK_TYPE_ONESHOT,        /**< Task runs once then completes */
    TASK_TYPE_PERSISTENT      /**< Task runs indefinitely */
} task_type_t;

/**
 * @typedef task_func_t
 * @brief Task function prototype
 * 
 * All task functions must conform to this signature.
 * 
 * @param params User-defined parameters passed to the task
 */
typedef void (*task_func_t)(void *params);

/**
 * @struct task_control_block_t
 * @brief Task Control Block (TCB)
 * 
 * Contains all information needed to manage a task including
 * its context, state, scheduling parameters, and statistics.
 */
typedef struct {
    uint32_t *stack_ptr;              /**< Current stack pointer */
    uint32_t *stack_base;             /**< Base address of task stack */
    uint32_t stack_size;              /**< Stack size in 32-bit words */
    task_state_t state;               /**< Current task state */
    task_priority_t priority;         /**< Task priority level */
    task_func_t function;             /**< Task entry point function */
    void *params;                     /**< Parameters passed to task */
    char name[TASK_NAME_LEN];         /**< Task name for debugging */
    uint32_t task_id;                 /**< Unique task identifier */
    uint8_t core_affinity;            /**< Core assignment (0, 1, or 0xFF for any) */
    task_type_t type;                 /**< Task execution type */
    uint32_t run_count;               /**< Number of times task has run */
    uint64_t total_runtime;           /**< Total execution time in microseconds */
    uint64_t last_run_time;           /**< Timestamp of last execution */
} task_control_block_t;

/**
 * @struct scheduler_stats_t
 * @brief Scheduler runtime statistics
 * 
 * Provides performance metrics and debugging information
 * about scheduler operation.
 */
typedef struct {
    uint32_t context_switches;        /**< Total number of context switches */
    uint32_t task_creates;            /**< Total tasks created */
    uint32_t task_deletes;            /**< Total tasks deleted */
    uint64_t total_runtime;           /**< Total scheduler runtime in microseconds */
    uint32_t core0_switches;          /**< Context switches on core 0 */
    uint32_t core1_switches;          /**< Context switches on core 1 */
} scheduler_stats_t;

/**
 * @struct core_sync_t
 * @brief Core synchronization structure
 * 
 * Manages thread-safe communication between CPU cores.
 */
typedef struct {
    uint task_list_lock_num;          /**< Spin lock for task list access */
    uint scheduler_lock_num;          /**< Spin lock for scheduler state */
    volatile bool core1_started;      /**< Flag indicating core 1 is running */
    volatile bool scheduler_running;  /**< Global scheduler running state */
} core_sync_t;

/**
 * @defgroup scheduler_api Scheduler API Functions
 * @{
 */

/**
 * @brief Initialize the scheduler
 * 
 * Sets up scheduler data structures, synchronization objects,
 * and prepares both cores for task execution.
 * 
 * @return true if initialization successful, false otherwise
 * 
 * @pre Must be called before any other scheduler functions
 * @post Scheduler is ready to accept tasks but not yet running
 * 
 * @code
 * if (!scheduler_init()) {
 *     printf("Scheduler initialization failed\n");
 *     return -1;
 * }
 * @endcode
 */
bool scheduler_init(void);

/**
 * @brief Start the scheduler
 * 
 * Begins task scheduling on both cores. Starts the scheduler timer
 * and launches core 1 execution.
 * 
 * @return true if scheduler started successfully, false otherwise
 * 
 * @pre scheduler_init() must have been called successfully
 * @post Scheduler is actively scheduling tasks
 */
bool scheduler_start(void);

/**
 * @brief Stop the scheduler
 * 
 * Halts task scheduling on both cores and stops the scheduler timer.
 * Running tasks are interrupted.
 * 
 * @post All task scheduling stops, core 1 is reset
 * 
 * @warning This function does not gracefully shutdown tasks
 */
void scheduler_stop(void);

/**
 * @brief Create a new task
 * 
 * Creates a task with specified parameters and adds it to the scheduler.
 * Tasks are created in READY state and will be scheduled based on priority.
 * 
 * @param function      Task entry point function
 * @param params        Parameters to pass to the task (can be NULL)
 * @param stack_size    Stack size in 32-bit words (0 for default)
 * @param priority      Task priority level
 * @param name          Task name for debugging (max 15 chars)
 * @param core_affinity Core to run on (0, 1, or 0xFF for any core)
 * @param type          Task type (ONESHOT or PERSISTENT)
 * 
 * @return Task ID on success (>0), -1 on failure
 * 
 * @pre Scheduler must be initialized
 * @post Task is created and ready to be scheduled
 * 
 * @code
 * int task_id = scheduler_create_task(
 *     sensor_task,              //Function
 *     &sensor_config,           //Parameters
 *     1024,                     //Stack size
 *     TASK_PRIORITY_NORMAL,     //Priority
 *     "sensor",                 //Name
 *     1,                        //Run on core 1
 *     TASK_TYPE_PERSISTENT      //Runs forever
 * );
 * @endcode
 */
int scheduler_create_task(task_func_t function, void *params, uint32_t stack_size,
    task_priority_t priority, const char *name, uint8_t core_affinity, task_type_t type);

/**
 * @brief Delete a task
 * 
 * Removes a task from the scheduler and frees its resources.
 * 
 * @param task_id Task ID to delete
 * @return true if task deleted successfully, false otherwise
 * 
 * @warning Cannot delete currently running task
 * @todo Implement this function
 */
bool scheduler_delete_task(int task_id);

/**
 * @brief Suspend a task
 * 
 * Temporarily prevents a task from being scheduled.
 * 
 * @param task_id Task ID to suspend
 * @return true if task suspended successfully, false otherwise
 * 
 * @post Task will not be scheduled until resumed
 * @todo Implement this function
 */
bool scheduler_suspend_task(int task_id);

/**
 * @brief Resume a suspended task
 * 
 * Makes a suspended task eligible for scheduling again.
 * 
 * @param task_id Task ID to resume
 * @return true if task resumed successfully, false otherwise
 * 
 * @pre Task must be in SUSPENDED state
 * @post Task returns to READY state
 * @todo Implement this function
 */
bool scheduler_resume_task(int task_id);

/**
 * @brief Yield CPU to other tasks
 * 
 * Current task voluntarily gives up remaining time slice.
 * Useful for cooperative multitasking.
 * 
 * @post Current task moved to READY, scheduler runs next task
 * 
 * @code
 * while (1) {
 *     do_work();
 *     scheduler_yield();  //Let other tasks run
 * }
 * @endcode
 */
void scheduler_yield(void);

/**
 * @brief Delay task execution
 * 
 * Suspends the current task for specified milliseconds.
 * Other tasks run during the delay period.
 * 
 * @param ms Milliseconds to delay
 * 
 * @note This is a blocking delay for the calling task only
 */
void scheduler_delay(uint32_t ms);

/**
 * @brief Get current task ID
 * 
 * Returns the ID of the currently executing task.
 * 
 * @return Current task ID, or -1 if called from non-task context
 */
int scheduler_get_current_task(void);

/**
 * @brief Get scheduler statistics
 * 
 * Retrieves runtime statistics about scheduler performance.
 * 
 * @param stats Pointer to statistics structure to fill
 * @return true if statistics retrieved successfully, false otherwise
 * 
 * @code
 * scheduler_stats_t stats;
 * if (scheduler_get_stats(&stats)) {
 *     printf("Context switches: %lu\n", stats.context_switches);
 * }
 * @endcode
 */
bool scheduler_get_stats(scheduler_stats_t *stats);

/**
 * @brief Get task information
 * 
 * Retrieves detailed information about a specific task.
 * 
 * @param task_id Task ID to query
 * @param tcb     Pointer to TCB structure to fill
 * @return true if task found and info retrieved, false otherwise
 */
bool scheduler_get_task_info(int task_id, task_control_block_t *tcb);

/**
 * @brief Enable/disable scheduler tracing
 * 
 * Controls verbose debug output from the scheduler.
 * Useful for debugging scheduling issues.
 * 
 * @param enable true to enable tracing, false to disable
 * 
 * @note Tracing may impact system performance
 */
void scheduler_enable_tracing(bool enable);

/**
 * @brief Run pending tasks on current core
 * 
 * Executes one iteration of scheduled tasks on the calling core.
 * Must be called regularly from the main loop.
 * 
 * @note This function is non-blocking
 * 
 * @code
 * while (1) {
 *     scheduler_run_pending_tasks();
 *     //Other main loop activities
 * }
 * @endcode
 */
void scheduler_run_pending_tasks(void);

/** @} */ //end of scheduler_api group

#ifdef __cplusplus
}
#endif

#endif //SCHEDULER_H