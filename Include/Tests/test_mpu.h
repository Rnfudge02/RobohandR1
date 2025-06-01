/**
 * @file test_mpu.h
 * @brief Memory Protection Unit (MPU) test suite
 * @author System Test Framework
 * @date 2025-05-26
 * 
 * This module provides comprehensive testing for the MPU component,
 * including memory region configuration, access permissions, fault handling,
 * and integration with the scheduler.
 */

#ifndef TEST_MPU_H
#define TEST_MPU_H

#include "test_framework.h"
#include "scheduler_mpu.h"

#ifdef __cplusplus
extern "C" {
#endif

// Test configuration constants
#define MPU_TEST_REGION_SIZE        4096    // 4KB test regions
#define MPU_TEST_BUFFER_SIZE        1024    // Test buffer size
#define MPU_TEST_TIMEOUT_MS         3000    // Test timeout
#define MPU_TEST_MAX_REGIONS        8       // Maximum regions to test

/**
 * @brief MPU test memory regions
 */
typedef enum {
    MPU_TEST_REGION_STACK = 0,
    MPU_TEST_REGION_CODE,
    MPU_TEST_REGION_DATA_RO,
    MPU_TEST_REGION_DATA_RW,
    MPU_TEST_REGION_PERIPHERAL,
    MPU_TEST_REGION_INVALID,
    MPU_TEST_REGION_COUNT
} mpu_test_region_type_t;

/**
 * @brief MPU test memory layout
 */
typedef struct {
    void* region_start[MPU_TEST_REGION_COUNT];
    size_t region_size[MPU_TEST_REGION_COUNT];
    mpu_access_permissions_t region_access[MPU_TEST_REGION_COUNT];
    bool region_valid[MPU_TEST_REGION_COUNT];
} mpu_test_memory_layout_t;

/**
 * @brief MPU test context
 */
typedef struct {
    uint32_t test_task_id;
    mpu_test_memory_layout_t memory_layout;
    uint8_t test_buffer[MPU_TEST_BUFFER_SIZE];
    volatile bool fault_occurred;
    uint32_t fault_address;
    uint32_t fault_type;
    volatile bool test_completed;
    mpu_status_info_t initial_status;
    mpu_status_info_t final_status;
    mpu_perf_stats_t perf_stats;
} mpu_test_context_t;

/**
 * @brief MPU fault test parameters
 */
typedef struct {
    void* target_address;
    bool should_read;
    bool should_write;
    bool should_execute;
    bool expect_fault;
    const char* test_description;
} mpu_fault_test_params_t;

// Test function declarations

/**
 * @brief Test MPU hardware support detection
 */
test_result_t test_mpu_hardware_support(test_context_t* ctx);

/**
 * @brief Test MPU initialization
 */
test_result_t test_mpu_initialization(test_context_t* ctx);

/**
 * @brief Test MPU enable/disable functionality
 */
test_result_t test_mpu_enable_disable(test_context_t* ctx);

/**
 * @brief Test basic region configuration
 */
test_result_t test_mpu_region_configuration(test_context_t* ctx);

/**
 * @brief Test memory access permissions
 */
test_result_t test_mpu_access_permissions(test_context_t* ctx);

/**
 * @brief Test read-only region protection
 */
test_result_t test_mpu_readonly_protection(test_context_t* ctx);

/**
 * @brief Test execute never (XN) protection
 */
test_result_t test_mpu_execute_protection(test_context_t* ctx);

/**
 * @brief Test MPU region alignment requirements
 */
test_result_t test_mpu_region_alignment(test_context_t* ctx);

/**
 * @brief Test MPU region size requirements
 */
test_result_t test_mpu_region_sizes(test_context_t* ctx);

/**
 * @brief Test task-specific MPU configuration
 */
test_result_t test_mpu_task_configuration(test_context_t* ctx);

/**
 * @brief Test MPU settings application during task switch
 */
test_result_t test_mpu_task_switching(test_context_t* ctx);

/**
 * @brief Test MPU fault detection and handling
 */
test_result_t test_mpu_fault_handling(test_context_t* ctx);

/**
 * @brief Test MPU fault recovery mechanisms
 */
test_result_t test_mpu_fault_recovery(test_context_t* ctx);

/**
 * @brief Test MPU status reporting
 */
test_result_t test_mpu_status_reporting(test_context_t* ctx);

/**
 * @brief Test MPU performance statistics
 */
test_result_t test_mpu_performance_stats(test_context_t* ctx);

/**
 * @brief Test default MPU configuration creation
 */
test_result_t test_mpu_default_config(test_context_t* ctx);

/**
 * @brief Test MPU accessibility checking
 */
test_result_t test_mpu_accessibility_check(test_context_t* ctx);

/**
 * @brief Test MPU integration with scheduler
 */
test_result_t test_mpu_scheduler_integration(test_context_t* ctx);

/**
 * @brief Test multiple MPU regions per task
 */
test_result_t test_mpu_multiple_regions(test_context_t* ctx);

/**
 * @brief Test MPU region overlap detection
 */
test_result_t test_mpu_region_overlap(test_context_t* ctx);

/**
 * @brief Test MPU with maximum regions
 */
test_result_t test_mpu_max_regions(test_context_t* ctx);

/**
 * @brief Test MPU stress with rapid configuration changes
 */
test_result_t test_mpu_stress_rapid_config(test_context_t* ctx);

/**
 * @brief Test MPU stress with many tasks
 */
test_result_t test_mpu_stress_many_tasks(test_context_t* ctx);

/**
 * @brief Test MPU performance under load
 */
test_result_t test_mpu_performance_load(test_context_t* ctx);

/**
 * @brief Test invalid MPU configurations
 */
test_result_t test_mpu_invalid_config(test_context_t* ctx);

/**
 * @brief Test MPU error conditions
 */
test_result_t test_mpu_error_conditions(test_context_t* ctx);

/**
 * @brief Test MPU fault injection
 */
test_result_t test_mpu_fault_injection(test_context_t* ctx);

/**
 * @brief Test MPU boundary conditions
 */
test_result_t test_mpu_boundary_conditions(test_context_t* ctx);

/**
 * @brief Test MPU security isolation
 */
test_result_t test_mpu_security_isolation(test_context_t* ctx);

// Helper functions

/**
 * @brief Initialize MPU test context
 */
bool mpu_test_init_context(mpu_test_context_t* test_ctx);

/**
 * @brief Clean up MPU test context
 */
void mpu_test_cleanup_context(mpu_test_context_t* test_ctx);

/**
 * @brief Setup test memory layout
 */
bool mpu_test_setup_memory_layout(mpu_test_memory_layout_t* layout);

/**
 * @brief Create test task with MPU protection
 */
int mpu_test_create_protected_task(const char* name, void* params,
                                  const task_mpu_config_t* mpu_config);

/**
 * @brief Verify MPU configuration matches expected
 */
bool mpu_test_verify_config(const task_mpu_config_t* expected,
                           const task_mpu_config_t* actual);

/**
 * @brief Test memory access with fault detection
 */
bool mpu_test_memory_access(void* address, bool read, bool write, bool execute,
                           bool expect_fault, mpu_test_context_t* test_ctx);

/**
 * @brief Generate controlled MPU fault
 */
bool mpu_test_generate_fault(mpu_fault_test_params_t* params,
                            mpu_test_context_t* test_ctx);

/**
 * @brief Wait for MPU test completion
 */
bool mpu_test_wait_completion(mpu_test_context_t* test_ctx, uint32_t timeout_ms);

/**
 * @brief Register MPU fault handler for testing
 */
bool mpu_test_register_fault_handler(mpu_test_context_t* test_ctx);

/**
 * @brief Verify MPU status information
 */
bool mpu_test_verify_status(const mpu_status_info_t* status);

/**
 * @brief Create test memory regions
 */
bool mpu_test_create_regions(mpu_test_context_t* test_ctx, uint32_t task_id);

/**
 * @brief Test task functions for MPU testing
 */
void mpu_test_task_basic(void* params);
void mpu_test_task_memory_access(void* params);
void mpu_test_task_fault_generator(void* params);
void mpu_test_task_boundary_test(void* params);

/**
 * @brief MPU fault handler for testing
 */
void mpu_test_fault_handler(uint32_t task_id, void* fault_addr, uint32_t fault_type);

/**
 * @brief Get safe test memory address
 */
void* mpu_test_get_safe_address(size_t size);

/**
 * @brief Get unsafe test memory address
 */
void* mpu_test_get_unsafe_address(void);

/**
 * @brief Check if address is in valid memory range
 */
bool mpu_test_is_valid_address(void* address, size_t size);

/**
 * @brief Create aligned memory region for testing
 */
void* mpu_test_allocate_aligned_region(size_t size, size_t alignment);

/**
 * @brief Free aligned memory region
 */
void mpu_test_free_aligned_region(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // TEST_MPU_H