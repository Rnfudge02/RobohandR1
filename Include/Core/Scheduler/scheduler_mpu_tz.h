/**
* @file scheduler_mpu_tz.h
* @brief Memory Protection Unit and TrustZone configuration for the scheduler
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-13
* 
* This module provides configuration functions for setting up MPU regions
* and TrustZone security attributes for tasks running under the scheduler.
* It handles memory protection between tasks and cores on the RP2350.
*/

#ifndef SCHEDULER_MPU_TZ_H
#define SCHEDULER_MPU_TZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Memory access permissions
 */
typedef enum {
    MPU_NO_ACCESS = 0,
    MPU_READ_ONLY = 1, 
    MPU_READ_WRITE = 2,
    MPU_READ_EXEC = 3,
    MPU_READ_WRITE_EXEC = 4
} mpu_access_t;

/**
 * @brief Memory region security attributes
 */
typedef enum {
    TZ_SECURE = 0,
    TZ_NON_SECURE = 1,
    TZ_NON_SECURE_CALLABLE = 2
} tz_security_t;

/**
 * @brief MPU region configuration structure
 */
typedef struct {
    void *start_addr;           /**< Region start address (must be aligned) */
    size_t size;                /**< Region size in bytes (must be power of 2) */
    mpu_access_t access;        /**< Access permissions */
    tz_security_t security;     /**< Security attributes */
    bool cacheable;             /**< Whether region is cacheable */
    bool bufferable;            /**< Whether writes can be buffered */
    bool shareable;             /**< Whether region is shareable between cores */
} mpu_region_config_t;

/**
 * @brief Task memory protection configuration
 */
typedef struct {
    uint32_t task_id;                      /**< Task ID to apply configuration to */
    mpu_region_config_t *regions;          /**< Array of region configurations */
    uint8_t region_count;                  /**< Number of regions in array */
} task_mpu_config_t;

/**
 * @brief Initialize the MPU and TrustZone for the scheduler
 * 
 * Sets up default configurations and prepares the MPU.
 * This should be called once during system initialization.
 * 
 * @return true if initialization successful
 * @return false if initialization failed
 */
bool scheduler_mpu_tz_init(void);

/**
 * @brief Configure MPU and TZ settings for a specific task
 * 
 * @param config Task MPU configuration
 * @return true if configuration successful
 * @return false if configuration failed
 */
bool scheduler_mpu_configure_task(const task_mpu_config_t *config);

/**
 * @brief Apply MPU settings before task execution
 * 
 * This function is called by the scheduler before switching to a task.
 * It applies the appropriate MPU and TZ settings for the task.
 * 
 * @param task_id ID of the task to apply settings for
 * @return true if settings applied successfully
 * @return false if settings could not be applied
 */
bool scheduler_mpu_apply_task_settings(uint32_t task_id);

/**
 * @brief Remove task-specific MPU settings
 * 
 * Reset MPU to default settings when task is not running.
 * 
 * @param task_id ID of the task to remove settings for
 * @return true if settings removed successfully
 * @return false if settings could not be removed
 */
bool scheduler_mpu_reset_task_settings(uint32_t task_id);

/**
 * @brief Create default MPU configuration for a task
 * 
 * Generates a sensible default MPU configuration for a task based on
 * its stack and code areas.
 * 
 * @param task_id Task ID to generate configuration for
 * @param stack_start Start of task's stack
 * @param stack_size Size of task's stack in bytes
 * @param code_start Start of task's code
 * @param code_size Size of task's code in bytes
 * @param config Output parameter to store generated configuration
 * @return true if configuration generated successfully
 * @return false if configuration could not be generated
 */
bool scheduler_mpu_create_default_config(uint32_t task_id, 
                                        void *stack_start, size_t stack_size,
                                        void *code_start, size_t code_size,
                                        task_mpu_config_t *config);

/**
 * @brief Get current task's MPU configuration
 * 
 * @param task_id Task ID to get configuration for
 * @param config Output parameter to store configuration
 * @return true if configuration retrieved successfully
 * @return false if configuration could not be retrieved
 */
bool scheduler_mpu_get_task_config(uint32_t task_id, task_mpu_config_t *config);

/**
 * @brief Check if address is accessible by the current task
 * 
 * @param addr Address to check
 * @param size Size of the memory region starting at addr
 * @param write_access Whether write access is required
 * @return true if address is accessible
 * @return false if address is not accessible
 */
bool scheduler_mpu_is_accessible(void *addr, size_t size, bool write_access);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_MPU_TZ_H