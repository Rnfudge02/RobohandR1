/**
 * @file test_integration.h
 * @brief Test framework integration for RTOS components
 * @author System Test Framework
 * @date 2025-05-26
 * 
 * This module provides the main integration point for the runtime testing
 * framework. It should be called during system initialization to verify
 * all components are working correctly before allowing user interaction.
 */

#ifndef TEST_INTEGRATION_H
#define TEST_INTEGRATION_H

#include "test_framework.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test integration configuration
 */
typedef struct {
    bool run_critical_only;          ///< Run only critical tests during init
    bool run_stress_tests;           ///< Include stress tests
    bool run_fault_tests;            ///< Include fault injection tests
    bool run_security_tests;         ///< Include security tests
    bool verbose_output;             ///< Enable verbose test output
    bool abort_on_failure;           ///< Abort init if any test fails
    bool enable_shell_commands;      ///< Register test shell commands
    uint32_t test_timeout_ms;        ///< Default test timeout
} test_integration_config_t;

/**
 * @brief Test integration results
 */
typedef struct {
    bool all_tests_passed;           ///< All tests passed successfully
    bool critical_tests_passed;     ///< All critical tests passed
    uint32_t total_tests_run;        ///< Total number of tests executed
    uint32_t tests_passed;           ///< Number of tests that passed
    uint32_t tests_failed;           ///< Number of tests that failed
    uint32_t tests_skipped;          ///< Number of tests that were skipped
    uint64_t total_execution_time_us; ///< Total test execution time
    const char* first_failure;       ///< Name of first failed test (if any)
} test_integration_results_t;

/**
 * @brief Initialize and run all component tests
 * 
 * This is the main entry point for the testing framework. It should be
 * called during system initialization to verify all components are working
 * correctly before allowing user interaction with the system.
 * 
 * @param config Test configuration (NULL for defaults)
 * @return true if all critical tests passed, false otherwise
 */
bool test_integration_run_all(const test_integration_config_t* config);

/**
 * @brief Initialize the test integration system
 * 
 * Sets up the test framework and registers all test suites.
 * Must be called before running tests.
 * 
 * @param config Test configuration (NULL for defaults)
 * @return true if initialization successful, false otherwise
 */
bool test_integration_init(const test_integration_config_t* config);

/**
 * @brief Run only critical tests during system initialization
 * 
 * This function runs only the most critical tests required for basic
 * system operation. It's designed to be fast and reliable for production
 * system initialization.
 * 
 * @return true if all critical tests passed, false otherwise
 */
bool test_integration_run_critical(void);

/**
 * @brief Run comprehensive test suite
 * 
 * This function runs the full test suite including unit tests, integration
 * tests, stress tests, and fault tests. Intended for development and
 * debugging scenarios.
 * 
 * @return true if all tests passed, false otherwise
 */
bool test_integration_run_comprehensive(void);

/**
 * @brief Run tests for a specific component
 * 
 * @param component_name Name of component to test ("scheduler", "mpu", "tz")
 * @return true if component tests passed, false otherwise
 */
bool test_integration_run_component(const char* component_name);

/**
 * @brief Get test integration results
 * 
 * @param results Output structure for test results
 * @return true if results retrieved successfully, false otherwise
 */
bool test_integration_get_results(test_integration_results_t* results);

/**
 * @brief Print test integration summary
 * 
 * @param verbose Include detailed test information
 */
void test_integration_print_summary(bool verbose);

/**
 * @brief Get default test integration configuration
 * 
 * @param config Output configuration structure
 */
void test_integration_get_default_config(test_integration_config_t* config);

/**
 * @brief Register test shell commands
 * 
 * Registers shell commands for manual test execution and debugging.
 * Only registers commands if shell support is enabled in configuration.
 * 
 * @return true if commands registered successfully, false otherwise
 */
bool test_integration_register_shell_commands(void);

/**
 * @brief Clean up test integration resources
 * 
 * Should be called when shutting down the system or when tests are complete.
 */
void test_integration_cleanup(void);

/**
 * @brief Check if system is ready for normal operation
 * 
 * This function checks the results of the initialization tests to determine
 * if the system is safe for normal operation.
 * 
 * @return true if system is ready, false if critical issues were found
 */
bool test_integration_system_ready(void);

/**
 * @brief Get component test status
 * 
 * @param component_name Name of component ("scheduler", "mpu", "tz")
 * @param passed Output: true if component tests passed
 * @param critical_passed Output: true if critical component tests passed
 * @return true if component status retrieved, false if component not found
 */
bool test_integration_get_component_status(const char* component_name, 
                                          bool* passed, bool* critical_passed);

/**
 * @brief Enable or disable specific test categories
 * 
 * @param category Test category to configure
 * @param enabled true to enable, false to disable
 * @return true if successful, false otherwise
 */
bool test_integration_enable_category(test_category_t category, bool enabled);

/**
 * @brief Set test execution timeout
 * 
 * @param timeout_ms Timeout in milliseconds
 */
void test_integration_set_timeout(uint32_t timeout_ms);

// Shell command functions (only enabled if shell support is configured)

/**
 * @brief Shell command: run tests
 */
int cmd_test_run(int argc, char* argv[]);

/**
 * @brief Shell command: test status
 */
int cmd_test_status(int argc, char* argv[]);

/**
 * @brief Shell command: test config
 */
int cmd_test_config(int argc, char* argv[]);

/**
 * @brief Shell command: test results
 */
int cmd_test_results(int argc, char* argv[]);

#ifdef __cplusplus
}
#endif

#endif // TEST_INTEGRATION_H