/**
 * @file kernel_test_integration.c
 * @brief Kernel test integration implementation with proper pre/post scheduler separation
 */

#include "kernel_test_integration.h"
#include "test_framework.h"
#include "test_mpu.h"
#include "test_tz.h"
#include "log_manager.h"
#include "scheduler.h"

/**
 * @brief Run only hardware and initialization tests before scheduler starts
 */
bool kernel_run_pre_scheduler_tests(const kernel_test_config_t* config) {
    if (!config || !config->enable_runtime_tests) {
        return true;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Test", "Running pre-scheduler tests (hardware/init only)");
    
    // Initialize test framework for pre-scheduler tests
    test_framework_config_t framework_config;
    test_framework_get_default_config(&framework_config);
    framework_config.verbose_output = config->test_config.verbose_output;
    framework_config.default_timeout_ms = 1000; // Short timeout for init tests
    
    if (!test_framework_init(&framework_config)) {
        log_message(LOG_LEVEL_ERROR, "Kernel Test", "Failed to initialize test framework");
        return false;
    }
    
    bool all_passed = true;
    
    // Test 1: Hardware Detection Tests
    // Only test hardware support detection, not actual usage
    if (scheduler_mpu_is_enabled()) {
        log_message(LOG_LEVEL_INFO, "Kernel Test", "✓ MPU hardware detected");
    } else {
        log_message(LOG_LEVEL_INFO, "Kernel Test", "- MPU hardware not available");
    }
    
    if (scheduler_tz_is_supported()) {
        log_message(LOG_LEVEL_INFO, "Kernel Test", "✓ TrustZone hardware detected");
    } else {
        log_message(LOG_LEVEL_INFO, "Kernel Test", "- TrustZone hardware not available");
    }
    
    // Test 2: Basic System State Tests
    // Test that core systems are initialized but not running
    
    // Verify scheduler is initialized but not started
    scheduler_stats_t stats;
    if (scheduler_get_stats(&stats)) {
        if (stats.total_runtime == 0 && stats.context_switches == 0) {
            log_message(LOG_LEVEL_INFO, "Kernel Test", "✓ Scheduler initialized but not started");
        } else {
            log_message(LOG_LEVEL_ERROR, "Kernel Test", "✗ Scheduler appears to be running during pre-tests");
            all_passed = false;
        }
    } else {
        log_message(LOG_LEVEL_ERROR, "Kernel Test", "✗ Scheduler not properly initialized");
        all_passed = false;
    }
    
    // Test 3: Memory Management Basic Tests (if applicable)
    // These don't require scheduler to be running
    
    log_message(LOG_LEVEL_INFO, "Kernel Test", "Pre-scheduler tests completed: %s", 
                all_passed ? "PASSED" : "FAILED");
    
    return all_passed;
}

/**
 * @brief Run full scheduler and integration tests after scheduler starts
 */
bool kernel_run_post_scheduler_tests(const kernel_test_config_t* config) {
    if (!config || !config->enable_runtime_tests) {
        return true;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Test", "Running post-scheduler tests (full suite)");
    
    // Initialize test integration for full testing
    test_integration_config_t integration_config;
    test_integration_get_default_config(&integration_config);
    
    // Apply kernel test config to integration config
    integration_config.run_critical_only = config->run_critical_only;
    integration_config.run_stress_tests = config->test_config.run_stress_tests;
    integration_config.run_fault_tests = config->test_config.run_fault_tests;
    integration_config.verbose_output = config->test_config.verbose_output;
    integration_config.abort_on_failure = config->abort_on_test_failure;
    integration_config.test_timeout_ms = config->test_config.test_timeout_ms;
    
    // Verify scheduler is actually running before running scheduler tests
    scheduler_stats_t stats;
    if (!scheduler_get_stats(&stats)) {
        log_message(LOG_LEVEL_ERROR, "Kernel Test", "Cannot get scheduler stats for post-scheduler tests");
        return false;
    }
    
    // Give scheduler a moment to start up and register some activity
    sleep_ms(100);
    scheduler_stats_t stats_after;
    scheduler_get_stats(&stats_after);
    
    if (stats_after.total_runtime <= stats.total_runtime && stats_after.context_switches <= stats.context_switches) {
        log_message(LOG_LEVEL_ERROR, "Kernel Test", "Scheduler not showing activity - may not be running properly");
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Test", "✓ Scheduler confirmed running - proceeding with full tests");
    
    // Run appropriate test level
    bool result;
    if (config->run_critical_only) {
        result = test_integration_run_critical();
    } else {
        result = test_integration_run_all(&integration_config);
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Test", "Post-scheduler tests completed: %s", 
                result ? "PASSED" : "FAILED");
    
    return result;
}