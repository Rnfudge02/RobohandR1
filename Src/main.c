/**
* @file main.c
* @brief Main application with USB shell, scheduler
*/

#include "pico/stdlib.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"


#include "scheduler.h"
#include "sensor_manager.h"
#include "stats.h"
#include "usb_shell.h"

#include <stdio.h>

#include "kernel_init.h"
#include "log_manager.h"

/**
 * @brief Main entry point
 * 
 * Initialize system components and enter the main processing loop.
 */
int main(void) {
    // Initialize system with custom configuration
    kernel_config_t config;
    kernel_get_default_config(&config);
    
    // Customize configuration
    config.app_name = APP_NAME;
    config.app_version = APP_VERSION;

    kernel_test_config_t t_config = {
        .enable_runtime_tests = true,
        .run_critical_only = true,
        .test_config.test_timeout_ms = 5000, // 5 second timeout
        .abort_on_test_failure = false,
        .test_config.run_stress_tests = false,
        .test_config.run_fault_tests = false,
        .test_config.verbose_output = true
    };

    config.test_config = t_config;
    
    // Initialize the system
    kernel_result_t result = kernel_init(&config);
    if (result != SYS_INIT_OK) {
        printf("System initialization failed with code: %d\n", result);
        return -1;
    }
    
    // Log startup message
    log_message(LOG_LEVEL_INFO, "Main", "RobohandR1 firmware started successfully");
    
    // Enter the main system loop (this function never returns)
    kernel_run();
    
    return 0;  // Never reached
}