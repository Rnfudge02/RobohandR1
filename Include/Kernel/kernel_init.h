/**
* @file kernel_init.h
* @brief System initialization and startup sequence management.
* @author Based on Robert Fudge's work
* @date 2025-05-14
*/

#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup kernel_enum Kernel Enumerations
 * @{
 */

/**
 * @brief Kernel initialization result codes.
 */
typedef enum {
    SYS_INIT_OK = 0,              // Initialization successful.
    SYS_INIT_ERROR_GENERAL,       // General error.
    SYS_INIT_ERROR_SCHEDULER,     // Scheduler initialization failed.
    SYS_INIT_ERROR_LOGGER,        // Logger initialization failed.
    SYS_INIT_ERROR_SHELL,         // Shell initialization failed.
    SYS_INIT_ERROR_SENSOR_MANAGER, // Sensor manager initialization failed.
    SYS_INIT_ERROR_MPU_TZ,        // MPU/TrustZone initialization failed.
    SYS_INIT_ERROR_HARDWARE       // Hardware initialization failed.
} kernel_result_t;

/**
 * @brief Kernel initialization flags.
 */
typedef enum {
    SYS_INIT_FLAG_NONE          = 0x00,
    SYS_INIT_FLAG_SHELL         = 0x01,  // Initialize shell.            (0b1 << 0)
    SYS_INIT_FLAG_SENSORS       = 0x02,  // Initialize sensors.          (0b1 << 1)
    SYS_INIT_FLAG_WATCHDOG      = 0x04,  // Enable watchdog timer.       (0b1 << 2)
    SYS_INIT_FLAG_SERVOS        = 0x08,  // Initialize servos.           (0b1 << 3)
    SYS_INIT_FLAG_MPU           = 0x10,  // Initialize MPU.              (0b1 << 4)
    SYS_INIT_FLAG_TZ            = 0x20,  // Initialize TZ.               (0b1 << 5)
    SYS_INIT_FLAG_DEFAULT       = 0x04 | 0x02 | 0x01  // Default.
} kernel_flags_t;

/** @} */ // end of kernel_enum group

/**
 * @defgroup kernel_struct Kernel Data Structures
 * @{
 */

/**
 * @brief Kernel initialization configuration.
 */
typedef struct {
    uint32_t watchdog_timeout_ms;   // Watchdog timeout in milliseconds.
    kernel_flags_t flags;         // Initialization flags.
    const char* app_name;           // Application name.
    const char* app_version;        // Application version.
} kernel_config_t;

/** @} */ // end of kernel_struct

/**
 * @defgroup kernel_api Kernel Application Programming Interface
 * @{
 */

/**
 * @brief Feed the watchdog to prevent system reset.
 * 
 * @note Must be called regularly if watchdog is enabled.
 */
__attribute__((section(".time_critical")))
void kernel_feed_watchdog(void);

/**
 * @brief Get default kernel initialization configuration.
 * 
 * @param config Pointer to configuration structure to fill.
 */
void kernel_get_default_config(kernel_config_t* config);

/**
 * @brief Get the time since kernel initialization.
 * 
 * @return Time in milliseconds since initialization.
 */
__attribute__((section(".time_critical")))
uint32_t kernel_get_uptime_ms(void);

/**
 * @brief Initialize the kernel with custom configuration.
 * 
 * @param config Kernel initialization configuration.
 * @return Initialization result code.
 */
kernel_result_t kernel_init(const kernel_config_t* config);

/**
 * @brief Register application commands with the shell.
 * 
 * @note Called automatically during initialization if shell is enabled.
 */
void kernel_register_commands(void);

/**
 * @brief Run the kernel main loop.
 * 
 * @note This function never returns.
 */
__attribute__((section(".time_critical")))
void kernel_run(void);


/**
 * @brief Handle kernel shutdown.
 * 
 * Performs cleanup and shutdown operations.
 * 
 * @param restart If true, restart the kernel; otherwise, halt.
 */
void kernel_shutdown(bool restart);

/** @} */ // end of kernel_api group


#ifdef __cplusplus
}
#endif

#endif // KERNEL_INIT_H