/**
* @file system_init.h
* @brief System initialization and startup sequence management
* @author Based on Robert Fudge's work
* @date 2025-05-14
*/

#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System initialization result codes
 */
typedef enum {
    SYS_INIT_OK = 0,              // Initialization successful
    SYS_INIT_ERROR_GENERAL,       // General error
    SYS_INIT_ERROR_SCHEDULER,     // Scheduler initialization failed
    SYS_INIT_ERROR_LOGGER,        // Logger initialization failed
    SYS_INIT_ERROR_SHELL,         // Shell initialization failed
    SYS_INIT_ERROR_SENSOR_MANAGER, // Sensor manager initialization failed
    SYS_INIT_ERROR_MPU_TZ,        // MPU/TrustZone initialization failed
    SYS_INIT_ERROR_HARDWARE       // Hardware initialization failed
} sys_init_result_t;

/**
 * @brief System initialization flags
 */
typedef enum {
    SYS_INIT_FLAG_NONE          = 0x00,
    SYS_INIT_FLAG_SHELL         = 0x01,  // Initialize shell
    SYS_INIT_FLAG_SENSORS       = 0x02,  // Initialize sensors
    SYS_INIT_FLAG_MPU_TZ        = 0x04,  // Initialize MPU/TrustZone
    SYS_INIT_FLAG_LOGGING       = 0x08,  // Initialize logging system
    SYS_INIT_FLAG_VERBOSE       = 0x10,  // Enable verbose output
    SYS_INIT_FLAG_WATCHDOG      = 0x20,  // Enable watchdog timer
    SYS_INIT_FLAG_DEFAULT       = 0x0B   // Default: Shell, Sensors, Logging
} sys_init_flags_t;

/**
 * @brief System initialization configuration
 */
typedef struct {
    sys_init_flags_t flags;         // Initialization flags
    uint32_t watchdog_timeout_ms;   // Watchdog timeout in milliseconds
    const char* app_name;           // Application name
    const char* app_version;        // Application version
} sys_init_config_t;

/**
 * @brief Initialize the system with custom configuration
 * 
 * @param config System initialization configuration
 * @return Initialization result code
 */
sys_init_result_t system_init(const sys_init_config_t* config);

/**
 * @brief Get default system initialization configuration
 * 
 * @param config Pointer to configuration structure to fill
 */
void system_init_get_default_config(sys_init_config_t* config);

/**
 * @brief Run the system main loop
 * 
 * @note This function never returns
 */
void system_run(void);

/**
 * @brief Register application commands with the shell
 * 
 * @note Called automatically during initialization if shell is enabled
 */
void system_register_commands(void);

/**
 * @brief Handle system shutdown
 * 
 * Performs cleanup and shutdown operations
 * 
 * @param restart If true, restart the system; otherwise, halt
 */
void system_shutdown(bool restart);

/**
 * @brief Get the time since system initialization
 * 
 * @return Time in milliseconds since initialization
 */
uint32_t system_get_uptime_ms(void);

/**
 * @brief Feed the watchdog to prevent system reset
 * 
 * @note Must be called regularly if watchdog is enabled
 */
void system_feed_watchdog(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_INIT_H