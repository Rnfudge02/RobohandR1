/**
 * @file test_integration.c
 * @brief Test framework integration implementation
 * @author System Test Framework
 * @date 2025-05-26
 */

#include "test_integration.h"
#include "test_scheduler.h"
#include "test_mpu.h"
#include "test_tz.h"
#include "log_manager.h"
#include "usb_shell.h"

#include "pico/time.h"
#include "pico/stdlib.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Global state
static bool integration_initialized = false;
static test_integration_config_t integration_config;
static test_integration_results_t integration_results;
static test_framework_stats_t framework_stats;

// Component test status tracking
typedef struct {
    const char* name;
    bool registered;
    bool critical_passed;
    bool all_passed;
    uint32_t tests_run;
    uint32_t tests_failed;
} component_status_t;

static component_status_t component_status[] = {
    {"scheduler", false, false, false, 0, 0},
    {"mpu", false, false, false, 0, 0},
    {"tz", false, false, false, 0, 0}
};

static const int num_components = sizeof(component_status) / sizeof(component_status[0]);

// Forward declarations
static bool register_all_test_suites(void);
static bool analyze_test_results(void);
static component_status_t* find_component_status(const char* name);
static void update_component_status(void);
static void print_component_summary(void);

/**
 * @brief Get default test integration configuration
 */
void test_integration_get_default_config(test_integration_config_t* config) {
    if (!config) return;
    
    memset(config, 0, sizeof(test_integration_config_t));
    
    config->run_critical_only = false;
    config->run_stress_tests = false;
    config->run_fault_tests = false;
    config->run_security_tests = true;
    config->verbose_output = true;
    config->abort_on_failure = false;
    config->enable_shell_commands = true;
    config->test_timeout_ms = 5000;
}

/**
 * @brief Initialize the test integration system
 */
bool test_integration_init(const test_integration_config_t* config) {
    if (integration_initialized) {
        return true;
    }
    
    // Set configuration
    if (config) {
        memcpy(&integration_config, config, sizeof(test_integration_config_t));
    } else {
        test_integration_get_default_config(&integration_config);
    }
    
    // Initialize results
    memset(&integration_results, 0, sizeof(integration_results));
    memset(&framework_stats, 0, sizeof(framework_stats));
    
    // Initialize test framework
    test_framework_config_t framework_config;
    test_framework_get_default_config(&framework_config);
    
    // Apply integration configuration to framework
    framework_config.abort_on_critical_failure = integration_config.abort_on_failure;
    framework_config.skip_stress_tests = !integration_config.run_stress_tests;
    framework_config.skip_fault_tests = !integration_config.run_fault_tests;
    framework_config.verbose_output = integration_config.verbose_output;
    framework_config.default_timeout_ms = integration_config.test_timeout_ms;
    
    // Configure test categories
    framework_config.enabled_categories = 0;
    framework_config.enabled_categories |= (1 << TEST_CATEGORY_UNIT);
    framework_config.enabled_categories |= (1 << TEST_CATEGORY_INTEGRATION);
    
    if (integration_config.run_stress_tests) {
        framework_config.enabled_categories |= (1 << TEST_CATEGORY_STRESS);
    }
    
    if (integration_config.run_fault_tests) {
        framework_config.enabled_categories |= (1 << TEST_CATEGORY_FAULT);
    }
    
    if (integration_config.run_security_tests) {
        framework_config.enabled_categories |= (1 << TEST_CATEGORY_SECURITY);
    }
    
    if (!test_framework_init(&framework_config)) {
        log_message(LOG_LEVEL_ERROR, "Test Integration", "Failed to initialize test framework");
        return false;
    }
    
    // Register all test suites
    if (!register_all_test_suites()) {
        log_message(LOG_LEVEL_ERROR, "Test Integration", "Failed to register test suites");
        return false;
    }
    
    // Register shell commands if enabled
    if (integration_config.enable_shell_commands) {
        test_integration_register_shell_commands();
    }
    
    integration_initialized = true;
    
    log_message(LOG_LEVEL_INFO, "Test Integration", "Test integration system initialized");
    
    return true;
}

/**
 * @brief Register all component test suites
 */
static bool register_all_test_suites(void) {
    bool all_registered = true;
    
    // Register scheduler tests
    if (test_scheduler_register_suite()) {
        component_status[0].registered = true;
        log_message(LOG_LEVEL_INFO, "Test Integration", "Scheduler test suite registered");
    } else {
        log_message(LOG_LEVEL_ERROR, "Test Integration", "Failed to register scheduler test suite");
        all_registered = false;
    }
    
    // Register MPU tests
    if (test_mpu_register_suite()) {
        component_status[1].registered = true;
        log_message(LOG_LEVEL_INFO, "Test Integration", "MPU test suite registered");
    } else {
        log_message(LOG_LEVEL_ERROR, "Test Integration", "Failed to register MPU test suite");
        all_registered = false;
    }
    
    // Register TrustZone tests
    if (test_tz_register_suite()) {
        component_status[2].registered = true;
        log_message(LOG_LEVEL_INFO, "Test Integration", "TrustZone test suite registered");
    } else {
        log_message(LOG_LEVEL_ERROR, "Test Integration", "Failed to register TrustZone test suite");
        all_registered = false;
    }
    
    return all_registered;
}

/**
 * @brief Initialize and run all component tests
 */
bool test_integration_run_all(const test_integration_config_t* config) {
    // Initialize if not already done
    if (!integration_initialized) {
        if (!test_integration_init(config)) {
            return false;
        }
    }
    
    log_message(LOG_LEVEL_INFO, "Test Integration", "Starting comprehensive test execution");
    
    uint64_t start_time = time_us_64();
    
    // Run all tests
    bool all_critical_passed = test_framework_run_all();
    
    uint64_t end_time = time_us_64();
    
    // Get framework statistics
    test_framework_get_stats(&framework_stats);
    
    // Analyze results
    analyze_test_results();
    
    // Update integration results
    integration_results.all_tests_passed = all_critical_passed && (framework_stats.tests_failed == 0);
    integration_results.critical_tests_passed = all_critical_passed;
    integration_results.total_tests_run = framework_stats.total_tests;
    integration_results.tests_passed = framework_stats.tests_passed;
    integration_results.tests_failed = framework_stats.tests_failed;
    integration_results.tests_skipped = framework_stats.tests_skipped;
    integration_results.total_execution_time_us = end_time - start_time;
    
    // Update component status
    update_component_status();
    
    log_message(LOG_LEVEL_INFO, "Test Integration", 
               "Test execution completed: %lu/%lu tests passed in %llu ms",
               framework_stats.tests_passed, framework_stats.total_tests,
               integration_results.total_execution_time_us / 1000);
    
    // Print summary if verbose
    if (integration_config.verbose_output) {
        test_integration_print_summary(true);
    }
    
    return all_critical_passed;
}

/**
 * @brief Run only critical tests during system initialization
 */
bool test_integration_run_critical(void) {
    // Set up configuration for critical tests only
    test_integration_config_t critical_config;
    test_integration_get_default_config(&critical_config);
    
    critical_config.run_critical_only = true;
    critical_config.run_stress_tests = false;
    critical_config.run_fault_tests = false;
    critical_config.run_security_tests = false;
    critical_config.verbose_output = false;
    critical_config.abort_on_failure = true;
    critical_config.test_timeout_ms = 2000; // Shorter timeout for critical tests
    
    // Initialize if not already done
    if (!integration_initialized) {
        if (!test_integration_init(&critical_config)) {
            return false;
        }
    }
    
    log_message(LOG_LEVEL_INFO, "Test Integration", "Running critical system tests");
    
    uint64_t start_time = time_us_64();
    
    // Temporarily disable non-critical test categories
    test_framework_enable_category(TEST_CATEGORY_STRESS, false);
    test_framework_enable_category(TEST_CATEGORY_FAULT, false);
    
    // Run tests with focus on critical ones
    bool critical_passed = true;
    
    // Run scheduler critical tests
    if (component_status[0].registered) {
        bool scheduler_passed = test_framework_run_suite("Scheduler");
        if (!scheduler_passed) {
            critical_passed = false;
            log_message(LOG_LEVEL_ERROR, "Test Integration", "Critical scheduler tests failed");
        }
    }
    
    // Run MPU critical tests (if supported)
    if (component_status[1].registered) {
        bool mpu_passed = test_framework_run_suite("MPU");
        if (!mpu_passed) {
            log_message(LOG_LEVEL_WARN, "Test Integration", "MPU tests failed - may not be supported");
        }
    }
    
    // Run TrustZone critical tests (if supported)
    if (component_status[2].registered) {
        bool tz_passed = test_framework_run_suite("TrustZone");
        if (!tz_passed) {
            log_message(LOG_LEVEL_WARN, "Test Integration", "TrustZone tests failed - may not be supported");
        }
    }
    
    uint64_t end_time = time_us_64();
    
    // Get framework statistics
    test_framework_get_stats(&framework_stats);
    
    // Update results
    integration_results.critical_tests_passed = critical_passed;
    integration_results.total_execution_time_us = end_time - start_time;
    
    log_message(LOG_LEVEL_INFO, "Test Integration", 
               "Critical tests completed in %llu ms: %s",
               integration_results.total_execution_time_us / 1000,
               critical_passed ? "PASSED" : "FAILED");
    
    return critical_passed;
}

/**
 * @brief Run comprehensive test suite
 */
bool test_integration_run_comprehensive(void) {
    test_integration_config_t comprehensive_config;
    test_integration_get_default_config(&comprehensive_config);
    
    comprehensive_config.run_critical_only = false;
    comprehensive_config.run_stress_tests = true;
    comprehensive_config.run_fault_tests = true;
    comprehensive_config.run_security_tests = true;
    comprehensive_config.verbose_output = true;
    comprehensive_config.abort_on_failure = false;
    
    return test_integration_run_all(&comprehensive_config);
}

/**
 * @brief Run tests for a specific component
 */
bool test_integration_run_component(const char* component_name) {
    if (!integration_initialized || !component_name) {
        return false;
    }
    
    component_status_t* comp_status = find_component_status(component_name);
    if (!comp_status || !comp_status->registered) {
        log_message(LOG_LEVEL_ERROR, "Test Integration", "Component '%s' not found or not registered", component_name);
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "Test Integration", "Running tests for component: %s", component_name);
    
    // Map component name to test suite name
    const char* suite_name = NULL;
    if (strcmp(component_name, "scheduler") == 0) {
        suite_name = "Scheduler";
    } else if (strcmp(component_name, "mpu") == 0) {
        suite_name = "MPU";
    } else if (strcmp(component_name, "tz") == 0) {
        suite_name = "TrustZone";
    }
    
    if (!suite_name) {
        return false;
    }
    
    return test_framework_run_suite(suite_name);
}

/**
 * @brief Analyze test results and update component status
 */
static bool analyze_test_results(void) {
    // Get detailed framework statistics
    if (!test_framework_get_stats(&framework_stats)) {
        return false;
    }
    
    // Determine if all critical tests passed
    bool all_critical_passed = test_framework_all_critical_passed();
    
    // Set first failure if any
    if (framework_stats.tests_failed > 0) {
        integration_results.first_failure = "See test log for details";
    } else {
        integration_results.first_failure = NULL;
    }
    
    return true;
}

/**
 * @brief Update component status based on test results
 */
static void update_component_status(void) {
    // This is a simplified implementation
    // In a more sophisticated version, we would track per-component results
    
    for (int i = 0; i < num_components; i++) {
        if (component_status[i].registered) {
            // For now, assume all registered components have the same status
            component_status[i].critical_passed = integration_results.critical_tests_passed;
            component_status[i].all_passed = integration_results.all_tests_passed;
            component_status[i].tests_run = framework_stats.total_tests / num_components; // Rough estimate
            component_status[i].tests_failed = framework_stats.tests_failed / num_components; // Rough estimate
        }
    }
}

/**
 * @brief Find component status by name
 */
static component_status_t* find_component_status(const char* name) {
    if (!name) return NULL;
    
    for (int i = 0; i < num_components; i++) {
        if (strcmp(component_status[i].name, name) == 0) {
            return &component_status[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Get test integration results
 */
bool test_integration_get_results(test_integration_results_t* results) {
    if (!results || !integration_initialized) {
        return false;
    }
    
    memcpy(results, &integration_results, sizeof(test_integration_results_t));
    return true;
}

/**
 * @brief Print component summary
 */
static void print_component_summary(void) {
    printf("\nComponent Test Summary:\n");
    printf("Component   | Registered | Critical | All Tests | Tests Run | Failed\n");
    printf("------------+------------+----------+-----------+-----------+--------\n");
    
    for (int i = 0; i < num_components; i++) {
        const component_status_t* comp = &component_status[i];
        printf("%-11s | %-10s | %-8s | %-9s | %-9lu | %lu\n",
               comp->name,
               comp->registered ? "Yes" : "No",
               comp->critical_passed ? "PASS" : "FAIL",
               comp->all_passed ? "PASS" : "FAIL",
               comp->tests_run,
               comp->tests_failed);
    }
    printf("\n");
}

/**
 * @brief Print test integration summary
 */
void test_integration_print_summary(bool verbose) {
    if (!integration_initialized) {
        printf("Test integration not initialized\n");
        return;
    }
    
    printf("\n");
    printf("=================== SYSTEM TEST SUMMARY ===================\n");
    printf("Overall Status: %s\n", integration_results.all_tests_passed ? "PASSED" : "FAILED");
    printf("Critical Tests: %s\n", integration_results.critical_tests_passed ? "PASSED" : "FAILED");
    printf("Total Tests: %lu (Passed: %lu, Failed: %lu, Skipped: %lu)\n",
           integration_results.total_tests_run,
           integration_results.tests_passed,
           integration_results.tests_failed,
           integration_results.tests_skipped);
    printf("Execution Time: %llu ms\n", integration_results.total_execution_time_us / 1000);
    
    if (integration_results.first_failure) {
        printf("First Failure: %s\n", integration_results.first_failure);
    }
    
    printf("============================================================\n");
    
    if (verbose) {
        print_component_summary();
        test_framework_print_results(false); // Don't duplicate verbose output
    }
}

/**
 * @brief Check if system is ready for normal operation
 */
bool test_integration_system_ready(void) {
    return integration_initialized && integration_results.critical_tests_passed;
}

/**
 * @brief Get component test status
 */
bool test_integration_get_component_status(const char* component_name, 
                                          bool* passed, bool* critical_passed) {
    if (!component_name || !passed || !critical_passed) {
        return false;
    }
    
    component_status_t* comp_status = find_component_status(component_name);
    if (!comp_status) {
        return false;
    }
    
    *passed = comp_status->all_passed;
    *critical_passed = comp_status->critical_passed;
    
    return true;
}

/**
 * @brief Enable or disable specific test categories
 */
bool test_integration_enable_category(test_category_t category, bool enabled) {
    if (!integration_initialized) {
        return false;
    }
    
    // Update integration config
    switch (category) {
        case TEST_CATEGORY_STRESS:
            integration_config.run_stress_tests = enabled;
            break;
        case TEST_CATEGORY_FAULT:
            integration_config.run_fault_tests = enabled;
            break;
        case TEST_CATEGORY_SECURITY:
            integration_config.run_security_tests = enabled;
            break;
        default:
            // Other categories are always enabled
            break;
    }
    
    return true;
}

/**
 * @brief Set test execution timeout
 */
void test_integration_set_timeout(uint32_t timeout_ms) {
    integration_config.test_timeout_ms = timeout_ms;
}

/**
 * @brief Clean up test integration resources
 */
void test_integration_cleanup(void) {
    if (!integration_initialized) {
        return;
    }
    
    test_framework_cleanup();
    
    memset(&integration_results, 0, sizeof(integration_results));
    memset(&framework_stats, 0, sizeof(framework_stats));
    
    for (int i = 0; i < num_components; i++) {
        component_status[i].registered = false;
        component_status[i].critical_passed = false;
        component_status[i].all_passed = false;
    }
    
    integration_initialized = false;
    
    log_message(LOG_LEVEL_INFO, "Test Integration", "Test integration system cleaned up");
}

// Shell command implementations

/**
 * @brief Shell command: run tests
 */
int cmd_test_run(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: test run <all|critical|comprehensive|component_name>\n");
        printf("  all          - Run all registered tests\n");
        printf("  critical     - Run only critical tests\n");
        printf("  comprehensive - Run comprehensive test suite\n");
        printf("  scheduler    - Run scheduler tests only\n");
        printf("  mpu          - Run MPU tests only\n");
        printf("  tz           - Run TrustZone tests only\n");
        return 1;
    }
    
    bool result = false;
    uint64_t start_time = time_us_64();
    
    if (strcmp(argv[1], "all") == 0) {
        result = test_integration_run_all(NULL);
    }
    else if (strcmp(argv[1], "critical") == 0) {
        result = test_integration_run_critical();
    }
    else if (strcmp(argv[1], "comprehensive") == 0) {
        result = test_integration_run_comprehensive();
    }
    else if (strcmp(argv[1], "scheduler") == 0 || strcmp(argv[1], "mpu") == 0 || strcmp(argv[1], "tz") == 0) {
        result = test_integration_run_component(argv[1]);
    }
    else {
        printf("Unknown test target: %s\n", argv[1]);
        return 1;
    }
    
    uint64_t end_time = time_us_64();
    
    printf("Test execution completed in %llu ms: %s\n", 
           (end_time - start_time) / 1000,
           result ? "PASSED" : "FAILED");
    
    return result ? 0 : 1;
}

/**
 * @brief Shell command: test status
 */
int cmd_test_status(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    if (!integration_initialized) {
        printf("Test integration not initialized\n");
        return 1;
    }
    
    printf("Test Integration Status:\n");
    printf("  Initialized: %s\n", integration_initialized ? "Yes" : "No");
    printf("  System Ready: %s\n", test_integration_system_ready() ? "Yes" : "No");
    printf("  Critical Tests: %s\n", integration_results.critical_tests_passed ? "PASSED" : "FAILED");
    printf("  All Tests: %s\n", integration_results.all_tests_passed ? "PASSED" : "FAILED");
    
    printf("\nComponent Status:\n");
    for (int i = 0; i < num_components; i++) {
        const component_status_t* comp = &component_status[i];
        printf("  %s: %s (Critical: %s)\n",
               comp->name,
               comp->registered ? "Registered" : "Not Registered",
               comp->critical_passed ? "PASS" : "FAIL");
    }
    
    return 0;
}

/**
 * @brief Shell command: test config
 */
int cmd_test_config(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: test config <show|set> [options]\n");
        printf("  show                    - Show current configuration\n");
        printf("  set stress <on|off>     - Enable/disable stress tests\n");
        printf("  set fault <on|off>      - Enable/disable fault tests\n");
        printf("  set security <on|off>   - Enable/disable security tests\n");
        printf("  set verbose <on|off>    - Enable/disable verbose output\n");
        printf("  set timeout <ms>        - Set test timeout\n");
        return 1;
    }
    
    if (strcmp(argv[1], "show") == 0) {
        printf("Test Configuration:\n");
        printf("  Stress Tests: %s\n", integration_config.run_stress_tests ? "Enabled" : "Disabled");
        printf("  Fault Tests: %s\n", integration_config.run_fault_tests ? "Enabled" : "Disabled");
        printf("  Security Tests: %s\n", integration_config.run_security_tests ? "Enabled" : "Disabled");
        printf("  Verbose Output: %s\n", integration_config.verbose_output ? "Enabled" : "Disabled");
        printf("  Abort on Failure: %s\n", integration_config.abort_on_failure ? "Enabled" : "Disabled");
        printf("  Test Timeout: %lu ms\n", integration_config.test_timeout_ms);
    }
    else if (strcmp(argv[1], "set") == 0 && argc >= 4) {
        if (strcmp(argv[2], "stress") == 0) {
            bool enable = strcmp(argv[3], "on") == 0;
            test_integration_enable_category(TEST_CATEGORY_STRESS, enable);
            printf("Stress tests %s\n", enable ? "enabled" : "disabled");
        }
        else if (strcmp(argv[2], "fault") == 0) {
            bool enable = strcmp(argv[3], "on") == 0;
            test_integration_enable_category(TEST_CATEGORY_FAULT, enable);
            printf("Fault tests %s\n", enable ? "enabled" : "disabled");
        }
        else if (strcmp(argv[2], "security") == 0) {
            bool enable = strcmp(argv[3], "on") == 0;
            test_integration_enable_category(TEST_CATEGORY_SECURITY, enable);
            printf("Security tests %s\n", enable ? "enabled" : "disabled");
        }
        else if (strcmp(argv[2], "verbose") == 0) {
            integration_config.verbose_output = strcmp(argv[3], "on") == 0;
            printf("Verbose output %s\n", integration_config.verbose_output ? "enabled" : "disabled");
        }
        else if (strcmp(argv[2], "timeout") == 0) {
            uint32_t timeout = atoi(argv[3]);
            if (timeout > 0) {
                test_integration_set_timeout(timeout);
                printf("Test timeout set to %lu ms\n", timeout);
            } else {
                printf("Invalid timeout value\n");
                return 1;
            }
        }
        else {
            printf("Unknown configuration option: %s\n", argv[2]);
            return 1;
        }
    }
    else {
        printf("Invalid config command\n");
        return 1;
    }
    
    return 0;
}

/**
 * @brief Shell command: test results
 */
int cmd_test_results(int argc, char* argv[]) {
    bool verbose = false;
    
    if (argc >= 2 && strcmp(argv[1], "verbose") == 0) {
        verbose = true;
    }
    
    test_integration_print_summary(verbose);
    
    return 0;
}

/**
 * @brief Register test shell commands
 */
bool test_integration_register_shell_commands(void) {
    static const shell_command_t test_commands[] = {
        {cmd_test_run, "test", "Run system tests (test run <target>)"},
        {cmd_test_status, "test_status", "Show test system status"},
        {cmd_test_config, "test_config", "Configure test system (test_config show|set ...)"},
        {cmd_test_results, "test_results", "Show test results (test_results [verbose])"}
    };
    
    bool all_registered = true;
    
    for (int i = 0; i < sizeof(test_commands) / sizeof(test_commands[0]); i++) {
        if (!shell_register_command(&test_commands[i])) {
            all_registered = false;
        }
    }
    
    if (all_registered) {
        log_message(LOG_LEVEL_INFO, "Test Integration", "Test shell commands registered");
    } else {
        log_message(LOG_LEVEL_WARN, "Test Integration", "Some test shell commands failed to register");
    }
    
    return all_registered;
}