/**
* @file scheduler_tz.h
* @brief TrustZone security configuration for the scheduler
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-13
* 
* This module provides TrustZone-specific configuration for tasks running
* under the scheduler, enabling secure/non-secure transitions and proper
* isolation between security domains.
*/

#ifndef SCHEDULER_TZ_H
#define SCHEDULER_TZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "scheduler.h"

/**
 * @brief Security state for tasks
 */
typedef enum {
    TASK_SECURITY_SECURE = 0,         /**< Task runs in secure state */
    TASK_SECURITY_NON_SECURE = 1,     /**< Task runs in non-secure state */
    TASK_SECURITY_TRANSITIONAL = 2    /**< Task may transition between secure/non-secure */
} task_security_state_t;

/**
 * @brief Secure function call definition
 */
typedef struct {
    const char *name;                 /**< Function name */
    void *secure_gateway;             /**< Secure gateway entry point */
    void *non_secure_callable;        /**< Non-secure callable function */
} secure_function_t;

/**
 * @brief TrustZone configuration for a task
 */
typedef struct {
    uint32_t task_id;                      /**< Task ID to apply configuration to */
    task_security_state_t security_state;  /**< Task security state */
    secure_function_t *secure_functions;   /**< Array of secure functions accessible to task */
    uint8_t function_count;                /**< Number of secure functions */
} task_tz_config_t;

/**
 * @brief Initialize TrustZone support for the scheduler
 * 
 * Sets up security configuration for the scheduler. This should be
 * called during system initialization before tasks start.
 * 
 * @return true if initialization successful
 * @return false if initialization failed or TrustZone not supported
 */
bool scheduler_tz_init(void);

/**
 * @brief Configure TrustZone settings for a specific task
 * 
 * @param config Task TrustZone configuration
 * @return true if configuration successful
 * @return false if configuration failed
 */
bool scheduler_tz_configure_task(const task_tz_config_t *config);

/**
 * @brief Apply TrustZone settings before task execution
 * 
 * This function is called by the scheduler before switching to a task.
 * It applies the appropriate TrustZone settings for the task's security state.
 * 
 * @param task_id ID of the task to apply settings for
 * @return true if settings applied successfully
 * @return false if settings could not be applied
 */
bool scheduler_tz_apply_task_settings(uint32_t task_id);

/**
 * @brief Reset TrustZone settings after task execution
 * 
 * This function is called by the scheduler after a task completes.
 * It resets TrustZone settings to a known good state.
 * 
 * @param task_id ID of the task that was executing
 * @return true if settings reset successfully
 * @return false if settings could not be reset
 */
bool scheduler_tz_reset_task_settings(uint32_t task_id);

/**
 * @brief Check if TrustZone is enabled and available
 * 
 * @return true if TrustZone is enabled
 * @return false if TrustZone is not enabled
 */
bool scheduler_tz_is_enabled(void);

/**
 * @brief Get current security state
 * 
 * @return Current security state (secure or non-secure)
 */
task_security_state_t scheduler_tz_get_security_state(void);

/**
 * @brief Register a secure function for non-secure access
 * 
 * Makes a secure function callable from non-secure code through
 * the appropriate gateway mechanisms.
 * 
 * @param name Function name
 * @param secure_function Pointer to secure function
 * @param non_secure_callable Pointer to non-secure callable version (generated)
 * @return true if registration successful
 * @return false if registration failed
 */
bool scheduler_tz_register_secure_function(const char *name, 
                                          void *secure_function,
                                          void **non_secure_callable);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_TZ_H