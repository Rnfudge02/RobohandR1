/**
 * @file spinlock_manager.h
 * @brief Hardware spinlock manager for thread-safe operations.
 */

#ifndef SPINLOCK_MANAGER_H
#define SPINLOCK_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"

/**
 * @defgroup spinlock_man_enum Spinlock Manager Enumerations
 * @{
 */

// Spinlock categories for better organization
typedef enum {
    SPINLOCK_CAT_UNUSED = 0,
    SPINLOCK_CAT_SCHEDULER,
    SPINLOCK_CAT_SENSOR,
    SPINLOCK_CAT_SERVO,
    SPINLOCK_CAT_I2C,
    SPINLOCK_CAT_SPI,
    SPINLOCK_CAT_FAULT,
    SPINLOCK_CAT_LOGGING,
    SPINLOCK_CAT_MEMORY,
    SPINLOCK_CAT_FILESYSTEM,
    SPINLOCK_CAT_NETWORK,
    SPINLOCK_CAT_USER,
    SPINLOCK_CAT_DEBUG,
    SPINLOCK_CAT_COUNT
} spinlock_category_t;

typedef enum {
    SPINLOCK_INIT_PHASE_NONE = 0,    // Not initialized.
    SPINLOCK_INIT_PHASE_CORE,        // Core functionality initialized.
    SPINLOCK_INIT_PHASE_TRACKING,    // Spinlock tracking initialized.
    SPINLOCK_INIT_PHASE_FULL         // Fully initialized with logging.
} spinlock_init_phase_t;

/** @} */ // end of spinlock_man_enum group

/**
 * @defgroup spinlock_man_struct Spinlock Manager Data structures
 * @{
 */

// Information about a spinlock
typedef struct {
    bool allocated;                  // Whether this spinlock is allocated.
    spinlock_category_t category;    // Category of the spinlock.
    uint32_t owner_task_id;          // Task ID that currently owns the spinlock.
    const char* owner_name;          // Name of the component that allocated the spinlock.
    uint64_t last_acquired_time;     // Time when the spinlock was last acquired.
    uint64_t total_locked_time_us;   // Total time spent locked.
    uint64_t max_locked_time_us;     // Maximum time spent locked.
    uint32_t acquisition_count;      // Number of times the spinlock has been acquired.
} spinlock_info_t;

typedef struct {
    const char* owner_name;          // Name to identify the spinlock.
    spinlock_category_t category;    // Category for organization.
    uint spin_num;                   // Hardware spinlock number.
} spinlock_registration_t;

/** @} */ // end of spinlock_man_struct group

typedef bool (*spinlock_registration_callback_t)(spinlock_init_phase_t phase, void* context);

/**
 * @defgroup spinlock_man_api Spinlock Manager Application Programming Interface
 * @{
 */

/**
 * @brief Acquire the spinlock.
 *
 * @param spinlock_num Argument count.
 * @param task_id ID of task acquiring the lock.
 * @return Save state used to release lock.
 */
uint32_t hw_spinlock_acquire(uint32_t spinlock_num, uint32_t task_id);

/**
 * @brief Allocate a spinlock to a program.
 *
 * @param category Category to sort spinlock under.
 * @param owner_name Name of spinlock owner.
 * @return Spinlock number allocated to the program.
 */
uint32_t hw_spinlock_allocate(spinlock_category_t category, const char* owner_name);

/**
 * @brief Special bootstrap spinlock allocator - no dependencies.
 * 
 * For bootstrap components that need spinlocks during early initialization.
 * These spinlocks will be registered with the manager later.
 * 
 * @param self_tracking Set to true if this component will register its spinlock later.
 * @return Hardware spinlock number or UINT_MAX on failure.
 */
uint hw_spinlock_bootstrap_claim(bool self_tracking);

/**
 * @brief Sensor manager command handler.
 *
 * @param component_name String representing the component name.
 * @param callback Registration callback.
 * @param context Pointer to the structure holding the spinlock manager context.
 * @return 0 on success, non-zero on error.
 */
bool hw_spinlock_register_component(const char* component_name, spinlock_registration_callback_t callback, void* context);

/**
 * @brief Register an external spinlock with the spinlock manager.
 *
 * @param spinlock_num Number of previously acquired spinlock.
 * @param category Category the spinlock should be sorted under.
 * @param owner_name String representing function owning the spinlock.
 * @return 0 on success, non-zero on error.
 */
bool hw_spinlock_register_external(uint32_t spinlock_num, spinlock_category_t category, const char* owner_name);

/**
 * @brief Free a hardware spinlock.
 *
 * @param spinlock_num Spinlock number to free.
 * @return 0 on success, non-zero on error.
 */
bool hw_spinlock_free(uint32_t spinlock_num);

/**
 * @brief Get the number of spinlocks allocated for a specific category.
 *
 * @param category Category to retrieve count of.
 * @return number of spinlocks allocated under specified category.
 */
uint32_t hw_spinlock_get_count_by_category(spinlock_category_t category);

/**
 * @brief Retrieve information about a specific spinlock.
 *
 * @param spinlock_num Spinlock number to check.
 * @param info Pointer to array structure to populate.
 * @return 0 on success, non-zero on error.
 */
bool hw_spinlock_get_info(uint32_t spinlock_num, spinlock_info_t* info);

/**
 * @brief Retrieve the spinlock managers initialization phase.
 *
 * @param argc Argument count.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero on error.
 */
spinlock_init_phase_t hw_spinlock_get_init_phase(void);

/**
 * @brief Get total spinlock count.
 *
 * @return total number of spinlocks allocated.
 */
uint32_t hw_spinlock_get_total_count(void);

/**
 * @brief Initialize spinlock manager.
 *
 * @return 0 if error occurs.
 * @return 1 if successful.
 */
bool hw_spinlock_manager_init(void);

/**
 * @brief Initialize spinlock manager with logging functionality.
 *
 * @return 0 if error occurs.
 * @return 1 if successful.
 */
bool hw_spinlock_manager_init_logging(void);

/**
 * @brief Initialize spinlock manager without logging functionality.
 *
 * @return 0 if error occurs.
 * @return 1 if successful.
 */
bool hw_spinlock_manager_init_no_logging(void);

/**
 * @brief Check if the core spinlock manager functionality is present.
 *
 * @return 0 if uninitialized.
 * @return 1 if initialized.
 */
bool hw_spinlock_manager_is_core_initialized(void);

/**
 * @brief Check if the spinlock manager is fully initialized.
 *
 * @return 0 if uninitialized, 1 if initialized.
 */
bool hw_spinlock_manager_is_fully_initialized(void);

/**
 * @brief Print spinlock usage statistics.
 *
 * @param detailed Provide detailed usage statistics.
 */
void hw_spinlock_print_usage(bool detailed);

/**
 * @brief Register a callback with the spinlock manager.
 *
 * @param callback Callback function pointer.
 * @param context Spinlock manager context.
 * @return 0 on success, non-zero on error.
 */
bool hw_spinlock_manager_register_callback(bool (*callback)(void*), void* context);

/**
 * @brief Register the spinlock manager with the scheduler.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
bool hw_spinlock_manager_register_with_scheduler(void);

/**
 * @brief Release a specific hardware spinlock.
 *
 * @param spinlock_num Number of spinlock to release.
 * @param save_val Value that was returned from the acquisition operation.
 */
void hw_spinlock_release(uint32_t spinlock_num, uint32_t save_val);

/**
 * @brief Release all hardware spinlocks with an associated task ID.
 *
 * @param task_id Task ID to release spinlocks off.
 * @return uint32_t.
 */
uint32_t hw_spinlock_release_by_task(uint32_t task_id);

/** @} */ // end of spinlock_man_api group

/**
 * @defgroup spinlock_man_cmd Spinlock Manager Command Interface
 * @{
 */

/**
 * @brief Register the spinlock manager commands with a shell.
 */
void register_spinlock_commands(void);

/** @} */ // end of spinlock_man_cmd group

#endif // SPINLOCK_MANAGER_H