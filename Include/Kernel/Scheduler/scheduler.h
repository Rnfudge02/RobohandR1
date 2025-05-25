/**
* @file scheduler.h
* @brief Multi-core cooperative/preemptive scheduler for Raspberry Pi Pico 2W.
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [2025-05-20]
* @version 1.0
* 
* This scheduler provides both cooperative and preemptive multitasking with 
* dual-core support, priority-based scheduling, and proper synchronization 
* between cores. It supports both one-shot and persistent tasks.
* 
* @section features Features.
* - Dual-core support. (RP2040/RP2350)
* - Priority-based scheduling. (5 levels)
* - Task types: one-shot and persistent.
* - Core affinity settings.
* - Thread-safe operations.
* - Runtime statistics.
* 
* @section usage Basic Usage.
* @code
* // Initialize scheduler. NOSONAR - Code
* scheduler_init();
* 
* // Create a persistent task. (runs forever) NOSONAR - Code
* scheduler_create_task(my_task, NULL, 0, TASK_PRIORITY_NORMAL, 
*                      "mytask", 0, TASK_TYPE_PERSISTENT);
* 
* // Start scheduler. NOSONAR - Code
* scheduler_start();
* 
* // Main loop. NOSONAR - Code
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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup scheduler_constant Scheduler Configuration Constants
 * @{
 */

/** Maximum number of tasks per core. */
#define MAX_TASKS 16

/** Default stack size per task. (in 32-bit words) */
#define STACK_SIZE 2048

/** Maximum task name length including null terminator. */
#define TASK_NAME_LEN 16

/** @} */ // end of scheduler_constant group

/**
 * @typedef task_func_t
 * @brief Task function prototype.
 * 
 * All task functions must conform to this signature.
 * 
 * @param params User-defined parameters passed to the task.
 */
typedef void (*task_func_t)(void *params);

/**
 * @defgroup scheduler_enum Scheduler Enumerations
 * @{
 */

/**
 * @enum deadline_type_t
 * @brief Task deadline types.
 * 
 * Defines the type of deadline for a task.
 */
typedef enum {
    DEADLINE_NONE = 0,    /**< No deadline requirements. */
    DEADLINE_SOFT,        /**< Soft deadline. (best effort) */
    DEADLINE_HARD         /**< Hard deadline. (critical) */
} deadline_type_t;

/**
 * @enum task_priority_t
 * @brief Task priority levels.
 * 
 * Higher priority tasks preempt lower priority tasks.
 * Tasks of equal priority are scheduled round-robin.
 */
typedef enum {
    TASK_PRIORITY_IDLE = 0,   /**< Lowest priority - runs when system idle. */
    TASK_PRIORITY_LOW,        /**< Low priority background tasks. */
    TASK_PRIORITY_NORMAL,     /**< Default priority for most tasks. */
    TASK_PRIORITY_HIGH,       /**< High priority tasks. (e.g., UI) */
    TASK_PRIORITY_CRITICAL    /**< Highest priority - time critical tasks. */
} task_priority_t;

/**
 * @enum task_state_t
 * @brief Task states in the scheduler lifecycle.
 * 
 * Tasks transition through these states during execution.
 * The scheduler uses these states to determine which tasks to run.
 */
typedef enum {
    TASK_STATE_INACTIVE = 0,  /**< Task slot is empty/unused. */
    TASK_STATE_READY,         /**< Task is ready to be scheduled. */
    TASK_STATE_RUNNING,       /**< Task is currently executing. */
    TASK_STATE_BLOCKED,       /**< Task is waiting for a resource. */
    TASK_STATE_SUSPENDED,     /**< Task is temporarily suspended. */
    TASK_STATE_COMPLETED      /**< Task has finished execution. */
} task_state_t;

/**
 * @enum task_type_t
 * @brief Task execution behavior types.
 * 
 * Determines how the scheduler handles task completion.
 */
typedef enum {
    TASK_TYPE_ONESHOT,        /**< Task runs once then completes. */
    TASK_TYPE_PERSISTENT      /**< Task runs indefinitely. */
} task_type_t;

/** @} */ // end of scheduler_enum

/**
 * @defgroup scheduler_struct Scheduler Data Structures
 * @{
 */

/**
 * @struct core_sync_t
 * @brief Core synchronization structure.
 * 
 * Manages thread-safe communication between CPU cores.
 */
typedef struct {
    unsigned int task_list_lock_num;          /**< Spin lock for task list access. */
    unsigned int scheduler_lock_num;          /**< Spin lock for scheduler state. */
    volatile bool core1_started;      /**< Flag indicating core 1 is running. NOSONAR - Core synchronization */
    volatile bool scheduler_running;  /**< Global scheduler running state. NOSONAR - Core synchronization */
} core_sync_t;

/**
 * @struct deadline_info_t
 * @brief Task deadline information.
 * 
 * Contains information about a task's deadline requirements.
 */
typedef struct {
    uint64_t last_start_time;      /**< Last execution start time. */
    uint64_t last_completion_time; /**< Last execution completion time. */
    uint32_t period_ms;            /**< Task period in milliseconds. */
    uint32_t deadline_ms;          /**< Deadline relative to period start. */
    uint32_t execution_budget_us;  /**< Maximum execution time budget. */
    uint32_t deadline_misses;      /**< Number of deadline misses. */
    deadline_type_t type;          /**< Type of deadline. */
    void (*deadline_miss_handler) (uint32_t task_id); /**< Optional handler for deadline misses. */
} deadline_info_t;

/**
 * @struct scheduler_stats_t
 * @brief Scheduler runtime statistics.
 * 
 * Provides performance metrics and debugging information
 * about scheduler operation.
 */
typedef struct {
    uint64_t total_runtime;           /**< Total scheduler runtime in microseconds. */
    uint32_t context_switches;        /**< Total number of context switches. */
    uint32_t task_creates;            /**< Total tasks created. */
    uint32_t task_deletes;            /**< Total tasks deleted. */
    uint32_t core0_switches;          /**< Context switches on core 0. */
    uint32_t core1_switches;          /**< Context switches on core 1. */
} scheduler_stats_t;

/**
 * @struct task_control_block_t
 * @brief Task Control Block (TCB) with TrustZone support.
 * 
 * Contains all information needed to manage a task including
 * its context, state, scheduling parameters, and statistics.
 */
typedef struct {
    uint64_t total_runtime;           /**< Total execution time in microseconds. */
    uint64_t last_run_time;           /**< Timestamp of last execution. */
    uint32_t* stack_ptr;              /**< Current stack pointer. */
    uint32_t* stack_base;             /**< Base address of task stack. */
    uint32_t stack_size;              /**< Stack size in 32-bit words. */
    uint32_t fault_count;             /**< Number of MPU/secure faults. */
    uint32_t task_id;                 /**< Unique task identifier. */
    uint32_t run_count;               /**< Number of times task has run. */
    task_func_t function;             /**< Task entry point function. */
    task_state_t state;               /**< Current task state. */
    task_priority_t priority;         /**< Task priority level. */
    task_type_t type;                 /**< Task execution type. */
    void *params;                     /**< Parameters passed to task. */
    
    deadline_info_t deadline;         /**< Deadline information. */

    uint8_t core_affinity;            /**< Core assignment. (0, 1, or 0xFF for any) */
    bool deadline_overrun;            /**< Flag indicating deadline overrun. */
    bool mpu_enabled;                 /**< Whether MPU protection is enabled. */
    bool is_secure;                   /**< Whether task runs in secure state. */
    char name[TASK_NAME_LEN];         /**< Task name for debugging. */
    char fault_reason[32];            /**< Last fault reason. */
} task_control_block_t;

/** @} */ // end of scheduler_struct group

/**
 * @defgroup scheduler_api Scheduler API Functions
 * @{
 */

/**
 * @brief Create a new task.
 * 
 * Creates a task with specified parameters and adds it to the scheduler.
 * Tasks are created in READY state and will be scheduled based on priority.
 * 
 * @param function      Task entry point function.
 * @param params        Parameters to pass to the task. (can be NULL)
 * @param stack_size    Stack size in 32-bit words. (0 for default)
 * @param priority      Task priority level.
 * @param name          Task name for debugging. (max 15 chars)
 * @param core_affinity Core to run on. (0, 1, or 0xFF for any core)
 * @param type          Task type. (ONESHOT or PERSISTENT)
 * 
 * @return Task ID on success (>0), -1 on failure.
 * 
 * @pre Scheduler must be initialized.
 * @post Task is created and ready to be scheduled.
 * 
 * @code
 * int task_id = scheduler_create_task(
 *     sensor_task,              // Function. NOSONAR - Code
 *     &sensor_config,           // Parameters. NOSONAR - Code
 *     1024,                     // Stack size. NOSONAR - Code
 *     TASK_PRIORITY_NORMAL,     // Priority. NOSONAR - Code
 *     "sensor",                 // Name. NOSONAR - Code
 *     1,                        // Run on core 1. NOSONAR - Code
 *     TASK_TYPE_PERSISTENT      // Runs forever. NOSONAR - Code
 * );
 * @endcode
 */
__attribute__((section(".time_critical")))
int scheduler_create_task(task_func_t function, void *params, uint32_t stack_size,
    task_priority_t priority, const char *name, uint8_t core_affinity, task_type_t type);

/**
 * @brief Delete a task.
 * 
 * Removes a task from the scheduler and frees its resources.
 * 
 * @param task_id Task ID to delete.
 * @return true if task deleted successfully, false otherwise.
 * 
 * @warning Cannot delete currently running task.
 */
__attribute__((section(".time_critical")))
bool scheduler_delete_task(int task_id);

/**
 * @brief Delay task execution.
 * 
 * Suspends the current task for specified milliseconds.
 * Other tasks run during the delay period.
 * 
 * @param ms Milliseconds to delay.
 * 
 * @note This is a blocking delay for the calling task only.
 */
__attribute__((section(".time_critical")))
void scheduler_delay(uint32_t ms);

/**
 * @brief Enable/disable scheduler tracing.
 * 
 * Controls verbose debug output from the scheduler.
 * Useful for debugging scheduling issues.
 * 
 * @param enable true to enable tracing, false to disable.
 * 
 * @note Tracing may impact system performance.
 */
__attribute__((section(".time_critical")))
void scheduler_enable_tracing(bool enable);

/**
 * @brief Get current task ID.
 * 
 * Returns the ID of the currently executing task.
 * 
 * @return Current task ID, or -1 if called from non-task context.
 */
__attribute__((section(".time_critical")))
int scheduler_get_current_task(void);

/**
 * @brief Get the current task for a specific core.
 * 
 * @param core Core number (0 or 1).
 * @return Pointer to current task, or NULL if no task running.
 */
__attribute__((section(".time_critical")))
task_control_block_t* scheduler_get_current_task_ptr(uint8_t core);

/**
* @brief Get deadline statistics for a task.
* 
* @param task_id Task ID to query.
* @param info Pointer to deadline_info_t to fill.
* @return true if successful, false otherwise.
*/
__attribute__((section(".time_critical")))
bool scheduler_get_deadline_info(int task_id, deadline_info_t *info);

/**
 * @brief Get scheduler statistics.
 * 
 * Retrieves runtime statistics about scheduler performance.
 * 
 * @param stats Pointer to statistics structure to fill.
 * @return true if statistics retrieved successfully, false otherwise.
 * 
 * @code
 * scheduler_stats_t stats;
 * if (scheduler_get_stats(&stats)) {
 *     printf("Context switches: %lu\n", stats.context_switches);
 * }
 * @endcode
 */
__attribute__((section(".time_critical")))
bool scheduler_get_stats(scheduler_stats_t *stats);

/**
 * @brief Get task information.
 * 
 * Retrieves detailed information about a specific task.
 * 
 * @param task_id Task ID to query.
 * @param tcb     Pointer to TCB structure to fill.
 * @return true if task found and info retrieved, false otherwise.
 */
__attribute__((section(".time_critical")))
bool scheduler_get_task_info(int task_id, task_control_block_t *tcb);

/**
 * @brief Initialize the scheduler.
 * 
 * Sets up scheduler data structures, synchronization objects,
 * and prepares both cores for task execution.
 * 
 * @return true if initialization successful, false otherwise.
 * 
 * @pre Must be called before any other scheduler functions.
 * @post Scheduler is ready to accept tasks but not yet running.
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
 * @brief Resume a suspended task.
 * 
 * Makes a suspended task eligible for scheduling again.
 * 
 * @param task_id Task ID to resume.
 * @return true if task resumed successfully, false otherwise.
 * 
 * @pre Task must be in SUSPENDED state.
 * @post Task returns to READY state.
 */
__attribute__((section(".time_critical")))
bool scheduler_resume_task(int task_id);

/**
 * @brief Run pending tasks on current core.
 * 
 * Executes one iteration of scheduled tasks on the calling core.
 * Must be called regularly from the main loop.
 * 
 * @note This function is non-blocking.
 * 
 * @code
 * while (1) {
 *     scheduler_run_pending_tasks();
 *     // Other main loop activities. NOSONAR - Code
 * }
 * @endcode
 */
__attribute__((section(".time_critical")))
void scheduler_run_pending_tasks(void);

/**
 * @brief Set the current task for a specific core.
 * 
 * @param core Core number (0 or 1).
 * @param task Pointer to task to set as current.
 * @return true if successful, false otherwise.
 */
__attribute__((section(".time_critical")))
bool scheduler_set_current_task_ptr(uint8_t core, task_control_block_t* task);

/**
 * @brief Set deadline parameters for a task.
 * 
 * @param task_id Task ID to configure.
 * @param type Deadline type. (none, soft, hard)
 * @param period_ms Task period in milliseconds.
 * @param deadline_ms Deadline relative to period start. (ms)
 * @param execution_budget_us Maximum execution time budget. (us)
 * @return true if successful, false otherwise.
 */
__attribute__((section(".time_critical")))
bool scheduler_set_deadline(int task_id, deadline_type_t type, 
    uint32_t period_ms, uint32_t deadline_ms,
    uint32_t execution_budget_us);

/**
* @brief Register a deadline miss handler for a task.
* 
* This function is called when a task misses its deadline.
* 
* @param task_id Task ID to configure.
* @param handler Function to call on deadline miss.
* @return true if successful, false otherwise.
*/
__attribute__((section(".time_critical")))
bool scheduler_set_deadline_miss_handler(int task_id, 
    void (*handler)(uint32_t task_id));

/**
 * @brief Set MPU protection for a task.
 * 
 * @param task_id            Task ID.
 * @param stack_start        Starting address.
 * @param stack_size         Stack size.
 * @param code_start         Startings address of code segment.
 * @param code_size          Size of the code segment.
 * 
 * @return 1 on success, 0 on failure.
 * 
 * @pre Scheduler must be initialized and task is created.
 * @post Task is created and ready to be scheduled.
 * 
 */
__attribute__((section(".time_critical")))
bool scheduler_set_mpu_protection(int task_id, void *stack_start, size_t stack_size,
    void *code_start, size_t code_size);

/**
 * @brief Start the scheduler.
 * 
 * Begins task scheduling on both cores. Starts the scheduler timer
 * and launches core 1 execution.
 * 
 * @return true if scheduler started successfully, false otherwise.
 * 
 * @pre scheduler_init() must have been called successfully.
 * @post Scheduler is actively scheduling tasks.
 */
bool scheduler_start(void);

/**
 * @brief Stop the scheduler.
 * 
 * Halts task scheduling on both cores and stops the scheduler timer.
 * Running tasks are interrupted.
 * 
 * @post All task scheduling stops, core 1 is reset.
 * 
 * @warning This function does not gracefully shutdown tasks.
 */
void scheduler_stop(void);

/**
 * @brief Suspend a task.
 * 
 * Temporarily prevents a task from being scheduled.
 * 
 * @param task_id Task ID to suspend.
 * @return true if task suspended successfully, false otherwise.
 * 
 * @post Task will not be scheduled until resumed.
 */
__attribute__((section(".time_critical")))
bool scheduler_suspend_task(int task_id);

/**
 * @brief Yield CPU to other tasks.
 * 
 * Current task voluntarily gives up remaining time slice.
 * Useful for cooperative multitasking.
 * 
 * @post Current task moved to READY, scheduler runs next task.
 * 
 * @code
 * while (1) {
 *     do_work();
 *     scheduler_yield();  // Let other tasks run. NOSONAR - Code
 * }
 * @endcode
 */
__attribute__((section(".time_critical")))
void scheduler_yield(void);

/** @} */ //end of scheduler_api group

/**
 * @defgroup scheduler_cmd Scheduler Commands, can be registered with a shell for UI use.
 * @{
 */

/**
 * @brief Control task deadlines.
 * 
 * Allows for control of the deadline scheduler feature.
 * 
 * Usage: deadline arg1 arg2
 * 
 * @param argc Argument count.
 * @param argv Argument array.
 * @return 0 on success, 1 on error.
 * 
 * @code
 * 
 * @endcode
 */
int cmd_deadline(int argc, char *argv[]);

/**
 * @brief List all tasks command.
 * 
 * Displays a table of all tasks with their current state,
 * priority, core affinity, and execution statistics.
 * 
 * Usage: ps
 * 
 * @param argc Argument count. (unused)
 * @param argv Argument array. (unused)
 * @return Always returns 0
 */
int cmd_ps(int argc, char *argv[]);

/**
 * @brief Scheduler control command.
 * 
 * Controls the scheduler state and displays status information.
 * 
 * Usage: scheduler <start|stop|status>
 * 
 * @param argc Argument count.
 * @param argv Argument array.
 * @return 0 on success, 1 on error.
 * 
 * @code
 * scheduler start    // Start the scheduler. NOSONAR - Code
 * scheduler stop     // Stop the scheduler. NOSONAR - Code
 * scheduler status   // Display scheduler status. NOSONAR - Code
 * @endcode
 */
int cmd_scheduler(int argc, char *argv[]);

/**
 * @brief Show scheduler statistics.
 * 
 * Displays detailed scheduler performance metrics including
 * context switches, runtime, and task counts.
 * 
 * Usage: stats
 * 
 * @param argc Argument count. (unused)
 * @param argv Argument array. (unused)
 * @return 0 on success, 1 on error.
 */
int cmd_stats(int argc, char *argv[]);

/**
 * @brief Task management command.
 * 
 * Creates test tasks with specified parameters.
 * 
 * Usage: task create <n> <priority> <core>
 * 
 * @param argc Argument count.
 * @param argv Argument array.
 * @return 0 on success, 1 on error.
 * 
 * @code
 * task create 1 2 0    // Create task 1, priority 2, core 0. NOSONAR - Code
 * task create 2 3 -1   // Create task 2, priority 3, any core. NOSONAR - Code
 * @endcode
 */
int cmd_task(int argc, char *argv[]);

/**
 * @brief Control scheduler tracing.
 * 
 * Enables or disables verbose debug output from the scheduler.
 * Useful for debugging scheduling issues.
 * 
 * Usage: trace <on|off>
 * 
 * @param argc Argument count.
 * @param argv Argument array.
 * @return 0 on success, 1 on error.
 * 
 * @code
 * trace on     // Enable debug output. NOSONAR - Code
 * trace off    // Disable debug output. NOSONAR - Code
 * @endcode
 */
int cmd_trace(int argc, char *argv[]);

/**
 * @brief Register scheduler commands with the shell.
 * 
 * Registers all scheduler-related commands with the USB shell.
 * Must be called after shell_init() but before entering main loop.
 * 
 * @pre Shell must be initialized.
 * @post All scheduler commands are available in the shell.
 * 
 * @code
 * shell_init();
 * register_scheduler_commands();
 * @endcode
 */
void register_scheduler_commands(void);

/**
 * @brief Test task function for demonstrations.
 * 
 * A sample task that runs for a specified number of iterations,
 * printing progress and demonstrating task switching.
 * 
 * @param params Task number as integer pointer.
 * 
 * @note This task is created by the 'task create' command.
 */
void test_task(void *params);

/** @} */ // end of scheduler_cmd

#ifdef __cplusplus
}
#endif

#endif //SCHEDULER_H