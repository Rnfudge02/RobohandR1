/**
 * @file test_tz.h
 * @brief TrustZone Security Extension test suite
 * @author System Test Framework
 * @date 2025-05-26
 * 
 * This module provides comprehensive testing for the TrustZone security
 * extension, including secure/non-secure state transitions, secure function
 * registration, SAU configuration, and integration with the scheduler.
 */

#ifndef TEST_TZ_H
#define TEST_TZ_H

#include "test_framework.h"
#include "scheduler_tz.h"

#ifdef __cplusplus
extern "C" {
#endif

// Test configuration constants
#define TZ_TEST_TIMEOUT_MS              5000    // Test timeout
#define TZ_TEST_MAX_SECURE_FUNCTIONS    8       // Maximum secure functions to test
#define TZ_TEST_BUFFER_SIZE             1024    // Test buffer size
#define TZ_TEST_NSC_REGION_SIZE         4096    // Non-secure callable region size

/**
 * @brief TrustZone test security states
 */
typedef enum {
    TZ_TEST_STATE_UNKNOWN = 0,
    TZ_TEST_STATE_SECURE,
    TZ_TEST_STATE_NON_SECURE,
    TZ_TEST_STATE_TRANSITIONAL
} tz_test_security_state_t;

/**
 * @brief TrustZone test context
 */
typedef struct {
    uint32_t test_task_id;
    tz_test_security_state_t expected_state;
    tz_test_security_state_t actual_state;
    volatile bool test_completed;
    volatile bool transition_occurred;
    volatile bool secure_function_called;
    uint32_t transition_count;
    uint64_t test_start_time;
    tz_status_info_t initial_status;
    tz_status_info_t final_status;
    tz_perf_stats_t perf_stats;
    uint8_t test_buffer[TZ_TEST_BUFFER_SIZE];
} tz_test_context_t;

/**
 * @brief Secure function test parameters
 */
typedef struct {
    const char* function_name;
    void* secure_function_ptr;
    void* nsc_function_ptr;
    uint32_t test_parameter;
    uint32_t expected_result;
    bool function_registered;
} tz_secure_function_test_t;

/**
 * @brief TrustZone transition test parameters
 */
typedef struct {
    task_security_state_t from_state;
    task_security_state_t to_state;
    bool transition_expected;
    const char* test_description;
} tz_transition_test_params_t;

// Test function declarations

/**
 * @brief Test TrustZone hardware support detection
 */
test_result_t test_tz_hardware_support(test_context_t* ctx);

/**
 * @brief Test TrustZone initialization
 */
test_result_t test_tz_initialization(test_context_t* ctx);

/**
 * @brief Test TrustZone enable/disable functionality
 */
test_result_t test_tz_enable_disable(test_context_t* ctx);

/**
 * @brief Test security state detection
 */
test_result_t test_tz_security_state_detection(test_context_t* ctx);

/**
 * @brief Test secure to non-secure transitions
 */
test_result_t test_tz_secure_to_nonsecure_transition(test_context_t* ctx);

/**
 * @brief Test non-secure to secure transitions
 */
test_result_t test_tz_nonsecure_to_secure_transition(test_context_t* ctx);

/**
 * @brief Test task security state configuration
 */
test_result_t test_tz_task_security_configuration(test_context_t* ctx);

/**
 * @brief Test secure function registration
 */
test_result_t test_tz_secure_function_registration(test_context_t* ctx);

/**
 * @brief Test non-secure callable (NSC) function creation
 */
test_result_t test_tz_nsc_function_creation(test_context_t* ctx);

/**
 * @brief Test secure function invocation
 */
test_result_t test_tz_secure_function_invocation(test_context_t* ctx);

/**
 * @brief Test SAU (Security Attribution Unit) configuration
 */
test_result_t test_tz_sau_configuration(test_context_t* ctx);

/**
 * @brief Test TrustZone fault handling
 */
test_result_t test_tz_fault_handling(test_context_t* ctx);

/**
 * @brief Test TrustZone status reporting
 */
test_result_t test_tz_status_reporting(test_context_t* ctx);

/**
 * @brief Test TrustZone performance statistics
 */
test_result_t test_tz_performance_stats(test_context_t* ctx);

/**
 * @brief Test TrustZone integration with scheduler
 */
test_result_t test_tz_scheduler_integration(test_context_t* ctx);

/**
 * @brief Test multiple secure functions
 */
test_result_t test_tz_multiple_secure_functions(test_context_t* ctx);

/**
 * @brief Test TrustZone task switching
 */
test_result_t test_tz_task_switching(test_context_t* ctx);

/**
 * @brief Test security boundary enforcement
 */
test_result_t test_tz_security_boundary_enforcement(test_context_t* ctx);

/**
 * @brief Test secure memory isolation
 */
test_result_t test_tz_secure_memory_isolation(test_context_t* ctx);

/**
 * @brief Test TrustZone with maximum secure functions
 */
test_result_t test_tz_max_secure_functions(test_context_t* ctx);

/**
 * @brief Test TrustZone stress with rapid transitions
 */
test_result_t test_tz_stress_rapid_transitions(test_context_t* ctx);

/**
 * @brief Test TrustZone stress with many tasks
 */
test_result_t test_tz_stress_many_tasks(test_context_t* ctx);

/**
 * @brief Test TrustZone performance under load
 */
test_result_t test_tz_performance_load(test_context_t* ctx);

/**
 * @brief Test invalid TrustZone configurations
 */
test_result_t test_tz_invalid_config(test_context_t* ctx);

/**
 * @brief Test TrustZone error conditions
 */
test_result_t test_tz_error_conditions(test_context_t* ctx);

/**
 * @brief Test TrustZone security violation detection
 */
test_result_t test_tz_security_violation_detection(test_context_t* ctx);

/**
 * @brief Test TrustZone secure/non-secure data isolation
 */
test_result_t test_tz_data_isolation(test_context_t* ctx);

/**
 * @brief Test TrustZone privilege escalation prevention
 */
test_result_t test_tz_privilege_escalation_prevention(test_context_t* ctx);

// Helper functions

/**
 * @brief Initialize TrustZone test context
 */
bool tz_test_init_context(tz_test_context_t* test_ctx);

/**
 * @brief Clean up TrustZone test context
 */
void tz_test_cleanup_context(tz_test_context_t* test_ctx);

/**
 * @brief Create test task with TrustZone configuration
 */
int tz_test_create_secure_task(const char* name, void* params,
                              task_security_state_t security_state);

/**
 * @brief Wait for TrustZone test completion
 */
bool tz_test_wait_completion(tz_test_context_t* test_ctx, uint32_t timeout_ms);

/**
 * @brief Verify TrustZone configuration
 */
bool tz_test_verify_config(const task_tz_config_t* expected,
                          const task_tz_config_t* actual);

/**
 * @brief Test security state transition
 */
bool tz_test_security_transition(task_security_state_t from_state,
                                task_security_state_t to_state,
                                tz_test_context_t* test_ctx);

/**
 * @brief Register test secure function
 */
bool tz_test_register_secure_function(const char* name,
                                     void* secure_func,
                                     void** nsc_func);

/**
 * @brief Verify TrustZone status information
 */
bool tz_test_verify_status(const tz_status_info_t* status);

/**
 * @brief Get current security state for testing
 */
tz_test_security_state_t tz_test_get_current_state(void);

/**
 * @brief Create secure memory region for testing
 */
void* tz_test_create_secure_region(size_t size);

/**
 * @brief Free secure memory region
 */
void tz_test_free_secure_region(void* ptr);

/**
 * @brief Test task functions for TrustZone testing
 */
void tz_test_task_secure(void* params);
void tz_test_task_non_secure(void* params);
void tz_test_task_transitional(void* params);
void tz_test_task_boundary_test(void* params);

/**
 * @brief Test secure functions
 */
uint32_t tz_test_secure_function_add(uint32_t a, uint32_t b);
uint32_t tz_test_secure_function_multiply(uint32_t a, uint32_t b);
uint32_t tz_test_secure_function_hash(const uint8_t* data, size_t len);

/**
 * @brief TrustZone fault handler for testing
 */
void tz_test_security_fault_handler(uint32_t task_id, uint32_t fault_type);

/**
 * @brief Verify security isolation
 */
bool tz_test_verify_isolation(tz_test_context_t* test_ctx);

/**
 * @brief Test security boundary crossing
 */
bool tz_test_boundary_crossing(void* secure_addr, void* nonsecure_addr);

/**
 * @brief Check if address is in secure region
 */
bool tz_test_is_secure_address(void* address);

/**
 * @brief Check if address is in non-secure region
 */
bool tz_test_is_nonsecure_address(void* address);

/**
 * @brief Simulate security violation
 */
bool tz_test_simulate_violation(tz_test_context_t* test_ctx);

/**
 * @brief Test secure function calling convention
 */
bool tz_test_secure_calling_convention(void* nsc_func, uint32_t param);

/**
 * @brief Create test NSC veneer
 */
void* tz_test_create_nsc_veneer(void* secure_func);

/**
 * @brief Validate NSC veneer
 */
bool tz_test_validate_nsc_veneer(void* veneer_addr);

#ifdef __cplusplus
}
#endif

#endif // TEST_TZ_H