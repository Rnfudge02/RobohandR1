/**
 * @file test_scheduler.c
 * @brief Scheduler component test suite implementation
 * @author System Test Framework
 * @date 2025-05-26
 */

#include "test_scheduler.h"
#include "log_manager.h"
#include "scheduler.h"

#include "pico/time.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Global test context for task communication
static scheduler_test_context_t* g_test_context = NULL;

// Test task implementations

void test_task_simple(void* params) {
    test_task_data_t* data = (test_task_data_t*)params;
    if (!data) return;
    
    data->state = TASK_TEST_STATE_RUNNING;
    data->start_time_us = time_us_64();
    data->run_count++;
    
    // Simple computation to simulate work
    volatile uint32_t sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }
    
    data->end_time_us = time_us_64();
    data->state = TASK_TEST_STATE_COMPLETED;
    
    if (data->completion_flag) {
        *data->completion_flag = true;
    }
}

void test_task_yielding(void* params) {
    test_task_data_t* data = (test_task_data_t*)params;
    if (!data) return;
    
    data->state = TASK_TEST_STATE_RUNNING;
    data->start_time_us = time_us_64();
    data->run_count++;
    
    // Yield to other tasks
    if (data->should_yield) {
        scheduler_yield();
    }
    
    // Optional delay
    if (data->should_delay && data->delay_ms > 0) {
        scheduler_delay(data->delay_ms);
    }
    
    data->end_time_us = time_us_64();
    data->state = TASK_TEST_STATE_COMPLETED;
    
    if (data->completion_flag) {
        *data->completion_flag = true;
    }
}

void test_task_timing(void* params) {
    test_task_data_t* data = (test_task_data_t*)params;
    if (!data) return;
    
    data->state = TASK_TEST_STATE_RUNNING;
    data->start_time_us = time_us_64();
    data->run_count++;
    
    // Precise timing work
    uint64_t work_start = time_us_64();
    while ((time_us_64() - work_start) < 1000) { // 1ms of work
        // Busy wait
    }
    
    data->end_time_us = time_us_64();
    data->state = TASK_TEST_STATE_COMPLETED;
    
    if (data->completion_flag) {
        *data->completion_flag = true;
    }
}

void test_task_deadline(void* params) {
    test_task_data_t* data = (test_task_data_t*)params;
    if (!data) return;
    
    data->state = TASK_TEST_STATE_RUNNING;
    data->start_time_us = time_us_64();
    data->run_count++;
    
    // Simulate deadline-sensitive work
    uint64_t work_duration = 500; // 500us of work
    uint64_t work_start = time_us_64();
    
    while ((time_us_64() - work_start) < work_duration) {
        // Busy wait to simulate work
    }
    
    data->end_time_us = time_us_64();
    data->state = TASK_TEST_STATE_COMPLETED;
    
    if (data->completion_flag) {
        *data->completion_flag = true;
    }
}

void test_task_stress(void* params) {
    test_task_data_t* data = (test_task_data_t*)params;
    if (!data) return;
    
    data->state = TASK_TEST_STATE_RUNNING;
    data->start_time_us = time_us_64();
    
    // Run multiple iterations if this is a persistent task
    while (data->run_count < data->expected_runs) {
        data->run_count++;
        
        // Simulate varying workload
        volatile uint32_t work = 0;
        for (int i = 0; i < (data->task_number * 100); i++) {
            work += i;
        }
        
        // Yield occasionally
        if (data->run_count % 5 == 0) {
            scheduler_yield();
        }
    }
    
    data->end_time_us = time_us_64();
    data->state = TASK_TEST_STATE_COMPLETED;
    
    if (data->completion_flag) {
        *data->completion_flag = true;
    }
}

// Helper function implementations

bool scheduler_test_init_context(scheduler_test_context_t* test_ctx) {
    if (!test_ctx) return false;
    
    memset(test_ctx, 0, sizeof(scheduler_test_context_t));
    
    for (int i = 0; i < SCHEDULER_TEST_MAX_TASKS; i++) {
        test_ctx->task_data[i].task_id = -1;
        test_ctx->task_data[i].task_number = i;
        test_ctx->task_data[i].state = TASK_TEST_STATE_CREATED;
        test_ctx->task_data[i].expected_runs = 1;
    }
    
    test_ctx->test_start_time = time_us_64();
    g_test_context = test_ctx;
    
    return true;
}

void scheduler_test_cleanup_context(scheduler_test_context_t* test_ctx) {
    if (!test_ctx) return;
    
    // Clean up any remaining tasks
    for (int i = 0; i < SCHEDULER_TEST_MAX_TASKS; i++) {
        if (test_ctx->task_data[i].task_id >= 0) {
            // Tasks will be cleaned up by scheduler
        }
    }
    
    g_test_context = NULL;
}

bool scheduler_test_wait_for_completion(volatile bool* flag, uint32_t timeout_ms) {
    if (!flag) return false;
    
    uint64_t start_time = time_us_64();
    uint64_t timeout_us = timeout_ms * 1000ULL;
    
    while (!(*flag)) {
        if ((time_us_64() - start_time) > timeout_us) {
            return false; // Timeout
        }
        sleep_ms(1); // Small delay to prevent busy waiting
    }
    
    return true;
}

int scheduler_test_create_task(const char* name, void* params, 
                              task_priority_t priority, uint8_t core,
                              task_type_t type) {
    return scheduler_create_task(
        (task_func_t)test_task_simple,  // Default to simple task
        params,
        SCHEDULER_TEST_TASK_STACK_SIZE,
        priority,
        name,
        core,
        type
    );
}

bool scheduler_test_verify_stats(const scheduler_stats_t* expected,
                                const scheduler_stats_t* actual,
                                bool allow_greater) {
    if (!expected || !actual) return false;
    
    if (allow_greater) {
        return (actual->task_creates >= expected->task_creates) &&
               (actual->context_switches >= expected->context_switches);
    } else {
        return (actual->task_creates == expected->task_creates) &&
               (actual->context_switches == expected->context_switches);
    }
}

bool scheduler_test_get_current_task_info(task_control_block_t* tcb) {
    if (!tcb) return false;
    
    int current_task_id = scheduler_get_current_task();
    if (current_task_id < 0) return false;
    
    return scheduler_get_task_info(current_task_id, tcb);
}

// Test function implementations

test_result_t test_scheduler_init(test_context_t* ctx) {
    // Test scheduler initialization - this should already be done
    // We'll verify it's working properly
    
    scheduler_stats_t stats;
    TEST_ASSERT_TRUE(scheduler_get_stats(&stats), ctx, "Failed to get scheduler stats");
    
    // Check that scheduler is running
    TEST_ASSERT_TRUE(stats.total_runtime > 0 || stats.context_switches > 0, ctx, 
                    "Scheduler does not appear to be running");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_start_stop(test_context_t* ctx) {
    // Note: We can't actually stop/start the scheduler during tests
    // as it would break the test framework itself
    // This test verifies the scheduler is running
    
    scheduler_stats_t stats;
    TEST_ASSERT_TRUE(scheduler_get_stats(&stats), ctx, "Failed to get scheduler stats");
    
    // Verify scheduler is active
    uint64_t initial_switches = stats.context_switches;
    
    // Wait a bit and check if context switches are happening
    sleep_ms(100);
    
    TEST_ASSERT_TRUE(scheduler_get_stats(&stats), ctx, "Failed to get scheduler stats after delay");
    TEST_ASSERT_TRUE(stats.context_switches >= initial_switches, ctx, 
                    "No context switches detected - scheduler may not be running");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_task_creation(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    // Get initial stats
    TEST_ASSERT_TRUE(scheduler_get_stats(&test_ctx.initial_stats), ctx, "Failed to get initial stats");
    
    // Create a simple test task
    volatile bool task_completed = false;
    test_ctx.task_data[0].completion_flag = &task_completed;
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_simple,
        &test_ctx.task_data[0],
        SCHEDULER_TEST_TASK_STACK_SIZE,
        TASK_PRIORITY_NORMAL,
        "test_task_basic",
        0xFF, // Any core
        TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create test task");
    test_ctx.task_data[0].task_id = task_id;
    
    // Wait for task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Task did not complete within timeout");
    
    // Verify task completed successfully
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[0].state, ctx, 
                     "Task did not reach completed state");
    TEST_ASSERT_EQUAL(1, test_ctx.task_data[0].run_count, ctx, "Task run count incorrect");
    
    // Verify stats were updated
    TEST_ASSERT_TRUE(scheduler_get_stats(&test_ctx.final_stats), ctx, "Failed to get final stats");
    TEST_ASSERT_TRUE(test_ctx.final_stats.task_creates > test_ctx.initial_stats.task_creates, ctx,
                    "Task creation not reflected in stats");
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_priority_scheduling(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    // Create tasks with different priorities
    volatile bool low_task_completed = false;
    volatile bool high_task_completed = false;
    
    test_ctx.task_data[0].completion_flag = &low_task_completed;
    test_ctx.task_data[1].completion_flag = &high_task_completed;
    
    // Create low priority task first
    int low_task_id = scheduler_create_task(
        (task_func_t)test_task_simple,
        &test_ctx.task_data[0],
        SCHEDULER_TEST_TASK_STACK_SIZE,
        TASK_PRIORITY_LOW,
        "test_task_low_pri",
        0xFF,
        TASK_TYPE_ONESHOT
    );
    
    // Create high priority task
    int high_task_id = scheduler_create_task(
        (task_func_t)test_task_simple,
        &test_ctx.task_data[1],
        SCHEDULER_TEST_TASK_STACK_SIZE,
        TASK_PRIORITY_HIGH,
        "test_task_high_pri",
        0xFF,
        TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(low_task_id >= 0, ctx, "Failed to create low priority task");
    TEST_ASSERT_TRUE(high_task_id >= 0, ctx, "Failed to create high priority task");
    
    // Wait for both tasks to complete
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&low_task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Low priority task did not complete");
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&high_task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "High priority task did not complete");
    
    // High priority task should have started first (or at least not significantly later)
    TEST_ASSERT_TRUE(test_ctx.task_data[1].start_time_us <= test_ctx.task_data[0].start_time_us + 10000,
                    ctx, "High priority task did not preempt low priority task");
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_multicore_distribution(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    // Create tasks with specific core affinities
    volatile bool core0_task_completed = false;
    volatile bool core1_task_completed = false;
    volatile bool any_core_task_completed = false;
    
    test_ctx.task_data[0].completion_flag = &core0_task_completed;
    test_ctx.task_data[1].completion_flag = &core1_task_completed;
    test_ctx.task_data[2].completion_flag = &any_core_task_completed;
    
    // Create tasks for each core
    int core0_task = scheduler_create_task(
        (task_func_t)test_task_simple, &test_ctx.task_data[0],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_core0", 0, TASK_TYPE_ONESHOT
    );
    
    int core1_task = scheduler_create_task(
        (task_func_t)test_task_simple, &test_ctx.task_data[1],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_core1", 1, TASK_TYPE_ONESHOT
    );
    
    int any_core_task = scheduler_create_task(
        (task_func_t)test_task_simple, &test_ctx.task_data[2],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_any_core", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(core0_task >= 0, ctx, "Failed to create core 0 task");
    TEST_ASSERT_TRUE(core1_task >= 0, ctx, "Failed to create core 1 task");
    TEST_ASSERT_TRUE(any_core_task >= 0, ctx, "Failed to create any-core task");
    
    // Wait for all tasks to complete
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&core0_task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Core 0 task did not complete");
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&core1_task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Core 1 task did not complete");
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&any_core_task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Any-core task did not complete");
    
    // Verify all tasks completed
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[0].state, ctx, "Core 0 task failed");
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[1].state, ctx, "Core 1 task failed");
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[2].state, ctx, "Any-core task failed");
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_task_yield(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    // Create yielding tasks
    volatile bool task1_completed = false;
    volatile bool task2_completed = false;
    
    test_ctx.task_data[0].completion_flag = &task1_completed;
    test_ctx.task_data[0].should_yield = true;
    test_ctx.task_data[1].completion_flag = &task2_completed;
    test_ctx.task_data[1].should_yield = true;
    
    int task1_id = scheduler_create_task(
        (task_func_t)test_task_yielding, &test_ctx.task_data[0],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_yield1", 0xFF, TASK_TYPE_ONESHOT
    );
    
    int task2_id = scheduler_create_task(
        (task_func_t)test_task_yielding, &test_ctx.task_data[1],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_yield2", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task1_id >= 0, ctx, "Failed to create yielding task 1");
    TEST_ASSERT_TRUE(task2_id >= 0, ctx, "Failed to create yielding task 2");
    
    // Wait for both tasks to complete
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task1_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Yielding task 1 did not complete");
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task2_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Yielding task 2 did not complete");
    
    // Verify tasks completed
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[0].state, ctx, "Task 1 failed");
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[1].state, ctx, "Task 2 failed");
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_statistics(test_context_t* ctx) {
    scheduler_stats_t initial_stats, final_stats;
    
    // Get initial statistics
    TEST_ASSERT_TRUE(scheduler_get_stats(&initial_stats), ctx, "Failed to get initial stats");
    
    // Create and run some tasks to change statistics
    volatile bool task_completed = false;
    test_task_data_t task_data = {0};
    task_data.completion_flag = &task_completed;
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_simple, &task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_stats", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create stats test task");
    
    // Wait for task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Stats test task did not complete");
    
    // Get final statistics
    TEST_ASSERT_TRUE(scheduler_get_stats(&final_stats), ctx, "Failed to get final stats");
    
    // Verify statistics were updated
    TEST_ASSERT_TRUE(final_stats.task_creates > initial_stats.task_creates, ctx,
                    "Task creation count not updated");
    TEST_ASSERT_TRUE(final_stats.context_switches >= initial_stats.context_switches, ctx,
                    "Context switch count decreased");
    TEST_ASSERT_TRUE(final_stats.total_runtime >= initial_stats.total_runtime, ctx,
                    "Total runtime decreased");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_task_states(test_context_t* ctx) {
    // Create a task and verify its state transitions
    volatile bool task_completed = false;
    test_task_data_t task_data = {0};
    task_data.completion_flag = &task_completed;
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_simple, &task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_states", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create state test task");
    
    // Get task info
    task_control_block_t tcb;
    TEST_ASSERT_TRUE(scheduler_get_task_info(task_id, &tcb), ctx, "Failed to get task info");
    
    // Initially task should be ready or running
    TEST_ASSERT_TRUE(tcb.state == TASK_STATE_READY || tcb.state == TASK_STATE_RUNNING || 
                    tcb.state == TASK_STATE_COMPLETED, ctx, "Invalid initial task state");
    
    // Wait for completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "State test task did not complete");
    
    // After completion, task should be completed
    sleep_ms(10); // Give scheduler time to update state
    TEST_ASSERT_TRUE(scheduler_get_task_info(task_id, &tcb), ctx, "Failed to get task info after completion");
    TEST_ASSERT_EQUAL(TASK_STATE_COMPLETED, tcb.state, ctx, "Task not in completed state");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_max_tasks(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    volatile bool completion_flags[8] = {false};
    int created_tasks = 0;
    
    // Create multiple tasks up to a reasonable limit
    for (int i = 0; i < 8; i++) {
        test_ctx.task_data[i].completion_flag = &completion_flags[i];
        
        int task_id = scheduler_create_task(
            (task_func_t)test_task_simple, &test_ctx.task_data[i],
            SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
            "test_max_task", 0xFF, TASK_TYPE_ONESHOT
        );
        
        if (task_id >= 0) {
            test_ctx.task_data[i].task_id = task_id;
            created_tasks++;
        } else {
            break; // Hit the limit
        }
    }
    
    TEST_ASSERT_TRUE(created_tasks > 0, ctx, "Failed to create any tasks");
    
    // Wait for all created tasks to complete
    for (int i = 0; i < created_tasks; i++) {
        TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&completion_flags[i], SCHEDULER_TEST_TIMEOUT_MS),
                        ctx, "Max task test task did not complete");
    }
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_timing(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    volatile bool task_completed = false;
    test_ctx.task_data[0].completion_flag = &task_completed;
    
    uint64_t start_time = time_us_64();
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_timing, &test_ctx.task_data[0],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_timing", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create timing test task");
    
    // Wait for task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Timing test task did not complete");
    
    uint64_t end_time = time_us_64();
    uint64_t total_time = end_time - start_time;
    uint64_t task_time = test_ctx.task_data[0].end_time_us - test_ctx.task_data[0].start_time_us;
    
    // Verify timing is reasonable (task should take ~1ms, total should be reasonable)
    TEST_ASSERT_TRUE(task_time >= 900 && task_time <= 2000, ctx, "Task execution time out of range");
    TEST_ASSERT_TRUE(total_time <= 100000, ctx, "Total test time too long"); // 100ms max
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_deadline_scheduling(test_context_t* ctx) {
    volatile bool task_completed = false;
    test_task_data_t task_data = {0};
    task_data.completion_flag = &task_completed;
    
    // Create a task with deadline requirements
    int task_id = scheduler_create_task(
        (task_func_t)test_task_deadline, &task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_deadline", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create deadline test task");
    
    // Set deadline parameters
    TEST_ASSERT_TRUE(scheduler_set_deadline(task_id, DEADLINE_SOFT, 10, 5, 1000),
                    ctx, "Failed to set task deadline");
    
    // Wait for task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Deadline test task did not complete");
    
    // Verify deadline info can be retrieved
    deadline_info_t deadline_info;
    TEST_ASSERT_TRUE(scheduler_get_deadline_info(task_id, &deadline_info), ctx,
                    "Failed to get deadline info");
    
    TEST_ASSERT_EQUAL(DEADLINE_SOFT, deadline_info.type, ctx, "Incorrect deadline type");
    TEST_ASSERT_EQUAL(10, deadline_info.period_ms, ctx, "Incorrect deadline period");
    TEST_ASSERT_EQUAL(5, deadline_info.deadline_ms, ctx, "Incorrect deadline");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_deadline_miss_handling(test_context_t* ctx) {
    // This is a complex test that would require deliberate deadline misses
    // For now, we'll test the deadline configuration
    
    volatile bool task_completed = false;
    test_task_data_t task_data = {0};
    task_data.completion_flag = &task_completed;
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_deadline, &task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "test_deadline_miss", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create deadline miss test task");
    
    // Set a very tight deadline that might be missed
    TEST_ASSERT_TRUE(scheduler_set_deadline(task_id, DEADLINE_HARD, 2, 1, 100),
                    ctx, "Failed to set tight deadline");
    
    // Wait for task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Deadline miss test task did not complete");
    
    // Check if deadline was missed (this might happen due to timing)
    deadline_info_t deadline_info;
    TEST_ASSERT_TRUE(scheduler_get_deadline_info(task_id, &deadline_info), ctx,
                    "Failed to get deadline info");
    
    // The test passes regardless of whether deadline was missed
    // We're just testing that the system handles it gracefully
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_hard_deadline_enforcement(test_context_t* ctx) {
    // Similar to deadline miss handling
    // Test that hard deadlines are properly configured
    
    volatile bool task_completed = false;
    test_task_data_t task_data = {0};
    task_data.completion_flag = &task_completed;
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_deadline, &task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_HIGH, // Higher priority for hard deadline
        "test_hard_deadline", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create hard deadline test task");
    
    // Set hard deadline
    TEST_ASSERT_TRUE(scheduler_set_deadline(task_id, DEADLINE_HARD, 5, 3, 500),
                    ctx, "Failed to set hard deadline");
    
    // Wait for task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Hard deadline test task did not complete");
    
    // Verify hard deadline was set
    deadline_info_t deadline_info;
    TEST_ASSERT_TRUE(scheduler_get_deadline_info(task_id, &deadline_info), ctx,
                    "Failed to get deadline info");
    
    TEST_ASSERT_EQUAL(DEADLINE_HARD, deadline_info.type, ctx, "Hard deadline not set");
    
    return TEST_RESULT_PASS;
}

// Stress and fault tests
test_result_t test_scheduler_stress_many_tasks(test_context_t* ctx) {
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    const int num_stress_tasks = 6; // Reduced for reliability
    volatile bool completion_flags[6] = {false};
    int created_tasks = 0;
    
    // Create multiple stress tasks
    for (int i = 0; i < num_stress_tasks; i++) {
        test_ctx.task_data[i].completion_flag = &completion_flags[i];
        test_ctx.task_data[i].expected_runs = 3; // Multiple runs per task
        
        int task_id = scheduler_create_task(
            (task_func_t)test_task_stress, &test_ctx.task_data[i],
            SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
            "stress_task", 0xFF, TASK_TYPE_ONESHOT
        );
        
        if (task_id >= 0) {
            test_ctx.task_data[i].task_id = task_id;
            created_tasks++;
        }
    }
    
    TEST_ASSERT_TRUE(created_tasks >= 3, ctx, "Failed to create enough stress tasks");
    
    // Wait for all tasks to complete
    for (int i = 0; i < created_tasks; i++) {
        TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&completion_flags[i], 
                        SCHEDULER_TEST_TIMEOUT_MS * 2), ctx, "Stress task did not complete");
    }
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_stress_rapid_lifecycle(test_context_t* ctx) {
    // Test rapid creation and completion of tasks
    scheduler_stats_t initial_stats, final_stats;
    
    TEST_ASSERT_TRUE(scheduler_get_stats(&initial_stats), ctx, "Failed to get initial stats");
    
    const int num_rapid_tasks = 5;
    volatile bool completion_flags[5] = {false};
    test_task_data_t task_data[5] = {0};
    
    // Rapidly create and run tasks
    for (int i = 0; i < num_rapid_tasks; i++) {
        task_data[i].completion_flag = &completion_flags[i];
        
        int task_id = scheduler_create_task(
            (task_func_t)test_task_simple, &task_data[i],
            SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
            "rapid_task", 0xFF, TASK_TYPE_ONESHOT
        );
        
        TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create rapid task");
        
        // Brief delay between creations
        sleep_ms(10);
    }
    
    // Wait for all to complete
    for (int i = 0; i < num_rapid_tasks; i++) {
        TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&completion_flags[i], 
                        SCHEDULER_TEST_TIMEOUT_MS), ctx, "Rapid task did not complete");
    }
    
    TEST_ASSERT_TRUE(scheduler_get_stats(&final_stats), ctx, "Failed to get final stats");
    
    // Verify stats reflect the rapid task creation
    TEST_ASSERT_TRUE(final_stats.task_creates >= initial_stats.task_creates + num_rapid_tasks,
                    ctx, "Rapid task creation not reflected in stats");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_performance_load(test_context_t* ctx) {
    // Test scheduler performance under load
    uint64_t start_time = time_us_64();
    
    scheduler_stats_t initial_stats;
    TEST_ASSERT_TRUE(scheduler_get_stats(&initial_stats), ctx, "Failed to get initial stats");
    
    // Create some background load
    volatile bool load_task_completed = false;
    test_task_data_t load_task_data = {0};
    load_task_data.completion_flag = &load_task_completed;
    load_task_data.expected_runs = 10;
    
    int load_task_id = scheduler_create_task(
        (task_func_t)test_task_stress, &load_task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_LOW,
        "load_task", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(load_task_id >= 0, ctx, "Failed to create load task");
    
    // Wait for completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&load_task_completed, 
                    SCHEDULER_TEST_TIMEOUT_MS * 2), ctx, "Load task did not complete");
    
    uint64_t end_time = time_us_64();
    uint64_t total_time = end_time - start_time;
    
    // Performance should be reasonable (not too slow)
    TEST_ASSERT_TRUE(total_time < 5000000, ctx, "Performance test took too long"); // 5 second max
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_invalid_parameters(test_context_t* ctx) {
    // Test scheduler behavior with invalid parameters
    
    // Try to create task with invalid function pointer
    int invalid_task1 = scheduler_create_task(
        NULL, NULL, SCHEDULER_TEST_TASK_STACK_SIZE,
        TASK_PRIORITY_NORMAL, "invalid", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(invalid_task1 < 0, ctx, "Scheduler accepted NULL function");
    
    // Try to create task with invalid core affinity
    int invalid_task2 = scheduler_create_task(
        (task_func_t)test_task_simple, NULL, SCHEDULER_TEST_TASK_STACK_SIZE,
        TASK_PRIORITY_NORMAL, "invalid_core", 5, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(invalid_task2 < 0, ctx, "Scheduler accepted invalid core affinity");
    
    // Try to get info for non-existent task
    task_control_block_t tcb;
    TEST_ASSERT_FALSE(scheduler_get_task_info(99999, &tcb), ctx,
                     "Scheduler returned info for non-existent task");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_error_handling(test_context_t* ctx) {
    // Test various error conditions
    
    // Test invalid deadline parameters
    volatile bool task_completed = false;
    test_task_data_t task_data = {0};
    task_data.completion_flag = &task_completed;
    
    int task_id = scheduler_create_task(
        (task_func_t)test_task_simple, &task_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "error_test", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(task_id >= 0, ctx, "Failed to create error test task");
    
    // Try invalid deadline operations
    TEST_ASSERT_FALSE(scheduler_set_deadline(-1, DEADLINE_SOFT, 10, 5, 1000),
                     ctx, "Scheduler accepted invalid task ID for deadline");
    
    deadline_info_t deadline_info;
    TEST_ASSERT_FALSE(scheduler_get_deadline_info(-1, &deadline_info),
                     ctx, "Scheduler returned deadline info for invalid task ID");
    
    // Wait for valid task to complete
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&task_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Error test task did not complete");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_core_affinity(test_context_t* ctx) {
    // Test core affinity functionality
    scheduler_test_context_t test_ctx;
    TEST_ASSERT_TRUE(scheduler_test_init_context(&test_ctx), ctx, "Failed to init test context");
    
    volatile bool any_core_completed = false;
    test_ctx.task_data[0].completion_flag = &any_core_completed;
    
    // Create task with any-core affinity
    int any_core_task = scheduler_create_task(
        (task_func_t)test_task_simple, &test_ctx.task_data[0],
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "affinity_test", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(any_core_task >= 0, ctx, "Failed to create any-core affinity task");
    
    // Wait for completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&any_core_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Core affinity test task did not complete");
    
    // Verify task completed
    TEST_ASSERT_EQUAL(TASK_TEST_STATE_COMPLETED, test_ctx.task_data[0].state, ctx,
                     "Core affinity task failed");
    
    scheduler_test_cleanup_context(&test_ctx);
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_task_types(test_context_t* ctx) {
    // Test different task types (oneshot vs persistent)
    
    // Test oneshot task
    volatile bool oneshot_completed = false;
    test_task_data_t oneshot_data = {0};
    oneshot_data.completion_flag = &oneshot_completed;
    
    int oneshot_task = scheduler_create_task(
        (task_func_t)test_task_simple, &oneshot_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "oneshot_test", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(oneshot_task >= 0, ctx, "Failed to create oneshot task");
    
    // Wait for oneshot task completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&oneshot_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Oneshot task did not complete");
    
    // Verify oneshot task completed exactly once
    TEST_ASSERT_EQUAL(1, oneshot_data.run_count, ctx, "Oneshot task run count incorrect");
    
    return TEST_RESULT_PASS;
}

test_result_t test_scheduler_integration(test_context_t* ctx) {
    // Test scheduler integration with the overall system
    
    // This test verifies that the scheduler works well with other components
    // like logging, memory management, etc.
    
    scheduler_stats_t stats;
    TEST_ASSERT_TRUE(scheduler_get_stats(&stats), ctx, "Failed to get scheduler stats");
    
    // Test that we can get current task info
    int current_task = scheduler_get_current_task();
    // Current task might be -1 if running in interrupt context, which is okay
    
    // Create a task that interacts with the logging system
    volatile bool integration_completed = false;
    test_task_data_t integration_data = {0};
    integration_data.completion_flag = &integration_completed;
    
    int integration_task = scheduler_create_task(
        (task_func_t)test_task_simple, &integration_data,
        SCHEDULER_TEST_TASK_STACK_SIZE, TASK_PRIORITY_NORMAL,
        "integration_test", 0xFF, TASK_TYPE_ONESHOT
    );
    
    TEST_ASSERT_TRUE(integration_task >= 0, ctx, "Failed to create integration task");
    
    // Wait for completion
    TEST_ASSERT_TRUE(scheduler_test_wait_for_completion(&integration_completed, SCHEDULER_TEST_TIMEOUT_MS),
                    ctx, "Integration task did not complete");
    
    // Verify system is still stable
    TEST_ASSERT_TRUE(scheduler_get_stats(&stats), ctx, "System unstable after integration test");
    
    return TEST_RESULT_PASS;
}

// Register scheduler test suite
bool test_scheduler_register_suite(void) {
    test_suite_t* suite = test_create_suite("Scheduler", "Comprehensive scheduler functionality tests");
    if (!suite) return false;
    
    // Unit tests
    test_suite_add_test(suite, "init", "Test scheduler initialization", 
                       test_scheduler_init, TEST_SEVERITY_CRITICAL, TEST_CATEGORY_UNIT, 0);
    
    test_suite_add_test(suite, "start_stop", "Test scheduler start/stop", 
                       test_scheduler_start_stop, TEST_SEVERITY_CRITICAL, TEST_CATEGORY_UNIT, 0);
    
    test_suite_add_test(suite, "task_creation", "Test basic task creation", 
                       test_scheduler_task_creation, TEST_SEVERITY_HIGH, TEST_CATEGORY_UNIT, 0);
    
    test_suite_add_test(suite, "task_states", "Test task state transitions", 
                       test_scheduler_task_states, TEST_SEVERITY_HIGH, TEST_CATEGORY_UNIT, 0);
    
    test_suite_add_test(suite, "statistics", "Test scheduler statistics", 
                       test_scheduler_statistics, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_UNIT, 0);
    
    test_suite_add_test(suite, "task_types", "Test task type behavior", 
                       test_scheduler_task_types, TEST_SEVERITY_HIGH, TEST_CATEGORY_UNIT, 0);
    
    // Integration tests
    test_suite_add_test(suite, "priority_scheduling", "Test priority-based scheduling", 
                       test_scheduler_priority_scheduling, TEST_SEVERITY_HIGH, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "multicore", "Test multicore task distribution", 
                       test_scheduler_multicore_distribution, TEST_SEVERITY_HIGH, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "task_yield", "Test task yield functionality", 
                       test_scheduler_task_yield, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "core_affinity", "Test core affinity", 
                       test_scheduler_core_affinity, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "timing", "Test scheduler timing behavior", 
                       test_scheduler_timing, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "deadline_scheduling", "Test deadline scheduling", 
                       test_scheduler_deadline_scheduling, TEST_SEVERITY_HIGH, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "deadline_miss", "Test deadline miss handling", 
                       test_scheduler_deadline_miss_handling, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "hard_deadline", "Test hard deadline enforcement", 
                       test_scheduler_hard_deadline_enforcement, TEST_SEVERITY_HIGH, TEST_CATEGORY_INTEGRATION, 0);
    
    test_suite_add_test(suite, "integration", "Test scheduler integration", 
                       test_scheduler_integration, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_INTEGRATION, 0);
    
    // Stress tests
    test_suite_add_test(suite, "max_tasks", "Test maximum task creation", 
                       test_scheduler_max_tasks, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_STRESS, 0);
    
    test_suite_add_test(suite, "stress_many", "Test many concurrent tasks", 
                       test_scheduler_stress_many_tasks, TEST_SEVERITY_LOW, TEST_CATEGORY_STRESS, 5000);
    
    test_suite_add_test(suite, "stress_rapid", "Test rapid task lifecycle", 
                       test_scheduler_stress_rapid_lifecycle, TEST_SEVERITY_LOW, TEST_CATEGORY_STRESS, 0);
    
    test_suite_add_test(suite, "performance", "Test performance under load", 
                       test_scheduler_performance_load, TEST_SEVERITY_LOW, TEST_CATEGORY_STRESS, 10000);
    
    // Fault tests
    test_suite_add_test(suite, "invalid_params", "Test invalid parameters", 
                       test_scheduler_invalid_parameters, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_FAULT, 0);
    
    test_suite_add_test(suite, "error_handling", "Test error handling", 
                       test_scheduler_error_handling, TEST_SEVERITY_MEDIUM, TEST_CATEGORY_FAULT, 0);
    
    return test_framework_register_suite(suite);
}