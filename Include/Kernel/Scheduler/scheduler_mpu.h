/**
* @file scheduler_mpu.h
* @brief Memory Protection Unit configuration for the scheduler.
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [2025-05-20]
* 
* This module provides configuration functions for setting up MPU regions
* for tasks running under the scheduler. It is separated from TrustZone
* functionality to allow independent operation.
*/

#ifndef SCHEDULER_MPU_H
#define SCHEDULER_MPU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Maximum number of MPU regions per task
#define MAX_MPU_REGIONS_PER_TASK   8

/**
 * @defgroup mpu_enum MPU Enumerations
 * @{
 */

/**
 * @brief Memory access permissions.
 */
typedef enum {
    MPU_NO_ACCESS = 0,
    MPU_READ_ONLY = 1, 
    MPU_READ_WRITE = 2,
    MPU_READ_EXEC = 3,
    MPU_READ_WRITE_EXEC = 4
} mpu_access_permissions_t;

/** @} */ //end of mpu_enum group

/**
 * @defgroup mpu_struct MPU Structures
 * @{
 */

/**
 * @brief MPU region configuration structure.
 */
typedef struct {
    void *start_addr;           /**< Region start address. (must be aligned) */
    mpu_access_permissions_t access;        /**< Access permissions. */
    size_t size;                /**< Region size in bytes. (must be power of 2) */
    bool security;
    bool cacheable;             /**< Whether region is cacheable. */
    bool bufferable;            /**< Whether writes can be buffered. */
    bool shareable;             /**< Whether region is shareable between cores. */
} mpu_region_config_t;

/**
 * @brief Task memory protection configuration.
 */
typedef struct {
    uint32_t task_id;                      /**< Task ID to apply configuration to. */

    mpu_region_config_t regions[MAX_MPU_REGIONS_PER_TASK];          /**< Array of region configurations. */

    uint8_t region_count;                  /**< Number of regions in array. */
} task_mpu_config_t;

/**
 * @brief Status information for MPU fault reporting.
 */
typedef struct {
    uint32_t total_protected_tasks; /**< Number of tasks with MPU protection. */
    uint32_t fault_count;       /**< Total number of faults encountered. */
    uint32_t last_fault_address; /**< Address that caused the last fault. */
    uint32_t last_fault_type;   /**< Type of the last fault. */
    bool mpu_enabled;           /**< Whether MPU protection is currently enabled. */
    bool available;             /**< Not integrated, needed for building. */
    char fault_reason[64];      /**< Human-readable description of last fault. */
} mpu_status_info_t;

/**
 * @brief Performance statistics for MPU operations.
 */
typedef struct {
    uint64_t total_apply_time_us;      /**< Total time spent in apply operations. */
    uint64_t total_reset_time_us;      /**< Total time spent in reset operations. */
    uint64_t max_apply_time_us;        /**< Maximum time for a single apply operation. */
    uint64_t max_reset_time_us;        /**< Maximum time for a single reset operation. */
    uint32_t apply_settings_count;     /**< Number of apply operations. */
    uint32_t reset_settings_count;     /**< Number of reset operations. */
} mpu_perf_stats_t;

/** @} */ //end of mpu_struct group

/**
 * @defgroup mpu_api MPU Application Programming Interface
 * @{
 */

/**
 * @brief Apply MPU settings before task execution.
 * 
 * This function is called by the scheduler before switching to a task.
 * It applies the appropriate MPU settings for the task.
 * 
 * @param task_id ID of the task to apply settings for.
 * @return true if settings applied successfully.
 * @return false if settings could not be applied.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_apply_task_settings(uint32_t task_id);

/**
 * @brief Configure MPU settings for a specific task.
 * 
 * @param config Task MPU configuration.
 * @return true if configuration successful.
 * @return false if configuration failed.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_configure_task(const task_mpu_config_t *config);

/**
 * @brief Create default MPU configuration for a task.
 * 
 * Generates a sensible default MPU configuration for a task based on
 * its stack and code areas.
 * 
 * @param task_id Task ID to generate configuration for.
 * @param stack_start Start of task's stack.
 * @param stack_size Size of task's stack in bytes.
 * @param code_start Start of task's code.
 * @param code_size Size of task's code in bytes.
 * @param config Output parameter to store generated configuration.
 * @return true if configuration generated successfully.
 * @return false if configuration could not be generated.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_create_default_config(uint32_t task_id,
    void *stack_start, size_t stack_size, void *code_start,
    size_t code_size, task_mpu_config_t *config);

/**
 * @brief Enable or disable MPU protection for a task.
 * 
 * This is the main function to control MPU protection for a task.
 * It enables or disables the MPU based on the task's configuration.
 * 
 * @param task_id ID of the task to configure.
 * @param enable Whether to enable or disable protection.
 * @return true if successful, false if failed.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_enable_protection(uint32_t task_id, bool enable);

/**
 * @brief Get human-readable description of fault type.
 * 
 * @param fault_type The fault type code.
 * @return String description of the fault.
 */
__attribute__((section(".time_critical")))
const char* scheduler_mpu_get_fault_description(uint32_t fault_type);

/**
 * @brief Get performance statistics for MPU operations.
 * 
 * This function retrieves timing statistics for MPU operations,
 * which can be useful for performance debugging.
 * 
 * @param stats Output parameter to store statistics.
 * @return true if successful, false if failed.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_get_performance_stats(mpu_perf_stats_t *stats);

/**
 * @brief Get MPU protection status for a task.
 * 
 * @param task_id ID of the task to query.
 * @param is_protected Output parameter to store protection status.
 * @return true if successful, false if failed.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_get_protection_status(uint32_t task_id, bool *is_protected);

/**
 * @brief Get global MPU status information.
 * 
 * @param info Output parameter to store status information.
 * @return true if successful, false if failed.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_get_status(mpu_status_info_t *info);

/**
 * @brief Get current task's MPU configuration.
 * 
 * @param task_id Task ID to get configuration for.
 * @param config Output parameter to store configuration.
 * @return true if configuration retrieved successfully.
 * @return false if configuration could not be retrieved.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_get_task_config_minimal(uint32_t task_id, task_mpu_config_t *config);

/**
 * @brief Initialize the MPU for the scheduler.
 * 
 * Sets up default configurations and prepares the MPU.
 * This should be called once during system initialization.
 * 
 * @return true if initialization successful.
 * @return false if initialization failed.
 */
bool scheduler_mpu_init(void);

/**
 * @brief Check if address is accessible by the current task.
 * 
 * @param addr Address to check.
 * @param size Size of the memory region starting at addr.
 * @param write_access Whether write access is required.
 * @return true if address is accessible.
 * @return false if address is not accessible.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_is_accessible(void *addr, size_t size, bool write_access);

/**
 * @brief Check if MPU is available and enabled.
 * 
 * @return true if MPU is available and enabled.
 * @return false if MPU is not available or disabled.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_is_enabled(void);

/**
 * @brief Register a fault handler for MPU violations.
 * 
 * @param handler Function to call when an MPU fault occurs.
 * @return true if successful, false if failed.
 */
bool scheduler_mpu_register_fault_handler(void (*handler)(uint32_t task_id, void *fault_addr, uint32_t fault_type));

/**
 * @brief Remove task-specific MPU settings.
 * 
 * Reset MPU to default settings when task is not running.
 * 
 * @param task_id ID of the task to remove settings for.
 * @return true if settings removed successfully.
 * @return false if settings could not be removed.
 */
__attribute__((section(".time_critical")))
bool scheduler_mpu_reset_task_settings(uint32_t task_id);

/**
 * @brief Set global MPU enabled/disabled state.
 * 
 * This function allows temporarily disabling MPU for debugging
 * or performance comparison purposes.
 * 
 * @param enabled true to enable MPU, false to disable.
 * @return true if successful, false if failed.
 */
bool scheduler_mpu_set_global_enabled(bool enabled);

/**
 * @brief Test MPU protection by generating a controlled fault.
 * 
 * This function is for debugging purposes only and attempts to
 * generate a controlled MPU fault to test fault handling.
 * 
 * @param task_id ID of the task to test.
 * @return true if test was triggered, false if failed.
 */
bool scheduler_mpu_test_protection(uint32_t task_id);

/** @} */ //end of mpu_api group

/**
 * @defgroup mpu_cmd MPU Command Interface
 * @{
 */

/**
 * @brief MPU command handler.
 * 
 * This function handles the 'mpu' shell command which provides
 * configuration and debugging functionality for the Memory Protection Unit.
 * 
 * @param argc Argument count.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_mpu(int argc, char *argv[]);

/**
 * @brief Register MPU commands with the shell.
 * 
 * This function registers the MPU-related commands with the shell
 * system. It should be called during system initialization.
 */
void register_mpu_commands(void);

/** @} */ //end of mpu_cmd group


#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_MPU_H