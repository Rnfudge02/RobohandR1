/**
* @file kernel_init.c
* @brief Kernel initialization and startup sequence implementation
* @author Robert Fudge
* @date 2025-05-14
*/

#include "kernel_init.h"

#include "log_manager.h"
#include "spinlock_manager.h"
#include "sensor_manager.h"
#include "servo_manager.h"

#include "scheduler.h"
#include "scheduler_mpu.h"
#include "scheduler_tz.h"

#include "stats.h"
#include "usb_shell.h"

#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "interrupt_manager.h"
#include "led_driver.h"

#include "test_integration.h"
#include "kernel_test_integration.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <string.h>
#include <stdio.h>

// Module-level variables
static bool system_initialized = false;
static absolute_time_t system_start_time;
static kernel_config_t system_config;
static bool watchdog_enabled = false;

// Forward declarations for internal functions
static kernel_result_t init_hardware(void);
static kernel_result_t init_scheduler(void);
static kernel_result_t init_logging(void);
static kernel_result_t init_shell(void);
static kernel_result_t init_sensors(void);
static kernel_result_t init_mpu_tz(void);
static void register_shell_commands(void);
static void print_banner(void);
static void shell_task_wrapper(void *params);
static kernel_result_t init_shell_task(void);
static kernel_result_t init_servos(void);
static kernel_result_t init_core_subsystems(void);

static kernel_result_t init_interrupt_manager(void);
static kernel_result_t init_led_driver(void);

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
 * @brief Initialize the logging system
 */
static kernel_result_t init_logging(void) {
    // Core logging is already initialized in init_core_subsystems
    // This function just completes any additional logging setup
    
    // Set log levels if needed
    log_set_level(LOG_LEVEL_DEBUG, LOG_DEST_CONSOLE);
    log_set_level(LOG_LEVEL_ERROR, LOG_DEST_FLASH);
    
    // Log a message to confirm
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Logging system initialized");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the scheduler
 */
static kernel_result_t init_scheduler(void) {

    log_message(LOG_LEVEL_INFO, "Kernel Init", "Initializing system...");

    
    if (!scheduler_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "ERROR: Failed to initialize scheduler.");
        return SYS_INIT_ERROR_SCHEDULER;
    }

    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the shell
 */
static kernel_result_t init_shell(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SHELL)) {
        return SYS_INIT_OK; // Shell not requested
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Initializing shell...");

    // Initialize shell
    shell_init();
    
    // Register shell commands
    register_shell_commands();
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Shell initialized");
    
    // Create the shell task
    kernel_result_t result = init_shell_task();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize sensors
 */
static kernel_result_t init_sensors(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SENSORS)) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Sensors disabled.");
        return SYS_INIT_OK; // Sensors not requested
    }
    
    // Initialize sensor manager
    if (!sensor_manager_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to initialize sensor manager.");
        return SYS_INIT_ERROR_SENSOR_MANAGER;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Sensor manager initialized.");
    
    return SYS_INIT_OK;
}



/**
 * @brief Register shell commands
 */
static void register_shell_commands(void) {
    log_message(LOG_LEVEL_DEBUG, "Kernel Init", "Registering shell commands.");
    
    // Register core commands
    register_scheduler_commands();
    register_stats_commands();
    register_spinlock_commands();

    if (system_config.flags & SYS_INIT_FLAG_INTERRUPT_MGR) {
        register_interrupt_manager_commands();
    }
    
    if (system_config.flags & SYS_INIT_FLAG_TZ) {
        register_tz_commands();
    }

    if (system_config.flags & SYS_INIT_FLAG_MPU) {
        register_mpu_commands();
    }
    
    
    // Register sensor commands if sensors enabled
    if (system_config.flags & SYS_INIT_FLAG_SENSORS) {
        register_sensor_manager_commands();
    }

    if (system_config.flags & SYS_INIT_FLAG_SERVOS) {
        register_servo_manager_commands();
    }
    
    // Register application-specific commands
    kernel_register_commands();
    
    log_message(LOG_LEVEL_DEBUG, "Kernel Init", "Shell commands registered");
}

/**
 * @brief Initialize the system with custom configuration
 */
/**
 * @brief Initialize the system with custom configuration
 */
kernel_result_t kernel_init(const kernel_config_t* config) {
    kernel_result_t result;
    
    // Store start time
    system_start_time = get_absolute_time();
    
    // Copy configuration
    if (config != NULL) {
        memcpy(&system_config, config, sizeof(kernel_config_t));
    } else {
        // Use default configuration
        kernel_get_default_config(&system_config);
    }

    stdio_init_all();

    
    result = init_core_subsystems();
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

    if (!hw_spinlock_manager_register_with_scheduler()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to register hardware spinlock manager with scheduler");
        return SYS_INIT_ERROR_GENERAL;
    }

    result = init_interrupt_manager();
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

    result = init_servos();
    if (result != SYS_INIT_OK) {
        return result;
    }

    result = init_led_driver();
    if (result != SYS_INIT_OK) {
        return result;
    }

    // Stage 1: Pre-scheduler tests (in kernel_init before scheduler starts)
    if (config && config->test_config.enable_runtime_tests) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Running pre-scheduler component tests...");
    
        kernel_test_config_t pre_config = config->test_config;
        pre_config.test_config.run_stress_tests = false;
        pre_config.test_config.run_fault_tests = false;
        pre_config.test_config.verbose_output = true;
    
        bool pre_tests_passed = kernel_run_pre_scheduler_tests(&pre_config);
    
        if (!pre_tests_passed && config->test_config.abort_on_test_failure) {
            log_message(LOG_LEVEL_FATAL, "Kernel Init", "Aborting due to pre-scheduler test failures");
            return SYS_INIT_ERROR_GENERAL;
        }
    }
    
    // Start scheduler - MOVED BEFORE shell task creation
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Starting scheduler.");
    if (!scheduler_start()) {
        log_message(LOG_LEVEL_FATAL, "Kernel Init", "Failed to start scheduler.");
        return SYS_INIT_ERROR_SCHEDULER;
    }

    // Stage 2: Post-scheduler tests (after scheduler starts)
    if (config && config->test_config.enable_runtime_tests) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Running post-scheduler component tests...");
    
        kernel_test_config_t post_config = config->test_config;
        post_config.run_critical_only = false; // Can run more comprehensive tests now
    
        bool post_tests_passed = kernel_run_post_scheduler_tests(&post_config);
    
        if (!post_tests_passed) {
            log_message(LOG_LEVEL_WARN, "Kernel Init", "Some post-scheduler tests failed");
        }
    }


    // If servos are enabled, ensure the servo task is created
    if (system_config.flags & SYS_INIT_FLAG_SERVOS) {
        if (servo_manager_ensure_task()) {
            log_message(LOG_LEVEL_INFO, "Kernel Init", "Servo manager task confirmed");
        } else {
            log_message(LOG_LEVEL_WARN, "Kernel Init", "Could not create servo manager task");
        }
    }
    
    // Setup watchdog if requested
    if (system_config.flags & SYS_INIT_FLAG_WATCHDOG) {
        uint32_t timeout_ms = system_config.watchdog_timeout_ms;
        if (timeout_ms < 100) timeout_ms = 100;  // Minimum 100ms
        
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Enabling watchdog with %lu ms timeout", timeout_ms);
        watchdog_enable(timeout_ms, true);
        watchdog_enabled = true;
    }
    
    // Mark system as initialized
    system_initialized = true;
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "System initialization complete");
    
    return SYS_INIT_OK;
}

/**
 * @brief Get default system initialization configuration
 */
void kernel_get_default_config(kernel_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(kernel_config_t));
    
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
void kernel_run(void) {
    if (!system_initialized) {
        printf("ERROR: System not initialized before system_run().");
        return;
    }
    
    log_message(LOG_LEVEL_INFO, "System Runtime", "Entering main system loop");
    
    // Print initial prompt again to be sure
    printf("> ");
    
    // Main processing loop
    uint32_t loop_counter = 0;
    uint32_t last_stat_time = to_ms_since_boot(get_absolute_time());
    
    while (1) {
        //log_process(); NOSONAR

        if (system_config.flags & SYS_INIT_FLAG_INTERRUPT_MGR) {
            interrupt_process_coalesced();
        }

        // Run scheduler tasks
        scheduler_run_pending_tasks();
        
        // Feed watchdog if enabled
        if (watchdog_enabled && (loop_counter % 100 == 0)) {
            watchdog_update();
        }
        
        // Print statistics every 60 seconds in verbose mode
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_stat_time > 60000) {
            
            log_message(LOG_LEVEL_DEBUG, "System Runtime", "System uptime: %lu ms", kernel_get_uptime_ms());
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
__attribute__((weak)) void kernel_register_commands(void) {
    // Default implementation does nothing
    // Application should override this to register custom commands
}

/**
 * @brief Handle system shutdown
 */
void kernel_shutdown(bool restart) {
    log_message(LOG_LEVEL_INFO, "Kernel Init", "System shutdown initiated, restart = %d", restart);
    
    // Stop scheduler
    scheduler_stop();
    
    // Deinitialize servo manager if initialized
    if (system_config.flags & SYS_INIT_FLAG_SERVOS) {
        servo_manager_deinit();
    }

    if (system_config.flags & SYS_INIT_FLAG_LED_DRIVER) {
        led_driver_kernel_deinit();
    }

    log_flush();

    
    // Give time for logs to flush
    sleep_ms(100);
    
    // If restarting, trigger watchdog reset
    if (restart) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Restarting system...");
        sleep_ms(100);
        
        // Trigger reset
        watchdog_enable(1, false);  // 1ms timeout, no debug pause
        while (1) {
            // Wait for watchdog to reset system
        }
    }
    
    // Otherwise, just halt
    log_message(LOG_LEVEL_INFO, "Kernel Init", "System halted");
    while (1) {
        // Infinite loop
        __wfi();  // Wait for interrupt (low power)
    }
}

/**
 * @brief Get the time since system initialization
 */
uint32_t kernel_get_uptime_ms(void) {
    return to_ms_since_boot(get_absolute_time()) - 
           to_ms_since_boot(system_start_time);
}

/**
 * @brief Feed the watchdog to prevent system reset
 */
void kernel_feed_watchdog(void) {
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
static kernel_result_t init_shell_task(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SHELL)) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Shell disabled.");
        return SYS_INIT_OK; // Shell not requested
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
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to create shell task.");
        return SYS_INIT_ERROR_SHELL;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Shell task created with ID: %d.", shell_task_id);
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Shell task initialized.");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize servo subsystem
 */
static kernel_result_t init_servos(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_SERVOS)) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Servo initialization disabled.");
        return SYS_INIT_OK; // Servos not requested
    }
    
    // Initialize servo manager
    if (!servo_manager_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to initialize servo manager.");
        return SYS_INIT_ERROR_GENERAL;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Servo manager initialized");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize Memory Protection Unit
 * 
 * This function initializes only the MPU, without TrustZone.
 * 
 * @return SYS_INIT_OK if successful, error code otherwise
 */
static kernel_result_t init_mpu(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_MPU)) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "MPU support disabled.");
        return SYS_INIT_OK; // MPU not requested
    }
    
    // Initialize MPU
    if (!scheduler_mpu_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to initialize MPU.");
        return SYS_INIT_ERROR_MPU_TZ;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "MPU initialized successfully.");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize TrustZone
 * 
 * This function initializes only TrustZone security, without MPU.
 * 
 * @return SYS_INIT_OK if successful, error code otherwise
 */
static kernel_result_t init_trustzone(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_TZ)) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Trustzone support disabled.");
        return SYS_INIT_OK; // TrustZone not requested
    }
    
    // First check if TrustZone is supported by the hardware
    if (!scheduler_tz_is_supported()) {
        log_message(LOG_LEVEL_WARN, "Kernel Init", "TrustZone not supported by this hardware.");
        return SYS_INIT_OK; // Not an error, just not supported
    }
    

    log_message(LOG_LEVEL_INFO, "Kernel Init", "Initializing TrustZone Security Extension...");
    
    // Initialize TrustZone
    if (!scheduler_tz_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to initialize TrustZone");
        return SYS_INIT_ERROR_MPU_TZ;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "TrustZone initialized successfully");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize both MPU and TrustZone
 * 
 * This function replaces the original init_mpu_tz function,
 * but now calls the separate initialization functions.
 * 
 * @return SYS_INIT_OK if at least one subsystem initialized successfully
 */
static kernel_result_t init_mpu_tz(void) {
    kernel_result_t result;
    
    // Initialize MPU first
    result = init_mpu();
    if (result != SYS_INIT_OK) {
        return result;
    }
    
    // Then try to initialize TrustZone
    init_trustzone();
    // Even if TrustZone fails, continue as long as MPU is working
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize core subsystems in the correct order to avoid circular dependencies
 * 
 * This function initializes the spinlock manager and logging system in the
 * correct order to avoid circular dependencies.
 * 
 * @return SYS_INIT_OK if successful, error code otherwise
 */
static kernel_result_t init_core_subsystems(void) {
    // Step 1: Initialize spinlock manager core without logging
    if (!hw_spinlock_manager_init_no_logging()) {
        printf("ERROR: Failed to initialize hardware spinlock manager core\n");
        return SYS_INIT_ERROR_GENERAL;
    }
    
    // Step 2: Initialize logging core (without spinlocks yet)
    if (!log_init_core(NULL)) {
        printf("ERROR: Failed to initialize logging system core\n");
        return SYS_INIT_ERROR_LOGGER;
    }

    // Step 3: Initialize scheduler
    if (!scheduler_init()) {
        printf("ERROR: Failed to initialize scheduler\n");
        return SYS_INIT_ERROR_SCHEDULER;
    }
    
    // Step 4: Initialize logging as a scheduled task
    if (!log_init_as_task()) {
        printf("ERROR: Failed to initialize logging task\n");
        return SYS_INIT_ERROR_LOGGER;
    }
    
    // Step 4: Complete spinlock manager with logging enabled
    if (!hw_spinlock_manager_init_logging()) {
        printf("WARN: Could not complete spinlock manager initialization with logging\n");
        // Non-fatal - continue anyway
    }
    
    // Configure log destinations (start with console only)
    log_set_destinations(LOG_DEST_CONSOLE);
    
    // Log system initialization
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Core subsystems initialized");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the interrupt manager
 */
static kernel_result_t init_interrupt_manager(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_INTERRUPT_MGR)) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "Interrupt manager disabled.");
        return SYS_INIT_OK; // Interrupt manager not requested
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Initializing interrupt manager...");
    
    if (!interrupt_manager_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to initialize interrupt manager.");
        return SYS_INIT_ERROR_INTERRUPT_MGR;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Interrupt manager initialized successfully.");
    
    return SYS_INIT_OK;
}

/**
 * @brief Initialize the LED driver
 */
static kernel_result_t init_led_driver(void) {
    if (!(system_config.flags & SYS_INIT_FLAG_LED_DRIVER)) {
        log_message(LOG_LEVEL_INFO, "Kernel Init", "LED driver disabled.");
        return SYS_INIT_OK; // LED driver not requested
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Initializing LED driver...");
    
    if (!led_driver_kernel_init()) {
        log_message(LOG_LEVEL_ERROR, "Kernel Init", "Failed to initialize LED driver.");
        return SYS_INIT_ERROR_LED_DRIVER;
    }
    
    log_message(LOG_LEVEL_INFO, "Kernel Init", "LED driver initialized successfully.");
    
    return SYS_INIT_OK;
}