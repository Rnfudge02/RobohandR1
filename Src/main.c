/**
* @file main.c
* @brief Main application with USB shell, scheduler
*/

#include "pico/stdlib.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"
#include "hardware/structs/xip_ctrl.h"  // For XIP control
#include "hardware/xip_cache.h"         // For XIP functions

// Define the control bit if not defined already
#ifndef XIP_CTRL_EN
#define XIP_CTRL_EN (1u << 0)  // Typically bit 0 is the enable bit

#endif

#include "hardware_stats.h"
#include "scheduler.h"
#include "sensor_manager.h"
#include "sensor_manager_init.h"
#include "stats.h"
#include "usb_shell.h"

#include "hardware_stats_shell_commands.h"
#include "sensor_manager_shell_commands.h"
#include "scheduler_shell_commands.h"
#include "stats_shell_commands.h"

#include <stdio.h>

#include "system_init.h"
#include "log_manager.h"


// Optional: Include specific sensor or peripheral drivers if needed
// #include "Drivers/Sensors/bmm350_adapter.h"

// Define application version
#define APP_VERSION "1.0.0"
#define APP_NAME "RobohandR1"

// Forward declarations for application-specific functions
static void register_app_commands(void);
static int cmd_status(int argc, char *argv[]);
static int cmd_version(int argc, char *argv[]);
static void init_application(void);

// Application-specific shell commands
static const shell_command_t app_commands[] = {
    {"status", "Display application status", cmd_status},
    {"version", "Show firmware version", cmd_version},
};

/**
 * @brief Main entry point
 * 
 * Initialize system components and enter the main processing loop.
 */
int main(void) {
    // Initialize system with custom configuration
    sys_init_config_t config;
    system_init_get_default_config(&config);
    
    // Customize configuration
    config.app_name = APP_NAME;
    config.app_version = APP_VERSION;
    config.flags = SYS_INIT_FLAG_SHELL |      // Enable shell
                   SYS_INIT_FLAG_SENSORS |    // Enable sensors
                   SYS_INIT_FLAG_LOGGING |    // Enable logging
                   SYS_INIT_FLAG_VERBOSE;     // Enable verbose output
    
    // Initialize the system
    sys_init_result_t result = system_init(&config);
    if (result != SYS_INIT_OK) {
        printf("System initialization failed with code: %d\n", result);
        return -1;
    }
    
    // Initialize application-specific components
    init_application();
    
    // Log startup message
    LOG_INFO("App", "RobohandR1 firmware started successfully");
    
    // Enter the main system loop (this function never returns)
    system_run();
    
    return 0;  // Never reached
}

/**
 * @brief Register application-specific shell commands
 * 
 * This function is called by system_init to register any
 * application-specific commands with the shell.
 */
void system_register_commands(void) {
    register_app_commands();
}

/**
 * @brief Register application commands with the shell
 */
static void register_app_commands(void) {
    for (int i = 0; i < sizeof(app_commands) / sizeof(app_commands[0]); i++) {
        shell_register_command(&app_commands[i]);
    }
}

/**
 * @brief Initialize application-specific components
 * 
 * Set up any additional hardware or software components
 * that are specific to this application.
 */
static void init_application(void) {
    // Initialize application statistics
    stats_init();
    
    // Set up application-specific GPIO
    // gpio_init(LED_PIN);
    // gpio_set_dir(LED_PIN, GPIO_OUT);
    
    // Initialize any other application-specific hardware
    
    LOG_INFO("App", "Application initialization complete");
}

/**
 * @brief Command handler for 'status' command
 * 
 * Display current application status.
 */
static int cmd_status(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("Application Status:\n");
    printf("------------------\n");
    printf("Uptime: %lu ms\n", system_get_uptime_ms());
    
    // Get scheduler statistics
    scheduler_stats_t sched_stats;
    if (scheduler_get_stats(&sched_stats)) {
        printf("Tasks created: %lu\n", sched_stats.task_creates);
        printf("Context switches: %lu\n", sched_stats.context_switches);
    }
    
    // Get system statistics
    system_stats_t sys_stats;
    if (stats_get_system(&sys_stats)) {
        printf("CPU usage: %u%%\n", sys_stats.cpu_usage_percent);
        printf("Core 0: %u%%\n", sys_stats.core0_usage_percent);
        printf("Core 1: %u%%\n", sys_stats.core1_usage_percent);
        printf("Temperature: %lu°C\n", sys_stats.temperature_c);
    }
    
    return 0;
}

/**
 * @brief Command handler for 'version' command
 * 
 * Display firmware version information.
 */
static int cmd_version(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("%s firmware v%s\n", APP_NAME, APP_VERSION);
    printf("Build date: %s %s\n", __DATE__, __TIME__);
    printf("SDK version: %s\n", PICO_SDK_VERSION_STRING);
    
    return 0;
}