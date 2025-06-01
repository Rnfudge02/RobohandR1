/**
 * @file test_framework.h
 * @brief Runtime testing framework for RTOS components
 * @author System Test Framework
 * @date 2025-05-26
 * 
 * This framework provides comprehensive testing of kernel components during
 * system initialization to ensure stability and correct operation before
 * allowing user interaction with the system.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Test framework configuration
#define TEST_MAX_NAME_LEN           32
#define TEST_MAX_DESCRIPTION_LEN    128
#define TEST_MAX_TESTS_PER_SUITE    64
#define TEST_MAX_SUITES             16
#define TEST_TIMEOUT_DEFAULT_MS     5000

/**
 * @brief Test result codes
 */
typedef enum {
    TEST_RESULT_PASS = 0,        ///< Test passed successfully
    TEST_RESULT_FAIL,            ///< Test failed
    TEST_RESULT_SKIP,            ///< Test was skipped
    TEST_RESULT_TIMEOUT,         ///< Test timed out
    TEST_RESULT_ERROR,           ///< Test encountered an error
    TEST_RESULT_NOT_RUN          ///< Test has not been executed
} test_result_t;

/**
 * @brief Test severity levels
 */
typedef enum {
    TEST_SEVERITY_CRITICAL = 0,  ///< Critical - system cannot continue
    TEST_SEVERITY_HIGH,          ///< High - major functionality affected
    TEST_SEVERITY_MEDIUM,        ///< Medium - minor functionality affected
    TEST_SEVERITY_LOW,           ///< Low - cosmetic or edge case issues
    TEST_SEVERITY_INFO           ///< Info - verification only
} test_severity_t;

/**
 * @brief Test categories
 */
typedef enum {
    TEST_CATEGORY_UNIT = 0,      ///< Unit tests for individual components
    TEST_CATEGORY_INTEGRATION,   ///< Integration tests between components
    TEST_CATEGORY_STRESS,        ///< Stress and performance tests
    TEST_CATEGORY_FAULT,         ///< Fault injection and recovery tests
    TEST_CATEGORY_SECURITY       ///< Security and isolation tests
} test_category_t;

/**
 * @brief Test execution context
 */
typedef struct {
    uint32_t test_id;            ///< Unique test identifier
    const char* test_name;       ///< Test name
    const char* suite_name;      ///< Test suite name
    uint32_t timeout_ms;         ///< Test timeout in milliseconds
    uint64_t start_time_us;      ///< Test start time in microseconds
    uint64_t end_time_us;        ///< Test end time in microseconds
    test_severity_t severity;    ///< Test severity level
    test_category_t category;    ///< Test category
    void* user_data;             ///< User-defined test data
} test_context_t;

/**
 * @brief Test function signature
 * 
 * @param ctx Test execution context
 * @return Test result code
 */
typedef test_result_t (*test_function_t)(test_context_t* ctx);

/**
 * @brief Test case definition
 */
typedef struct {
    char name[TEST_MAX_NAME_LEN];                    ///< Test name
    char description[TEST_MAX_DESCRIPTION_LEN];      ///< Test description
    test_function_t function;                        ///< Test function pointer
    test_severity_t severity;                        ///< Test severity
    test_category_t category;                        ///< Test category
    uint32_t timeout_ms;                            ///< Test timeout
    bool enabled;                                   ///< Test enabled flag
    test_result_t result;                           ///< Last test result
    uint64_t execution_time_us;                     ///< Last execution time
    char failure_reason[TEST_MAX_DESCRIPTION_LEN];  ///< Failure reason
} test_case_t;

/**
 * @brief Test suite definition
 */
typedef struct {
    char name[TEST_MAX_NAME_LEN];                    ///< Suite name
    char description[TEST_MAX_DESCRIPTION_LEN];      ///< Suite description
    test_case_t tests[TEST_MAX_TESTS_PER_SUITE];     ///< Array of test cases
    uint32_t test_count;                            ///< Number of tests in suite
    bool enabled;                                   ///< Suite enabled flag
    
    // Statistics
    uint32_t tests_passed;                          ///< Number of passed tests
    uint32_t tests_failed;                          ///< Number of failed tests
    uint32_t tests_skipped;                         ///< Number of skipped tests
    uint32_t tests_timeout;                         ///< Number of timed out tests
    uint32_t tests_error;                           ///< Number of error tests
    uint64_t total_execution_time_us;               ///< Total execution time
} test_suite_t;

/**
 * @brief Test framework statistics
 */
typedef struct {
    uint32_t total_suites;                          ///< Total number of suites
    uint32_t total_tests;                           ///< Total number of tests
    uint32_t suites_passed;                         ///< Suites with all tests passed
    uint32_t suites_failed;                         ///< Suites with failed tests
    uint32_t tests_passed;                          ///< Total passed tests
    uint32_t tests_failed;                          ///< Total failed tests
    uint32_t tests_skipped;                         ///< Total skipped tests
    uint32_t tests_timeout;                         ///< Total timed out tests
    uint32_t tests_error;                           ///< Total error tests
    uint64_t total_execution_time_us;               ///< Total framework execution time
    bool framework_initialized;                     ///< Framework initialization status
    bool all_critical_passed;                      ///< All critical tests passed
} test_framework_stats_t;

/**
 * @brief Test framework configuration
 */
typedef struct {
    bool abort_on_critical_failure;                 ///< Abort testing on critical failure
    bool abort_on_high_failure;                     ///< Abort testing on high severity failure
    bool skip_stress_tests;                         ///< Skip stress tests during init
    bool skip_fault_tests;                          ///< Skip fault injection tests
    bool verbose_output;                            ///< Enable verbose test output
    uint32_t default_timeout_ms;                    ///< Default test timeout
    test_category_t enabled_categories;             ///< Enabled test categories (bitmask)
} test_framework_config_t;

// Test assertion macros
#define TEST_ASSERT(condition, ctx, format, ...) \
    do { \
        if (!(condition)) { \
            snprintf((ctx)->user_data, TEST_MAX_DESCRIPTION_LEN, \
                    "Assertion failed: " format, ##__VA_ARGS__); \
            return TEST_RESULT_FAIL; \
        } \
    } while (0)

#define TEST_ASSERT_EQUAL(expected, actual, ctx, format, ...) \
    TEST_ASSERT((expected) == (actual), ctx, \
               format " (expected: %ld, actual: %ld)", \
               ##__VA_ARGS__, (long)(expected), (long)(actual))

#define TEST_ASSERT_NOT_EQUAL(expected, actual, ctx, format, ...) \
    TEST_ASSERT((expected) != (actual), ctx, \
               format " (unexpected value: %ld)", \
               ##__VA_ARGS__, (long)(actual))

#define TEST_ASSERT_NULL(ptr, ctx, format, ...) \
    TEST_ASSERT((ptr) == NULL, ctx, format, ##__VA_ARGS__)

#define TEST_ASSERT_NOT_NULL(ptr, ctx, format, ...) \
    TEST_ASSERT((ptr) != NULL, ctx, format, ##__VA_ARGS__)

#define TEST_ASSERT_TRUE(condition, ctx, format, ...) \
    TEST_ASSERT((condition) == true, ctx, format, ##__VA_ARGS__)

#define TEST_ASSERT_FALSE(condition, ctx, format, ...) \
    TEST_ASSERT((condition) == false, ctx, format, ##__VA_ARGS__)

// Test framework API

/**
 * @brief Initialize the test framework
 * 
 * @param config Framework configuration (NULL for defaults)
 * @return true if initialization successful, false otherwise
 */
bool test_framework_init(const test_framework_config_t* config);

/**
 * @brief Register a test suite
 * 
 * @param suite Test suite to register
 * @return true if registration successful, false otherwise
 */
bool test_framework_register_suite(test_suite_t* suite);

/**
 * @brief Add a test case to a suite
 * 
 * @param suite Test suite
 * @param name Test name
 * @param description Test description
 * @param function Test function
 * @param severity Test severity
 * @param category Test category
 * @param timeout_ms Test timeout (0 for default)
 * @return true if test added successfully, false otherwise
 */
bool test_suite_add_test(test_suite_t* suite,
                        const char* name,
                        const char* description,
                        test_function_t function,
                        test_severity_t severity,
                        test_category_t category,
                        uint32_t timeout_ms);

/**
 * @brief Run all registered test suites
 * 
 * @return true if all critical tests passed, false otherwise
 */
bool test_framework_run_all(void);

/**
 * @brief Run a specific test suite
 * 
 * @param suite_name Name of the suite to run
 * @return true if all critical tests in suite passed, false otherwise
 */
bool test_framework_run_suite(const char* suite_name);

/**
 * @brief Run a specific test case
 * 
 * @param suite_name Name of the test suite
 * @param test_name Name of the test case
 * @return Test result
 */
test_result_t test_framework_run_test(const char* suite_name, const char* test_name);

/**
 * @brief Get framework statistics
 * 
 * @param stats Output statistics structure
 * @return true if successful, false otherwise
 */
bool test_framework_get_stats(test_framework_stats_t* stats);

/**
 * @brief Print test results summary
 * 
 * @param verbose Include detailed test information
 */
void test_framework_print_results(bool verbose);

/**
 * @brief Enable or disable a test category
 * 
 * @param category Test category to enable/disable
 * @param enabled Enable flag
 * @return true if successful, false otherwise
 */
bool test_framework_enable_category(test_category_t category, bool enabled);

/**
 * @brief Enable or disable a test suite
 * 
 * @param suite_name Name of the suite
 * @param enabled Enable flag
 * @return true if successful, false otherwise
 */
bool test_framework_enable_suite(const char* suite_name, bool enabled);

/**
 * @brief Enable or disable a specific test
 * 
 * @param suite_name Name of the test suite
 * @param test_name Name of the test case
 * @param enabled Enable flag
 * @return true if successful, false otherwise
 */
bool test_framework_enable_test(const char* suite_name, const char* test_name, bool enabled);

/**
 * @brief Get default framework configuration
 * 
 * @param config Output configuration structure
 */
void test_framework_get_default_config(test_framework_config_t* config);

/**
 * @brief Check if all critical tests have passed
 * 
 * @return true if all critical tests passed, false otherwise
 */
bool test_framework_all_critical_passed(void);

/**
 * @brief Clean up test framework resources
 */
void test_framework_cleanup(void);

// Test utility functions

/**
 * @brief Create a new test suite
 * 
 * @param name Suite name
 * @param description Suite description
 * @return Pointer to created suite, or NULL on failure
 */
test_suite_t* test_create_suite(const char* name, const char* description);

/**
 * @brief Get string representation of test result
 * 
 * @param result Test result
 * @return String representation
 */
const char* test_result_to_string(test_result_t result);

/**
 * @brief Get string representation of test severity
 * 
 * @param severity Test severity
 * @return String representation
 */
const char* test_severity_to_string(test_severity_t severity);

/**
 * @brief Get string representation of test category
 * 
 * @param category Test category
 * @return String representation
 */
const char* test_category_to_string(test_category_t category);

// Component-specific test suite registration functions
// These are implemented in their respective test files

/**
 * @brief Register scheduler test suite
 * @return true if successful, false otherwise
 */
bool test_scheduler_register_suite(void);

/**
 * @brief Register MPU test suite
 * @return true if successful, false otherwise
 */
bool test_mpu_register_suite(void);

/**
 * @brief Register TrustZone test suite
 * @return true if successful, false otherwise
 */
bool test_tz_register_suite(void);

#ifdef __cplusplus
}
#endif

#endif // TEST_FRAMEWORK_H