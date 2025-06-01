/**
 * @file test_scheduler.h
 * @brief Scheduler component test suite
 * @author System Test Framework
 * @date 2025-05-26
 * 
 * This module provides comprehensive testing for the scheduler component,
 * including task management, priority scheduling, multicore support,
 * and deadline scheduling functionality.
 */

#ifndef TEST_SCHEDULER_H
#define TEST_SCHEDULER_H

#include "test_framework.h"
#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

// Test configuration constants
#define SCHEDULER_TEST_MAX_TASKS        16
#define SCHEDULER_TEST_TASK_STACK_SIZE  1024
#define SCHEDULER_TEST_TIMEOUT_MS       2000
#define SCHEDULER_TEST_STRESS_ITERATIONS 100

/**
 * @brief Test task states for tracking
 */
typedef enum {
    TASK_TEST_STATE_CREATED = 0,
    TASK_TEST_STATE_RUNNING,
    TASK_TEST_STATE_COMPLETED,
    TASK_TEST_STATE_FAILED
} task_test_state_t;

/**
 * @brief Test task data structure
 */
typedef struct {
    int task_id;
    int task_number;
    uint32_t run_count;
    uint32_t expected_runs;
    task_test_state_t state;
    uint64_t start_time_us;
    uint64_t end_time_us;
    bool should_yield;
    bool should_delay;
    uint32_t delay_ms;
    volatile bool* completion_flag;
} test_task_data_t;

/**
 * @brief Scheduler test context
 */
typedef struct {
    test_task_data_t task_data[SCHEDULER_TEST_MAX_TASKS];
    int active_tasks;
    volatile bool test_completed;
    volatile uint32_t tasks_completed;
    volatile uint32_t tasks_failed;
    uint64_t test_start_time;
    scheduler_stats_t initial_stats;
    scheduler_stats_t final_stats;
} scheduler_test_context_t;

// Test function declarations

/**
 * @brief Test scheduler initialization
 */
test_result_t test_scheduler_init(test_context_t* ctx);

/**
 * @brief Test scheduler start/stop functionality
 */
test_result_t test_scheduler_start_stop(test_context_t* ctx);

/**
 * @brief Test basic task creation
 */
test_result_t test_scheduler_task_creation(test_context_t* ctx);

/**
 * @brief Test task priority scheduling
 */
test_result_t test_scheduler_priority_scheduling(test_context_t* ctx);

/**
 * @brief Test multicore task distribution
 */
test_result_t test_scheduler_multicore_distribution(test_context_t* ctx);

/**
 * @brief Test task yield functionality
 */
test_result_t test_scheduler_task_yield(test_context_t* ctx);

/**
 * @brief Test scheduler statistics
 */
test_result_t test_scheduler_statistics(test_context_t* ctx);

/**
 * @brief Test task state transitions
 */
test_result_t test_scheduler_task_states(test_context_t* ctx);

/**
 * @brief Test scheduler with maximum tasks
 */
test_result_t test_scheduler_max_tasks(test_context_t* ctx);

/**
 * @brief Test task execution timing
 */
test_result_t test_scheduler_timing(test_context_t* ctx);

/**
 * @brief Test deadline scheduling
 */
test_result_t test_scheduler_deadline_scheduling(test_context_t* ctx);

/**
 * @brief Test deadline miss handling
 */
test_result_t test_scheduler_deadline_miss_handling(test_context_t* ctx);

/**
 * @brief Test hard deadline enforcement
 */
test_result_t test_scheduler_hard_deadline_enforcement(test_context_t* ctx);

/**
 * @brief Test scheduler stress with many tasks
 */
test_result_t test_scheduler_stress_many_tasks(test_context_t* ctx);

/**
 * @brief Test scheduler stress with rapid task creation/destruction
 */
test_result_t test_scheduler_stress_rapid_lifecycle(test_context_t* ctx);

/**
 * @brief Test scheduler performance under load
 */
test_result_t test_scheduler_performance_load(test_context_t* ctx);

/**
 * @brief Test invalid task parameters
 */
test_result_t test_scheduler_invalid_parameters(test_context_t* ctx);

/**
 * @brief Test scheduler error handling
 */
test_result_t test_scheduler_error_handling(test_context_t* ctx);

/**
 * @brief Test core affinity functionality
 */
test_result_t test_scheduler_core_affinity(test_context_t* ctx);

/**
 * @brief Test task type behavior (oneshot vs persistent)
 */
test_result_t test_scheduler_task_types(test_context_t* ctx);

/**
 * @brief Test scheduler integration with other components
 */
test_result_t test_scheduler_integration(test_context_t* ctx);

// Helper functions

/**
 * @brief Initialize scheduler test context
 */
bool scheduler_test_init_context(scheduler_test_context_t* test_ctx);

/**
 * @brief Clean up scheduler test context
 */
void scheduler_test_cleanup_context(scheduler_test_context_t* test_ctx);

/**
 * @brief Wait for task completion with timeout
 */
bool scheduler_test_wait_for_completion(volatile bool* flag, uint32_t timeout_ms);

/**
 * @brief Create a test task with specified parameters
 */
int scheduler_test_create_task(const char* name, void* params, 
                              task_priority_t priority, uint8_t core,
                              task_type_t type);

/**
 * @brief Verify scheduler statistics
 */
bool scheduler_test_verify_stats(const scheduler_stats_t* expected,
                                const scheduler_stats_t* actual,
                                bool allow_greater);

/**
 * @brief Get current task info safely
 */
bool scheduler_test_get_current_task_info(task_control_block_t* tcb);

/**
 * @brief Test task function templates
 */
void test_task_simple(void* params);
void test_task_yielding(void* params);
void test_task_timing(void* params);
void test_task_deadline(void* params);
void test_task_stress(void* params);

#ifdef __cplusplus
}
#endif

#endif // TEST_SCHEDULER_H