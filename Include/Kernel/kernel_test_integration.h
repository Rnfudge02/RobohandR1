/**
 * @file kernel_test_integration.h  
 * @brief Fixed kernel test integration for proper pre/post scheduler testing
 */

#ifndef KERNEL_TEST_INTEGRATION_H
#define KERNEL_TEST_INTEGRATION_H

#include "test_integration.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enable_runtime_tests;
    bool run_critical_only;
    bool abort_on_test_failure;
    test_integration_config_t test_config;
} kernel_test_config_t;

/**
 * @brief Run only non-scheduler tests before scheduler starts
 * 
 * This should test:
 * - Basic system initialization
 * - Memory management  
 * - Hardware detection
 * - Component initialization (but not execution)
 */
bool kernel_run_pre_scheduler_tests(const kernel_test_config_t* config);

/**
 * @brief Run scheduler and integration tests after scheduler starts
 */
bool kernel_run_post_scheduler_tests(const kernel_test_config_t* config);

#endif // KERNEL_TEST_INTEGRATION_H