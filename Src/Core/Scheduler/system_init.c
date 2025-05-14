/**
* @file system_init.c
* @brief System initialization and startup sequence implementation
* @author Based on Robert Fudge's work
* @date 2025-05-14
*/

#include "system_init.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "scheduler.h"
#include "log_manager.h"
#include "usb_shell.h"
#include "sensor_manager_init.h"
#include "scheduler_shell_commands.h"
#include "stats_shell_commands.h"
#include "hardware_stats_shell_commands.h"
#include "sensor_manager_shell_commands.h"

#include <string.h>
#include <stdio.h>

// Optional MPU/TZ includes
#ifdef ENABLE_MPU_TZ
#include "scheduler_mpu_tz.h"
#include "scheduler_tz.h"
#endif

// Module-level variables
static bool system_initialized = false;
static absolute_time_t system_start_time;
static sys_init_config_t system_config;
static bool watchdog_enabled = false;

// Forward declarations for internal functions
static sys_init_result_t init_hardware(void);
static sys_init_result_t init_scheduler(void);
static sys_init_result_t init_logging(void);
static sys_init_result_t init_shell(void);
static sys_init_result_t init_sensors(void);
static sys_init_result_t init_mpu_tz(void);
static void register_shell_commands(void);
static void print_banner(void);
static void shell_task_wrapper(void *params);
static sys_init_result_t init_shell_task(void);

// Add a global variable to track the shell task ID
static int shell_task_id = -1;

/**
 * @brief Print system information banner
 */
static void print_banner(void) {
    printf("\n");
    printf("=============================================\n");
    printf("  %s v%s\n", system_config.app_name, system_config.app_version);
    printf("  Board: Raspberry Pi Pico 2W (RP2350)\n");
    printf("  Build Date: %s %s\n", __DATE__, __TIME__);
    printf("=============================================\n");
    printf("\n");
}

/**
 * @brief Initialize system hardware components
 */
static sys_init_result_t init_hardware(void) {
    // Initialize standard I/O
    stdio_init_all();
    
    // Wait for USB to connect if using USB stdio
    #ifdef USE_WAIT_FOR_USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    #endif
    
    // Give time for USB to initialize
    sleep_ms(500);
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Hardware initialization complete\n");
    }
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the scheduler
 */
static sys_init_result_t init_scheduler(void) {
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Initializing scheduler...\n");
    }
    
    if (!scheduler_init()) {
        printf("ERROR: Failed to initialize scheduler\n");
        return SYS_INIT_ERROR_SCHEDULER;
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Scheduler initialized successfully\n");
    }
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the logging system
 */
static sys_init_result_t init_logging(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_LOGGING)) {
        return SYS_INIT_OK; // Logging not requested
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Initializing logging system...\n");
    }
    
    // Configure logger
    log_config_t log_cfg;
    log_get_default_config(&log_cfg);
    
    // Define logging levels
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        log_cfg.console_level = LOG_LEVEL_DEBUG;
    } else {
        log_cfg.console_level = LOG_LEVEL_INFO;
    }
    
    log_cfg.flash_level = LOG_LEVEL_ERROR;
    log_cfg.buffer_size = 8192;
    
    // Initialize logger
    if (!log_init(&log_cfg)) {
        printf("ERROR: Failed to initialize logging system\n");
        return SYS_INIT_ERROR_LOGGER;
    }
    
    // Configure log destinations (start with console only)
    log_set_destinations(LOG_DEST_CONSOLE);
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Logging system initialized successfully\n");
    }
    
    // Log system initialization
    LOG_INFO("System", "Logging system initialized");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the shell
 */
/**
 * @brief Initialize the shell
 */
static sys_init_result_t init_shell(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SHELL)) {
        return SYS_INIT_OK; // Shell not requested
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Initializing shell...\n");
    }
    
    // Initialize shell
    shell_init();
    
    // Register shell commands
    register_shell_commands();
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Shell initialized successfully\n");
    }
    
    LOG_INFO("System", "Shell initialized");
    
    // Create the shell task
    sys_init_result_t result = init_shell_task();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize sensors
 */
static sys_init_result_t init_sensors(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SENSORS)) {
        return SYS_INIT_OK; // Sensors not requested
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Initializing sensor manager...\n");
    }
    
    // Initialize sensor manager
    if (!sensor_manager_init()) {
        LOG_ERROR("System", "Failed to initialize sensor manager");
        return SYS_INIT_ERROR_SENSOR_MANAGER;
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Sensor manager initialized successfully\n");
    }
    
    LOG_INFO("System", "Sensor manager initialized");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize MPU and TrustZone
 */
static sys_init_result_t init_mpu_tz(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_MPU_TZ)) {
        return SYS_INIT_OK; // MPU/TZ not requested
    }
    
    #ifdef ENABLE_MPU_TZ
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Initializing MPU and TrustZone...\n");
    }
    
    // Initialize MPU
    if (!scheduler_mpu_tz_init()) {
        LOG_ERROR("System", "Failed to initialize MPU/TZ");
        return SYS_INIT_ERROR_MPU_TZ;
    }
    
    // Initialize TrustZone
    if (!scheduler_tz_init()) {
        LOG_ERROR("System", "Failed to initialize TrustZone");
        return SYS_INIT_ERROR_MPU_TZ;
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("MPU and TrustZone initialized successfully\n");
    }
    
    LOG_INFO("System", "MPU and TrustZone initialized");
    #else
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("MPU/TZ support not enabled in build\n");
    }
    
    LOG_WARN("System", "MPU/TZ requested but not enabled in build");
    #endif
    
    return SYS_INIT_OK;
}

/**
 * @brief Register shell commands
 */
static void register_shell_commands(void) {
    LOG_DEBUG("System", "Registering shell commands");
    
    // Register core commands
    register_scheduler_commands();
    register_stats_commands();
    register_cache_fpu_commands();
    
    // Register sensor commands if sensors enabled
    if (system_config.flags & SYS_INIT_FLAG_SENSORS) {
        register_sensor_manager_commands();
    }
    
    // Register application-specific commands
    system_register_commands();
    
    LOG_DEBUG("System", "Shell commands registered");
}

/**
 * @brief Initialize the system with custom configuration
 */
/**
 * @brief Initialize the system with custom configuration
 */
sys_init_result_t system_init(const sys_init_config_t* config) {
    sys_init_result_t result;
    
    // Store start time
    system_start_time = get_absolute_time();
    
    // Copy configuration
    if (config != NULL) {
        memcpy(&system_config, config, sizeof(sys_init_config_t));
    } else {
        // Use default configuration
        system_init_get_default_config(&system_config);
    }
    
    // First, initialize hardware
    result = init_hardware();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Print welcome banner
    print_banner();
    
    // Initialize scheduler (required)
    result = init_scheduler();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Initialize logging system (if requested)
    result = init_logging();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Initialize MPU and TrustZone (if requested)
    result = init_mpu_tz();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Initialize shell (if requested)
    result = init_shell();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Initialize sensors (if requested)
    result = init_sensors();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Start scheduler - MOVED BEFORE shell task creation
    LOG_INFO("System", "Starting scheduler");
    if (!scheduler_start()) {
        LOG_FATAL("System", "Failed to start scheduler");
        return SYS_INIT_ERROR_SCHEDULER;
    }
    
    
    
    // Setup watchdog if requested
    if (system_config.flags & SYS_INIT_FLAG_WATCHDOG) {
        uint32_t timeout_ms = system_config.watchdog_timeout_ms;
        if (timeout_ms < 100) timeout_ms = 100;  // Minimum 100ms
        
        LOG_INFO("System", "Enabling watchdog with %lu ms timeout", timeout_ms);
        watchdog_enable(timeout_ms, true);
        watchdog_enabled = true;
    }
    
    // Mark system as initialized
    system_initialized = true;
    
    LOG_INFO("System", "System initialization complete");
    
    return SYS_INIT_OK;
}

/**
 * @brief Get default system initialization configuration
 */
void system_init_get_default_config(sys_init_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(sys_init_config_t));
    
    config->flags = SYS_INIT_FLAG_DEFAULT;  // Shell, Sensors, Logging
    config->watchdog_timeout_ms = 5000;     // 5 second timeout
    config->app_name = "RobohandR1";
    config->app_version = "1.0.0";
}

/**
 * @brief Run the system main loop
 */
/**
 * @brief Run the system main loop
 */
void system_run(void) {
    if (!system_initialized) {
        printf("ERROR: System not initialized before system_run()\n");
        return;
    }
    
    LOG_INFO("System", "Entering main system loop");
    
    // Print initial prompt again to be sure
    printf("> ");
    
    // Main processing loop
    uint32_t loop_counter = 0;
    uint32_t last_stat_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_shell_check = 0;
    
    while (1) {
        // Process log messages periodically
        if (system_config.flags & SYS_INIT_FLAG_LOGGING) {
            log_process();
        }
        
        // Run scheduler tasks
        scheduler_run_pending_tasks();
        
        // Feed watchdog if enabled
        if (watchdog_enabled && (loop_counter % 100 == 0)) {
            watchdog_update();
        }
        
        // Print statistics every 10 seconds in verbose mode
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if ((system_config.flags & SYS_INIT_FLAG_VERBOSE) && 
            (current_time - last_stat_time > 30000)) {
            
            LOG_DEBUG("System", "System uptime: %lu ms", system_get_uptime_ms());
            last_stat_time = current_time;
        }
        
        // Brief delay to prevent CPU hogging
        sleep_ms(1); // Reduce to 1ms for better responsiveness
        
        loop_counter++;
    }
}

/**
 * @brief Register application commands with the shell
 * 
 * This is a weak function that can be overridden by the application
 */
__attribute__((weak)) void system_register_commands(void) {
    // Default implementation does nothing
    // Application should override this to register custom commands
}

/**
 * @brief Handle system shutdown
 */
void system_shutdown(bool restart) {
    LOG_INFO("System", "System shutdown initiated, restart=%d", restart);
    
    // Stop scheduler
    scheduler_stop();
    
    // Flush logs
    if (system_config.flags & SYS_INIT_FLAG_LOGGING) {
        log_flush();
    }
    
    // Give time for logs to flush
    sleep_ms(100);
    
    // If restarting, trigger watchdog reset
    if (restart) {
        LOG_INFO("System", "Restarting system...");
        sleep_ms(100);
        
        // Trigger reset
        watchdog_enable(1, false);  // 1ms timeout, no debug pause
        while (1) {
            // Wait for watchdog to reset system
        }
    }
    
    // Otherwise, just halt
    LOG_INFO("System", "System halted");
    while (1) {
        // Infinite loop
        __wfi();  // Wait for interrupt (low power)
    }
}

/**
 * @brief Get the time since system initialization
 */
uint32_t system_get_uptime_ms(void) {
    return to_ms_since_boot(get_absolute_time()) - 
           to_ms_since_boot(system_start_time);
}

/**
 * @brief Feed the watchdog to prevent system reset
 */
void system_feed_watchdog(void) {
    if (watchdog_enabled) {
        watchdog_update();
    }
}


/**
 * @brief Shell task wrapper function for the scheduler
 * 
 * This function is registered with the scheduler and processes shell input.
 * 
 * @param params Parameters (unused)
 */
static void shell_task_wrapper(void *params) {
    (void)params; // Unused parameter
    
    // Call the shell processing function
    shell_task();
    
    scheduler_yield();
}

/**
 * @brief Initialize the shell task
 * 
 * Creates a scheduler task for processing shell commands.
 * 
 * @return SYS_INIT_OK if successful, error code otherwise
 */
static sys_init_result_t init_shell_task(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SHELL)) {
        return SYS_INIT_OK; // Shell not requested
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Creating shell task...\n");
    }
    
    // Create a task for the shell
    shell_task_id = scheduler_create_task(
        shell_task_wrapper,        // Task function
        NULL,                      // No parameters needed
        1024,                      // Stack size (adjust as needed)
        TASK_PRIORITY_HIGH,      // Normal priority
        "shell",                   // Task name
        0,                         // Run on core 0
        TASK_TYPE_PERSISTENT       // Run continuously
    );
    
    if (shell_task_id < 0) {
        LOG_ERROR("System", "Failed to create shell task");
        return SYS_INIT_ERROR_SHELL;
    }
    
    if (system_config.flags & SYS_INIT_FLAG_VERBOSE) {
        printf("Shell task created with ID: %d\n", shell_task_id);
    }
    
    // Print initial prompt
    printf("> ");
    
    LOG_INFO("System", "Shell task initialized");
    
    return SYS_INIT_OK;
}