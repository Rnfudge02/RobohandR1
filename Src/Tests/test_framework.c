/**
 * @file test_framework.c
 * @brief Runtime testing framework implementation
 * @author System Test Framework
 * @date 2025-05-26
 */

#include "test_framework.h"
#include "log_manager.h"
#include "spinlock_manager.h"
#include "scheduler.h"

#include "pico/time.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Framework state
static test_suite_t* registered_suites[TEST_MAX_SUITES];
static uint32_t num_registered_suites = 0;
static test_framework_config_t framework_config;
static test_framework_stats_t framework_stats;
static bool framework_initialized = false;
static uint32_t test_spinlock_num;

// Current test context for timeout handling
static volatile test_context_t* current_test_context = NULL;
static volatile bool test_timeout_occurred = false;

// Forward declarations
static void test_timeout_handler(void);
static bool execute_test_case(test_suite_t* suite, test_case_t* test_case);
static void update_suite_stats(test_suite_t* suite);
static void update_framework_stats(void);
static test_suite_t* find_suite(const char* suite_name);
static test_case_t* find_test(test_suite_t* suite, const char* test_name);

/**
 * @brief Initialize the test framework
 */
bool test_framework_init(const test_framework_config_t* config) {
    if (framework_initialized) {
        return true;
    }
    
    // Allocate spinlock for thread safety
    test_spinlock_num = hw_spinlock_allocate(SPINLOCK_CAT_TEST, "test_framework");
    
    // Initialize framework state
    memset(registered_suites, 0, sizeof(registered_suites));
    num_registered_suites = 0;
    memset(&framework_stats, 0, sizeof(framework_stats));
    
    // Set configuration
    if (config) {
        memcpy(&framework_config, config, sizeof(test_framework_config_t));
    } else {
        test_framework_get_default_config(&framework_config);
    }
    
    framework_initialized = true;
    framework_stats.framework_initialized = true;
    
    log_message(LOG_LEVEL_INFO, "Test Framework", "Test framework initialized");
    
    return true;
}

/**
 * @brief Get default framework configuration
 */
void test_framework_get_default_config(test_framework_config_t* config) {
    if (!config) return;
    
    memset(config, 0, sizeof(test_framework_config_t));
    
    config->abort_on_critical_failure = true;
    config->abort_on_high_failure = false;
    config->skip_stress_tests = false;
    config->skip_fault_tests = false;
    config->verbose_output = true;
    config->default_timeout_ms = TEST_TIMEOUT_DEFAULT_MS;
    config->enabled_categories = 0xFF; // All categories enabled
}

/**
 * @brief Register a test suite
 */
bool test_framework_register_suite(test_suite_t* suite) {
    if (!framework_initialized || !suite || num_registered_suites >= TEST_MAX_SUITES) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(test_spinlock_num, scheduler_get_current_task());
    
    // Check for duplicate suite names
    for (uint32_t i = 0; i < num_registered_suites; i++) {
        if (strcmp(registered_suites[i]->name, suite->name) == 0) {
            hw_spinlock_release(test_spinlock_num, owner_irq);
            return false;
        }
    }
    
    // Register the suite
    registered_suites[num_registered_suites++] = suite;
    framework_stats.total_suites++;
    framework_stats.total_tests += suite->test_count;
    
    hw_spinlock_release(test_spinlock_num, owner_irq);
    
    log_message(LOG_LEVEL_INFO, "Test Framework", "Registered test suite '%s' with %lu tests", 
                suite->name, suite->test_count);
    
    return true;
}

/**
 * @brief Add a test case to a suite
 */
bool test_suite_add_test(test_suite_t* suite,
                        const char* name,
                        const char* description,
                        test_function_t function,
                        test_severity_t severity,
                        test_category_t category,
                        uint32_t timeout_ms) {
    if (!suite || !name || !function || suite->test_count >= TEST_MAX_TESTS_PER_SUITE) {
        return false;
    }
    
    test_case_t* test_case = &suite->tests[suite->test_count];
    
    // Initialize test case
    memset(test_case, 0, sizeof(test_case_t));
    strncpy(test_case->name, name, TEST_MAX_NAME_LEN - 1);
    if (description) {
        strncpy(test_case->description, description, TEST_MAX_DESCRIPTION_LEN - 1);
    }
    
    test_case->function = function;
    test_case->severity = severity;
    test_case->category = category;
    test_case->timeout_ms = timeout_ms > 0 ? timeout_ms : framework_config.default_timeout_ms;
    test_case->enabled = true;
    test_case->result = TEST_RESULT_NOT_RUN;
    test_case->execution_time_us = 0;
    
    suite->test_count++;
    
    return true;
}

/**
 * @brief Run all registered test suites
 */
bool test_framework_run_all(void) {
    if (!framework_initialized) {
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "Test Framework", "Starting test execution for %lu suites", num_registered_suites);
    
    uint64_t start_time = time_us_64();
    bool all_critical_passed = true;
    
    // Reset framework statistics
    framework_stats.suites_passed = 0;
    framework_stats.suites_failed = 0;
    framework_stats.tests_passed = 0;
    framework_stats.tests_failed = 0;
    framework_stats.tests_skipped = 0;
    framework_stats.tests_timeout = 0;
    framework_stats.tests_error = 0;
    
    // Run each suite
    for (uint32_t i = 0; i < num_registered_suites; i++) {
        test_suite_t* suite = registered_suites[i];
        
        if (!suite->enabled) {
            log_message(LOG_LEVEL_INFO, "Test Framework", "Skipping disabled suite '%s'", suite->name);
            continue;
        }
        
        log_message(LOG_LEVEL_INFO, "Test Framework", "Running test suite '%s'", suite->name);
        
        bool suite_critical_passed = test_framework_run_suite(suite->name);
        if (!suite_critical_passed) {
            all_critical_passed = false;
            
            if (framework_config.abort_on_critical_failure) {
                log_message(LOG_LEVEL_ERROR, "Test Framework", 
                           "Critical test failure in suite '%s', aborting test execution", suite->name);
                break;
            }
        }
    }
    
    // Update final statistics
    update_framework_stats();
    framework_stats.total_execution_time_us = time_us_64() - start_time;
    framework_stats.all_critical_passed = all_critical_passed;
    
    log_message(LOG_LEVEL_INFO, "Test Framework", "Test execution completed in %llu ms", 
                framework_stats.total_execution_time_us / 1000);
    
    // Print results if verbose
    if (framework_config.verbose_output) {
        test_framework_print_results(true);
    }
    
    return all_critical_passed;
}

/**
 * @brief Run a specific test suite
 */
bool test_framework_run_suite(const char* suite_name) {
    if (!framework_initialized || !suite_name) {
        return false;
    }
    
    test_suite_t* suite = find_suite(suite_name);
    if (!suite) {
        log_message(LOG_LEVEL_ERROR, "Test Framework", "Test suite '%s' not found", suite_name);
        return false;
    }
    
    if (!suite->enabled) {
        log_message(LOG_LEVEL_INFO, "Test Framework", "Test suite '%s' is disabled", suite_name);
        return true;
    }
    
    // Reset suite statistics
    suite->tests_passed = 0;
    suite->tests_failed = 0;
    suite->tests_skipped = 0;
    suite->tests_timeout = 0;
    suite->tests_error = 0;
    suite->total_execution_time_us = 0;
    
    uint64_t suite_start_time = time_us_64();
    bool critical_passed = true;
    
    // Run each test in the suite
    for (uint32_t i = 0; i < suite->test_count; i++) {
        test_case_t* test_case = &suite->tests[i];
        
        if (!test_case->enabled) {
            test_case->result = TEST_RESULT_SKIP;
            suite->tests_skipped++;
            continue;
        }
        
        // Check if this category is enabled
        if (!(framework_config.enabled_categories & (1 << test_case->category))) {
            test_case->result = TEST_RESULT_SKIP;
            suite->tests_skipped++;
            continue;
        }
        
        // Skip certain categories based on configuration
        if ((test_case->category == TEST_CATEGORY_STRESS && framework_config.skip_stress_tests) ||
            (test_case->category == TEST_CATEGORY_FAULT && framework_config.skip_fault_tests)) {
            test_case->result = TEST_RESULT_SKIP;
            suite->tests_skipped++;
            continue;
        }
        
        // Execute the test
        bool test_passed = execute_test_case(suite, test_case);
        
        // Check for critical test failures
        if (!test_passed && test_case->severity == TEST_SEVERITY_CRITICAL) {
            critical_passed = false;
            
            if (framework_config.abort_on_critical_failure) {
                log_message(LOG_LEVEL_ERROR, "Test Framework", 
                           "Critical test '%s' failed, aborting suite execution", test_case->name);
                break;
            }
        }
        
        // Check for high severity test failures
        if (!test_passed && test_case->severity == TEST_SEVERITY_HIGH && 
            framework_config.abort_on_high_failure) {
            log_message(LOG_LEVEL_WARN, "Test Framework", 
                       "High severity test '%s' failed, aborting suite execution", test_case->name);
            break;
        }
    }
    
    suite->total_execution_time_us = time_us_64() - suite_start_time;
    update_suite_stats(suite);
    
    log_message(LOG_LEVEL_INFO, "Test Framework", 
               "Suite '%s' completed: %lu passed, %lu failed, %lu skipped", 
               suite->name, suite->tests_passed, suite->tests_failed, suite->tests_skipped);
    
    return critical_passed;
}

/**
 * @brief Execute a single test case
 */
static bool execute_test_case(test_suite_t* suite, test_case_t* test_case) {
    if (!suite || !test_case || !test_case->function) {
        return false;
    }
    
    log_message(LOG_LEVEL_DEBUG, "Test Framework", "Running test '%s'", test_case->name);
    
    // Prepare test context
    test_context_t context = {0};
    context.test_id = (uint32_t)test_case; // Use address as unique ID
    context.test_name = test_case->name;
    context.suite_name = suite->name;
    context.timeout_ms = test_case->timeout_ms;
    context.severity = test_case->severity;
    context.category = test_case->category;
    context.user_data = test_case->failure_reason; // Use failure_reason as user data buffer
    
    // Clear previous failure reason
    memset(test_case->failure_reason, 0, sizeof(test_case->failure_reason));
    
    // Set up timeout handling
    current_test_context = &context;
    test_timeout_occurred = false;
    
    // Record start time
    context.start_time_us = time_us_64();
    
    // Execute the test function
    test_result_t result = test_case->function(&context);
    
    // Record end time
    context.end_time_us = time_us_64();
    test_case->execution_time_us = context.end_time_us - context.start_time_us;
    
    // Clear timeout handling
    current_test_context = NULL;
    
    // Check for timeout
    if (test_timeout_occurred || test_case->execution_time_us > (test_case->timeout_ms * 1000ULL)) {
        result = TEST_RESULT_TIMEOUT;
        snprintf(test_case->failure_reason, sizeof(test_case->failure_reason),
                "Test timed out after %llu ms", test_case->execution_time_us / 1000);
    }
    
    // Store result
    test_case->result = result;
    
    // Update suite statistics
    switch (result) {
        case TEST_RESULT_PASS:
            suite->tests_passed++;
            if (framework_config.verbose_output) {
                log_message(LOG_LEVEL_INFO, "Test Framework", "✓ %s PASSED (%llu μs)", 
                           test_case->name, test_case->execution_time_us);
            }
            break;
            
        case TEST_RESULT_FAIL:
            suite->tests_failed++;
            log_message(LOG_LEVEL_WARN, "Test Framework", "✗ %s FAILED: %s (%llu μs)", 
                       test_case->name, test_case->failure_reason, test_case->execution_time_us);
            break;
            
        case TEST_RESULT_SKIP:
            suite->tests_skipped++;
            if (framework_config.verbose_output) {
                log_message(LOG_LEVEL_INFO, "Test Framework", "- %s SKIPPED", test_case->name);
            }
            break;
            
        case TEST_RESULT_TIMEOUT:
            suite->tests_timeout++;
            log_message(LOG_LEVEL_ERROR, "Test Framework", "⏱ %s TIMEOUT: %s", 
                       test_case->name, test_case->failure_reason);
            break;
            
        case TEST_RESULT_ERROR:
            suite->tests_error++;
            log_message(LOG_LEVEL_ERROR, "Test Framework", "! %s ERROR: %s (%llu μs)", 
                       test_case->name, test_case->failure_reason, test_case->execution_time_us);
            break;
            
        default:
            suite->tests_error++;
            log_message(LOG_LEVEL_ERROR, "Test Framework", "? %s UNKNOWN RESULT", test_case->name);
            break;
    }
    
    return (result == TEST_RESULT_PASS);
}

/**
 * @brief Update suite statistics
 */
static void update_suite_stats(test_suite_t* suite) {
    if (!suite) return;
    
    // A suite passes if all tests pass or are skipped (no failures, timeouts, or errors)
    if (suite->tests_failed == 0 && suite->tests_timeout == 0 && suite->tests_error == 0) {
        framework_stats.suites_passed++;
    } else {
        framework_stats.suites_failed++;
    }
}

/**
 * @brief Update framework statistics
 */
static void update_framework_stats(void) {
    framework_stats.tests_passed = 0;
    framework_stats.tests_failed = 0;
    framework_stats.tests_skipped = 0;
    framework_stats.tests_timeout = 0;
    framework_stats.tests_error = 0;
    
    for (uint32_t i = 0; i < num_registered_suites; i++) {
        test_suite_t* suite = registered_suites[i];
        framework_stats.tests_passed += suite->tests_passed;
        framework_stats.tests_failed += suite->tests_failed;
        framework_stats.tests_skipped += suite->tests_skipped;
        framework_stats.tests_timeout += suite->tests_timeout;
        framework_stats.tests_error += suite->tests_error;
    }
}

/**
 * @brief Find a test suite by name
 */
static test_suite_t* find_suite(const char* suite_name) {
    if (!suite_name) return NULL;
    
    for (uint32_t i = 0; i < num_registered_suites; i++) {
        if (strcmp(registered_suites[i]->name, suite_name) == 0) {
            return registered_suites[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Find a test case by name in a suite
 */
static test_case_t* find_test(test_suite_t* suite, const char* test_name) {
    if (!suite || !test_name) return NULL;
    
    for (uint32_t i = 0; i < suite->test_count; i++) {
        if (strcmp(suite->tests[i].name, test_name) == 0) {
            return &suite->tests[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Run a specific test case
 */
test_result_t test_framework_run_test(const char* suite_name, const char* test_name) {
    if (!framework_initialized || !suite_name || !test_name) {
        return TEST_RESULT_ERROR;
    }
    
    test_suite_t* suite = find_suite(suite_name);
    if (!suite) {
        return TEST_RESULT_ERROR;
    }
    
    test_case_t* test_case = find_test(suite, test_name);
    if (!test_case) {
        return TEST_RESULT_ERROR;
    }
    
    execute_test_case(suite, test_case);
    return test_case->result;
}

/**
 * @brief Get framework statistics
 */
bool test_framework_get_stats(test_framework_stats_t* stats) {
    if (!framework_initialized || !stats) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(test_spinlock_num, scheduler_get_current_task());
    memcpy(stats, &framework_stats, sizeof(test_framework_stats_t));
    hw_spinlock_release(test_spinlock_num, owner_irq);
    
    return true;
}

/**
 * @brief Print test results summary
 */
void test_framework_print_results(bool verbose) {
    if (!framework_initialized) {
        return;
    }
    
    printf("\n");
    printf("================== TEST RESULTS SUMMARY ==================\n");
    printf("Total Suites: %lu (Passed: %lu, Failed: %lu)\n", 
           framework_stats.total_suites, framework_stats.suites_passed, framework_stats.suites_failed);
    printf("Total Tests:  %lu (Passed: %lu, Failed: %lu, Skipped: %lu, Timeout: %lu, Error: %lu)\n",
           framework_stats.total_tests, framework_stats.tests_passed, framework_stats.tests_failed,
           framework_stats.tests_skipped, framework_stats.tests_timeout, framework_stats.tests_error);
    printf("Execution Time: %llu ms\n", framework_stats.total_execution_time_us / 1000);
    printf("Critical Tests: %s\n", framework_stats.all_critical_passed ? "PASSED" : "FAILED");
    printf("==========================================================\n");
    
    if (verbose) {
        for (uint32_t i = 0; i < num_registered_suites; i++) {
            test_suite_t* suite = registered_suites[i];
            
            printf("\nSuite: %s\n", suite->name);
            printf("  Description: %s\n", suite->description);
            printf("  Tests: %lu (Passed: %lu, Failed: %lu, Skipped: %lu)\n",
                   suite->test_count, suite->tests_passed, suite->tests_failed, suite->tests_skipped);
            printf("  Execution Time: %llu ms\n", suite->total_execution_time_us / 1000);
            
            for (uint32_t j = 0; j < suite->test_count; j++) {
                test_case_t* test = &suite->tests[j];
                
                const char* status;
                switch (test->result) {
                    case TEST_RESULT_PASS: status = "PASS"; break;
                    case TEST_RESULT_FAIL: status = "FAIL"; break;
                    case TEST_RESULT_SKIP: status = "SKIP"; break;
                    case TEST_RESULT_TIMEOUT: status = "TIMEOUT"; break;
                    case TEST_RESULT_ERROR: status = "ERROR"; break;
                    default: status = "NOT_RUN"; break;
                }
                
                printf("    %-30s [%s] %s (%llu μs)\n", 
                       test->name, 
                       test_severity_to_string(test->severity),
                       status,
                       test->execution_time_us);
                
                if (test->result != TEST_RESULT_PASS && test->result != TEST_RESULT_SKIP && 
                    strlen(test->failure_reason) > 0) {
                    printf("      Reason: %s\n", test->failure_reason);
                }
            }
        }
    }
    
    printf("\n");
}

/**
 * @brief Enable or disable a test suite
 */
bool test_framework_enable_suite(const char* suite_name, bool enabled) {
    test_suite_t* suite = find_suite(suite_name);
    if (!suite) {
        return false;
    }
    
    suite->enabled = enabled;
    return true;
}

/**
 * @brief Enable or disable a specific test
 */
bool test_framework_enable_test(const char* suite_name, const char* test_name, bool enabled) {
    test_suite_t* suite = find_suite(suite_name);
    if (!suite) {
        return false;
    }
    
    test_case_t* test_case = find_test(suite, test_name);
    if (!test_case) {
        return false;
    }
    
    test_case->enabled = enabled;
    return true;
}

/**
 * @brief Check if all critical tests have passed
 */
bool test_framework_all_critical_passed(void) {
    return framework_stats.all_critical_passed;
}

/**
 * @brief Clean up test framework resources
 */
void test_framework_cleanup(void) {
    if (!framework_initialized) {
        return;
    }
    
    hw_spinlock_free(test_spinlock_num);
    
    memset(registered_suites, 0, sizeof(registered_suites));
    num_registered_suites = 0;
    framework_initialized = false;
    
    log_message(LOG_LEVEL_INFO, "Test Framework", "Test framework cleaned up");
}

/**
 * @brief Enable or disable a test category
 */
bool test_framework_enable_category(test_category_t category, bool enabled) {
    if (!framework_initialized || category >= 32) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(test_spinlock_num, scheduler_get_current_task());
    
    if (enabled) {
        framework_config.enabled_categories |= (1 << category);
    } else {
        framework_config.enabled_categories &= ~(1 << category);
    }
    
    hw_spinlock_release(test_spinlock_num, owner_irq);
    
    return true;
}

/**
 * @brief Create a new test suite
 */
test_suite_t* test_create_suite(const char* name, const char* description) {
    if (!name) return NULL;
    
    test_suite_t* suite = malloc(sizeof(test_suite_t));
    if (!suite) return NULL;
    
    memset(suite, 0, sizeof(test_suite_t));
    strncpy(suite->name, name, TEST_MAX_NAME_LEN - 1);
    if (description) {
        strncpy(suite->description, description, TEST_MAX_DESCRIPTION_LEN - 1);
    }
    suite->enabled = true;
    
    return suite;
}

/**
 * @brief Get string representation of test result
 */
const char* test_result_to_string(test_result_t result) {
    switch (result) {
        case TEST_RESULT_PASS: return "PASS";
        case TEST_RESULT_FAIL: return "FAIL";
        case TEST_RESULT_SKIP: return "SKIP";
        case TEST_RESULT_TIMEOUT: return "TIMEOUT";
        case TEST_RESULT_ERROR: return "ERROR";
        case TEST_RESULT_NOT_RUN: return "NOT_RUN";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get string representation of test severity
 */
const char* test_severity_to_string(test_severity_t severity) {
    switch (severity) {
        case TEST_SEVERITY_CRITICAL: return "CRITICAL";
        case TEST_SEVERITY_HIGH: return "HIGH";
        case TEST_SEVERITY_MEDIUM: return "MEDIUM";
        case TEST_SEVERITY_LOW: return "LOW";
        case TEST_SEVERITY_INFO: return "INFO";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get string representation of test category
 */
const char* test_category_to_string(test_category_t category) {
    switch (category) {
        case TEST_CATEGORY_UNIT: return "UNIT";
        case TEST_CATEGORY_INTEGRATION: return "INTEGRATION";
        case TEST_CATEGORY_STRESS: return "STRESS";
        case TEST_CATEGORY_FAULT: return "FAULT";
        case TEST_CATEGORY_SECURITY: return "SECURITY";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Timeout handler (placeholder for future implementation)
 */
static void test_timeout_handler(void) {
    test_timeout_occurred = true;
}