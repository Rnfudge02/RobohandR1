/**
* @file scheduler_tz.h
* @brief TrustZone security configuration for the scheduler.
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [2025-05-20]
* 
* This module provides TrustZone-specific configuration for tasks running
* under the scheduler, enabling secure/non-secure transitions and proper
* isolation between security domains. This is separated from MPU functionality
* to allow independent operation.
*/

#ifndef SCHEDULER_TZ_H
#define SCHEDULER_TZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @defgroup tz_enum Trustzone Enumerations.
 * @{
 */

/**
 * @brief Memory region security attributes.
 */
typedef enum {
    TZ_SECURE = 0,
    TZ_NON_SECURE = 1,
    TZ_NON_SECURE_CALLABLE = 2
} tz_security_t;

/**
 * @brief Security state for tasks.
 */
typedef enum {
    TASK_SECURITY_SECURE = 0,         /**< Task runs in secure state. */
    TASK_SECURITY_NON_SECURE = 1,     /**< Task runs in non-secure state. */
    TASK_SECURITY_TRANSITIONAL = 2    /**< Task may transition between secure/non-secure. */
} task_security_state_t;

/** @} */ //end of tz_enum group

/**
 * @defgroup tz_struct Trustzone Structures.
 * @{
 */

/**
 * @brief Secure function call definition.
 */
typedef struct {
    void *secure_gateway;             /**< Secure gateway entry point. */
    void *non_secure_callable;        /**< Non-secure callable function. */
    const char *name;                 /**< Function name. */
} secure_function_t;

/**
 * @brief TrustZone configuration for a task.
 */
typedef struct {
    uint32_t task_id;                      /**< Task ID to apply configuration to. */
    task_security_state_t security_state;  /**< Task security state. */

    secure_function_t *secure_functions;   /**< Array of secure functions accessible to task. */

    uint8_t function_count;                /**< Number of secure functions. */
} task_tz_config_t;

/**
 * @brief Performance statistics for TrustZone operations.
 */
typedef struct {
    uint64_t total_apply_time_us;      /**< Total time spent in apply operations. */
    uint64_t total_reset_time_us;      /**< Total time spent in reset operations. */
    uint64_t max_apply_time_us;        /**< Maximum time for a single apply operation. */
    uint64_t max_reset_time_us;        /**< Maximum time for a single reset operation. */
    uint32_t apply_settings_count;     /**< Number of apply operations. */
    uint32_t reset_settings_count;     /**< Number of reset operations. */
    uint32_t state_transition_count;   /**< Number of security state transitions. */
} tz_perf_stats_t;

/**
 * @brief Status information for TrustZone operation.
 */
typedef struct {
    uint32_t secure_tasks;             /**< Number of tasks running in secure state. */
    uint32_t non_secure_tasks;         /**< Number of tasks running in non-secure state. */
    uint32_t transitional_tasks;       /**< Number of tasks that can transition states. */
    uint32_t sau_region_count;         /**< Number of SAU regions configured. */
    task_security_state_t current_state; /**< Current security state. */
    bool available;                    /**< Whether TrustZone is available on this hardware. */
    bool enabled;                      /**< Whether TrustZone is enabled. */
} tz_status_info_t;

/** @} */ //end of tz_struct group

/**
 * @defgroup tz_api Trustzone Application Programming Interface.
 * @{
 */

/**
 * @brief Apply TrustZone settings before task execution.
 * 
 * This function is called by the scheduler before switching to a task.
 * It applies the appropriate TrustZone settings for the task's security state.
 * 
 * @param task_id ID of the task to apply settings for.
 * @return true if settings applied successfully.
 * @return false if settings could not be applied.
 */
bool scheduler_tz_apply_task_settings(uint32_t task_id);

/**
 * @brief Configure TrustZone settings for a specific task.
 * 
 * @param config Task TrustZone configuration.
 * @return true if configuration successful.
 * @return false if configuration failed.
 */
bool scheduler_tz_configure_task(const task_tz_config_t *config);

/**
 * @brief Get performance statistics for TrustZone operations.
 * 
 * @param stats Output parameter to store statistics.
 * @return true on success, false on failure.
 */
bool scheduler_tz_get_performance_stats(tz_perf_stats_t *stats);

/**
 * @brief Get current security state.
 * 
 * @return Current security state. (secure or non-secure)
 */
task_security_state_t scheduler_tz_get_security_state(void);

/**
 * @brief Get TrustZone status information.
 * 
 * @param status Output parameter to store status information.
 * @return true on success, false on failure.
 */
bool scheduler_tz_get_status(tz_status_info_t *status);

/**
 * @brief Initialize TrustZone support for the scheduler.
 * 
 * Sets up security configuration for the scheduler. This should be
 * called during system initialization before tasks start.
 * 
 * @return true if initialization successful.
 * @return false if initialization failed or TrustZone not supported.
 */
bool scheduler_tz_init(void);

/**
 * @brief Check if TrustZone is enabled and available.
 * 
 * @return true if TrustZone is enabled.
 * @return false if TrustZone is not enabled.
 */
bool scheduler_tz_is_enabled(void);

/**
 * @brief Check if hardware supports TrustZone.
 * 
 * @return true if TrustZone is supported by hardware.
 * @return false if TrustZone is not supported.
 */
bool scheduler_tz_is_supported(void);

/**
 * @brief Register a secure function for non-secure access.
 * 
 * Makes a secure function callable from non-secure code through
 * the appropriate gateway mechanisms.
 * 
 * @param name Function name.
 * @param secure_function Pointer to secure function.
 * @param non_secure_callable Pointer to non-secure callable version. (generated)
 * @return true if registration successful.
 * @return false if registration failed.
 */
bool scheduler_tz_register_secure_function(const char *name, 
    void *secure_function, void **non_secure_callable);

/**
 * @brief Reset TrustZone settings after task execution.
 * 
 * This function is called by the scheduler after a task completes.
 * It resets TrustZone settings to a known good state.
 * 
 * @param task_id ID of the task that was executing.
 * @return true if settings reset successfully.
 * @return false if settings could not be reset.
 */
bool scheduler_tz_reset_task_settings(uint32_t task_id);

/**
 * @brief Set global TrustZone enabled/disabled state.
 * 
 * This function allows temporarily disabling TrustZone for debugging
 * or performance comparison purposes.
 * 
 * @param enabled true to enable TrustZone, false to disable.
 * @return true if successful, false if failed.
 */
bool scheduler_tz_set_global_enabled(bool enabled);

/** @} */ //end of tz_api group

/**
 * @defgroup tz_cmd Trustzone Commands, can be registered with a shell program.
 * @{
 */

/**
 * @brief TrustZone command handler.
 * 
 * This function handles the 'tz' shell command which provides
 * configuration and debugging functionality for TrustZone.
 * 
 * @param argc Argument count.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_tz(int argc, char *argv[]);

/**
 * @brief Register TrustZone commands with the shell.
 * 
 * This function registers the TrustZone-related commands with the shell
 * system. It should be called during system initialization.
 */
void register_tz_commands(void);

/** @} */ //end of tz_cmd group

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_TZ_H