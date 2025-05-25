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

// Define application version
#define APP_VERSION "1.0.0"
#define APP_NAME "RobohandR1"

// Forward declarations for application-specific functions
static int cmd_status(int argc, char *argv[]);
static int cmd_version(int argc, char *argv[]);
static void init_application(void);

// Application-specific shell commands
static const shell_command_t app_commands[] = {
    {cmd_status, "status", "Display application status"},
    {cmd_version, "version", "Show firmware version"},
};

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
    
    // Initialize the system
    kernel_result_t result = kernel_init(&config);
    if (result != SYS_INIT_OK) {
        printf("System initialization failed with code: %d.\n", result);
        return -1;
    }
    
    // Initialize application-specific components
    init_application();
    
    // Log startup message
    log_message(LOG_LEVEL_INFO, "Main", "RobohandR1 firmware started successfully.");
    
    // Enter the main system loop (this function never returns)
    kernel_run();
    
    return 0;  // Never reached
}

/**
 * @brief Command handler for 'status' command
 * 
 * Display current application status.
 */
static int cmd_status(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    log_message(LOG_LEVEL_INFO, "Stats", "Application Status");
    log_message(LOG_LEVEL_INFO, "Stats", "------------------");
    log_message(LOG_LEVEL_INFO, "Stats", "Uptime: %lu ms.", kernel_get_uptime_ms());
    
    // Get scheduler statistics
    scheduler_stats_t sched_stats;
    if (scheduler_get_stats(&sched_stats)) {
        log_message(LOG_LEVEL_INFO, "Stats", "Tasks created: %lu.", sched_stats.task_creates);
        log_message(LOG_LEVEL_INFO, "Stats", "Context switches: %lu.", sched_stats.context_switches);
    }
    
    // Get system statistics
    system_stats_t sys_stats;
    if (stats_get_system(&sys_stats)) {
        log_message(LOG_LEVEL_INFO, "Stats", "CPU usage: %u%%", sys_stats.cpu_usage_percent);
        log_message(LOG_LEVEL_INFO, "Stats", "Core 0: %u%%", sys_stats.core0_usage_percent);
        log_message(LOG_LEVEL_INFO, "Stats", "Core 1: %u%%", sys_stats.core1_usage_percent);
        log_message(LOG_LEVEL_INFO, "Stats", "Temperature: %lu°C.", sys_stats.temperature_c);
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
    
    log_message(LOG_LEVEL_INFO, "Firmware", "%s firmware v%s", APP_NAME, APP_VERSION);
    log_message(LOG_LEVEL_INFO, "Firmware", "Build date: %s %s", __DATE__, __TIME__);
    log_message(LOG_LEVEL_INFO, "Firmware", "SDK version: %s", PICO_SDK_VERSION_STRING);
    
    return 0;
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
    
    // Initialize any other application-specific hardware
    
    log_message(LOG_LEVEL_INFO, "Main", "Application initialization complete.");
}


/**
 * @brief Register application-specific shell commands
 * 
 * This function is called by kernel_init to register any
 * application-specific commands with the shell.
 */
void system_register_commands(void) {
    for (int i = 0; i < sizeof(app_commands) / sizeof(app_commands[0]); i++) {
        shell_register_command(&app_commands[i]);
    }
}