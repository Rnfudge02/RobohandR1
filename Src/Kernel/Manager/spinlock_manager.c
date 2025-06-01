/**
 * @file spinlock_manager.c
 * @brief Hardware spinlock manager implementation
 */

#include "spinlock_manager.h"
#include "log_manager.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "usb_shell.h"

// Total number of hardware spinlocks available on RP2350
#define HW_SPINLOCK_COUNT 32
#define MAX_BOOTSTRAP_SPINLOCKS 10
#define MAX_REGISTERED_COMPONENTS 20

// RP2350 SIO Spinlock hardware registers
#define SIO_BASE                    0xd0000000
#define SIO_SPINLOCK_BASE          (SIO_BASE + 0x100)  // Spinlock 0-31 at 0x100-0x17c
#define SIO_SPINLOCK_STATE         (SIO_BASE + 0x05c)  // Shows which locks are claimed

// Helper macros
#define SPINLOCK_REG(n)            (SIO_SPINLOCK_BASE + ((n) * 4))
#define READ_SPINLOCK_REG(n)       (*(volatile uint32_t*)SPINLOCK_REG(n))
#define READ_SPINLOCK_STATE()      (*(volatile uint32_t*)SIO_SPINLOCK_STATE)

// Information about all hardware spinlocks
static spinlock_info_t spinlock_info[HW_SPINLOCK_COUNT];

// Bitmap of allocated spinlocks, separate from SDK's internal tracking
static uint32_t allocated_spinlocks = 0;

// Our own spinlock for protecting the manager data structures
static uint manager_lock_num = UINT32_MAX;
static spin_lock_t* manager_lock = NULL;

// Current initialization phase
static spinlock_init_phase_t current_init_phase = SPINLOCK_INIT_PHASE_NONE;

// Bootstrap spinlock tracking
typedef struct {
    uint spin_num;
    bool registered;
} bootstrap_spinlock_t;

static bootstrap_spinlock_t bootstrap_spinlocks[MAX_BOOTSTRAP_SPINLOCKS];
static int bootstrap_spinlock_count = 0;

// Component registration tracking
typedef struct {
    const char* component_name;
    spinlock_registration_callback_t callback;
    void* context;
} registered_component_t;

static registered_component_t registered_components[MAX_REGISTERED_COMPONENTS];
static int registered_component_count = 0;

// Initialization flags
static bool core_initialized = false;
static bool logging_initialized = false;

// Forward declarations for internal functions
static void hw_spinlock_task_completion_callback(uint32_t task_id);
static const char* hw_spinlock_category_to_string(spinlock_category_t category);

void hw_spinlock_print_usage(bool detailed);
void hw_spinlock_print_usage_compact(bool detailed);
void hw_spinlock_print_usage_multiline(void);
void hw_spinlock_print_usage_wide(bool detailed);
void hw_spinlock_print_hardware_registers(void);

/**
 * @brief Track a bootstrap spinlock
 * 
 * @param spin_num Hardware spinlock number
 * @return true if tracked successfully
 */
static bool bootstrap_track_spinlock(uint spin_num) {
    if (bootstrap_spinlock_count >= MAX_BOOTSTRAP_SPINLOCKS) {
        return false;
    }
    
    bootstrap_spinlocks[bootstrap_spinlock_count].spin_num = spin_num;
    bootstrap_spinlocks[bootstrap_spinlock_count].registered = false;
    bootstrap_spinlock_count++;
    
    return true;
}

/**
 * @brief Get current spinlock manager initialization phase
 */
spinlock_init_phase_t hw_spinlock_get_init_phase(void) {
    return current_init_phase;
}

/**
 * @brief Register a component with the spinlock manager
 * 
 * @param component_name Name for identification
 * @param callback Function to call during initialization phases
 * @param context Context to pass to the callback
 * @return true if registered successfully
 */
bool hw_spinlock_register_component(
    const char* component_name,
    spinlock_registration_callback_t callback,
    void* context
) {
    if (registered_component_count >= MAX_REGISTERED_COMPONENTS) {
        return false;
    }
    
    registered_components[registered_component_count].component_name = component_name;
    registered_components[registered_component_count].callback = callback;
    registered_components[registered_component_count].context = context;
    registered_component_count++;
    
    // If we're already initialized, call the callback immediately with the current phase
    if (current_init_phase != SPINLOCK_INIT_PHASE_NONE) {
        callback(current_init_phase, context);
    }
    
    return true;
}

/**
 * @brief Initialize the core functionality of the spinlock manager
 */
bool hw_spinlock_manager_init_core(void) {
    if (current_init_phase != SPINLOCK_INIT_PHASE_NONE) {
        return true; // Already initialized
    }
    
    // Clear spinlock info array
    memset(spinlock_info, 0, sizeof(spinlock_info));
    
    // Claim a spinlock for the manager itself
    manager_lock_num = spin_lock_claim_unused(true);
    if (manager_lock_num == UINT32_MAX) {
        printf("ERROR: SpinlockMgr - Failed to claim spinlock for manager\n");
        return false;
    }
    
    manager_lock = spin_lock_instance(manager_lock_num);
    
    // Mark the manager's own spinlock as allocated in our tracking
    allocated_spinlocks |= (1u << manager_lock_num);
    
    // Set info for the manager's own spinlock
    spinlock_info[manager_lock_num].allocated = true;
    spinlock_info[manager_lock_num].category = SPINLOCK_CAT_SCHEDULER;
    spinlock_info[manager_lock_num].owner_task_id = 0; // System
    spinlock_info[manager_lock_num].owner_name = "SpinlockManager";
    
    printf("INFO: SpinlockMgr - Core functionality initialized\n");
    current_init_phase = SPINLOCK_INIT_PHASE_CORE;
    
    // Notify all registered components about this phase
    for (int i = 0; i < registered_component_count; i++) {
        registered_components[i].callback(current_init_phase, registered_components[i].context);
    }
    
    return true;
}

/**
 * @brief Initialize the tracking functionality of the spinlock manager
 */
bool hw_spinlock_manager_init_tracking(void) {
    if ((current_init_phase < SPINLOCK_INIT_PHASE_CORE) && !hw_spinlock_manager_init_core()) {
        return false;
    }
    
    if (current_init_phase >= SPINLOCK_INIT_PHASE_TRACKING) {
        return true; // Already initialized
    }
    
    // Register bootstrap spinlocks
    for (int i = 0; i < bootstrap_spinlock_count; i++) {
        if (!bootstrap_spinlocks[i].registered) {
            // Register with a temporary name
            char temp_name[32];
            snprintf(temp_name, sizeof(temp_name), "bootstrap_spin_%u", bootstrap_spinlocks[i].spin_num);
            
            // Mark the spinlock as allocated in our tracking
            allocated_spinlocks |= (1u << bootstrap_spinlocks[i].spin_num);
            
            // Set spinlock info
            spinlock_info[bootstrap_spinlocks[i].spin_num].allocated = true;
            spinlock_info[bootstrap_spinlocks[i].spin_num].category = SPINLOCK_CAT_UNUSED;
            spinlock_info[bootstrap_spinlocks[i].spin_num].owner_task_id = 0;
            spinlock_info[bootstrap_spinlocks[i].spin_num].owner_name = strdup(temp_name);
            
            bootstrap_spinlocks[i].registered = true;
        }
    }
    
    printf("INFO: SpinlockMgr - Tracking initialized, registered %d bootstrap spinlocks\n", 
           bootstrap_spinlock_count);
    
    current_init_phase = SPINLOCK_INIT_PHASE_TRACKING;
    
    // Notify all registered components about this phase
    for (int i = 0; i < registered_component_count; i++) {
        registered_components[i].callback(current_init_phase, registered_components[i].context);
    }
    
    return true;
}

/**
 * @brief Complete the initialization of the spinlock manager with logging
 */
bool hw_spinlock_manager_init_full(void) {
    if ((current_init_phase < SPINLOCK_INIT_PHASE_TRACKING) && !hw_spinlock_manager_init_tracking()) {
        return false;
    }
    
    if (current_init_phase >= SPINLOCK_INIT_PHASE_FULL) {
        return true; // Already fully initialized
    }
    
    // Now we can use the logging system
    if (log_is_initialized()) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Spinlock manager fully initialized");
    } else {
        printf("INFO: SpinlockMgr - Fully initialized (logging not available)\n");
    }
    
    current_init_phase = SPINLOCK_INIT_PHASE_FULL;
    
    // Notify all registered components about this phase
    for (int i = 0; i < registered_component_count; i++) {
        registered_components[i].callback(current_init_phase, registered_components[i].context);
    }
    
    return true;
}

/**
 * @brief Compatibility function for backward compatibility
 */
bool hw_spinlock_manager_init(void) {
    bool success = hw_spinlock_manager_init_core();
    
    if (success) {
        success = hw_spinlock_manager_init_tracking();
    }
    
    if (success) {
        success = hw_spinlock_manager_init_full();
    }
    
    return success;
}

/**
 * @brief Special bootstrap spinlock allocator - no dependencies
 * 
 * For bootstrap components that need spinlocks during early initialization.
 * These spinlocks will be registered with the manager later.
 * 
 * @param self_tracking Set to true if this component will register its spinlock later
 * @return Hardware spinlock number or UINT_MAX on failure
 */
uint hw_spinlock_bootstrap_claim(bool self_tracking) {
    // Direct hardware claim without manager dependencies
    uint spin_num = spin_lock_claim_unused(true);
    
    // If self-tracking, the component will register itself later
    // Otherwise, we'll track it through an internal list
    if (!self_tracking && spin_num != UINT_MAX) {
        // Add to a simple bootstrap tracking array
        // This is a private array in spinlock_manager.c
        bootstrap_track_spinlock(spin_num);
    }
    
    return spin_num;
}

bool hw_spinlock_manager_init_logging(void) {
    if (!core_initialized) {
        return false; // Core init not done yet
    }
    
    if (logging_initialized) {
        return true; // Logging already initialized
    }
    
    // Additional initialization that depends on logging goes here
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Spinlock manager logging initialized");
    logging_initialized = true;
    
    return true;
}

bool hw_spinlock_manager_init_no_logging(void) {
    if (core_initialized) {
        return true; // Already initialized
    }
    
    // Clear spinlock info array
    memset(spinlock_info, 0, sizeof(spinlock_info));
    
    // Claim a spinlock for the manager itself
    // This is special case - we must use direct SDK function
    manager_lock_num = spin_lock_claim_unused(true);
    if (manager_lock_num == UINT32_MAX) {
        printf("ERROR: SpinlockMgr - Failed to claim spinlock for manager\n");
        return false;
    }
    
    manager_lock = spin_lock_instance(manager_lock_num);
    
    // Mark the manager's own spinlock as allocated in our tracking
    allocated_spinlocks |= (1u << manager_lock_num);
    
    // Set info for the manager's own spinlock
    spinlock_info[manager_lock_num].allocated = true;
    spinlock_info[manager_lock_num].category = SPINLOCK_CAT_SCHEDULER;
    spinlock_info[manager_lock_num].owner_task_id = 0; // System
    spinlock_info[manager_lock_num].owner_name = "SpinlockManager";
    
    printf("INFO: SpinlockMgr - Hardware spinlock manager core initialized\n");
    core_initialized = true;
    
    return true;
}

/**
 * @brief Register with the scheduler
 */
bool hw_spinlock_manager_register_with_scheduler(void) {
    if (!core_initialized) {
        log_message(LOG_LEVEL_ERROR, "SpinlockMgr", "Cannot register with scheduler - not initialized");
        return false;
    }
    
    // Register a task completion callback with the scheduler
    // Note: This assumes your scheduler has support for task completion callbacks
    // You may need to add this feature to your scheduler
    
    // For this example, we're assuming a hypothetical function exists:
    // scheduler_register_task_completion_callback(hw_spinlock_task_completion_callback);
    
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Registered with scheduler for task completion callbacks");
    return true;
}

/**
 * @brief Register an externally created spinlock with the manager
 */
bool hw_spinlock_register_external(uint32_t spinlock_num, spinlock_category_t category, const char* owner_name) {
    if (!core_initialized || spinlock_num >= HW_SPINLOCK_COUNT) {
        return false;
    }
    
    // Acquire manager lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Check if already in use
    if (allocated_spinlocks & (1u << spinlock_num)) {
        spin_unlock(manager_lock, save);
        return false;
    }
    
    // Mark as allocated in our tracking
    allocated_spinlocks |= (1u << spinlock_num);
    
    // Set spinlock info
    spinlock_info[spinlock_num].allocated = true;
    spinlock_info[spinlock_num].category = category;
    spinlock_info[spinlock_num].owner_task_id = 0; // Initially system-owned
    spinlock_info[spinlock_num].owner_name = owner_name;
    spinlock_info[spinlock_num].acquisition_count = 0;
    spinlock_info[spinlock_num].total_locked_time_us = 0;
    spinlock_info[spinlock_num].max_locked_time_us = 0;
    
    if (logging_initialized) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Registered external spinlock %lu for %s (%s)",
                 spinlock_num, owner_name, hw_spinlock_category_to_string(category));
    } else {
        printf("INFO: SpinlockMgr - Registered external spinlock %lu for %s\n", 
               spinlock_num, owner_name);
    }
    
    spin_unlock(manager_lock, save);
    return true;
}

/**
 * @brief Allocate a spinlock by category
 */
uint32_t hw_spinlock_allocate(spinlock_category_t category, const char* owner_name) {
    if (!core_initialized && !hw_spinlock_manager_init()) {
        return UINT32_MAX;
    }
    
    if (category == SPINLOCK_CAT_UNUSED || category >= SPINLOCK_CAT_COUNT) {
        log_message(LOG_LEVEL_ERROR, "SpinlockMgr", "Invalid spinlock category: %d", category);
        return UINT32_MAX;
    }
    
    // Acquire manager lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Find an available spinlock
    uint32_t spinlock_num = UINT32_MAX;
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        // Skip the manager's own spinlock
        if (i == manager_lock_num) {
            continue;
        }
        
        // Check if this spinlock is available
        if (!(allocated_spinlocks & (1u << i))) {
            spinlock_num = i;
            break;
        }
    }
    
    if (spinlock_num == UINT32_MAX) {
        // No spinlocks available
        log_message(LOG_LEVEL_ERROR, "SpinlockMgr", "No available spinlocks for %s", owner_name);
        spin_unlock(manager_lock, save);
        return UINT32_MAX;
    }
    
    // Claim the spinlock from the SDK
    // Note: This should never fail since we already checked availability
    spin_lock_claim(spinlock_num);
        
    
    // Mark as allocated in our tracking
    allocated_spinlocks |= (1u << spinlock_num);
    
    // Set spinlock info
    spinlock_info[spinlock_num].allocated = true;
    spinlock_info[spinlock_num].category = category;
    spinlock_info[spinlock_num].owner_task_id = 0; // Initially system-owned
    spinlock_info[spinlock_num].owner_name = owner_name;
    spinlock_info[spinlock_num].acquisition_count = 0;
    spinlock_info[spinlock_num].total_locked_time_us = 0;
    spinlock_info[spinlock_num].max_locked_time_us = 0;
    
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Allocated spinlock %lu for %s (%s)",
             spinlock_num, owner_name, hw_spinlock_category_to_string(category));
    
    spin_unlock(manager_lock, save);
    return spinlock_num;
}

/**
 * @brief Free a previously allocated spinlock
 */
bool hw_spinlock_free(uint32_t spinlock_num) {
    if (!core_initialized || spinlock_num >= HW_SPINLOCK_COUNT) {
        return false;
    }
    
    // Acquire manager lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Check if this spinlock is allocated
    if (!(allocated_spinlocks & (1u << spinlock_num))) {
        // Not allocated
        spin_unlock(manager_lock, save);
        return false;
    }
    
    // Cannot free the manager's own spinlock
    if (spinlock_num == manager_lock_num) {
        log_message(LOG_LEVEL_ERROR, "SpinlockMgr", "Cannot free manager's own spinlock");
        spin_unlock(manager_lock, save);
        return false;
    }
    
    // Mark as unallocated in our tracking
    allocated_spinlocks &= ~(1u << spinlock_num);
    
    // Unclaim the spinlock from the SDK
    spin_lock_unclaim(spinlock_num);
    
    // Clear spinlock info
    spinlock_info[spinlock_num].allocated = false;
    spinlock_info[spinlock_num].category = SPINLOCK_CAT_UNUSED;
    spinlock_info[spinlock_num].owner_task_id = 0;
    spinlock_info[spinlock_num].owner_name = NULL;
    
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Freed spinlock %lu", spinlock_num);
    
    spin_unlock(manager_lock, save);
    return true;
}

/**
 * @brief Acquire a spinlock
 */
uint32_t hw_spinlock_acquire(uint32_t spinlock_num, uint32_t task_id) {
    if (!core_initialized || spinlock_num >= HW_SPINLOCK_COUNT) {
        // Return a dummy save value
        return 0;
    }
    
    // Check if this spinlock is allocated
    if (!(allocated_spinlocks & (1u << spinlock_num))) {
        log_message(LOG_LEVEL_ERROR, "SpinlockMgr", "Cannot acquire unallocated spinlock %lu", spinlock_num);
        return 0;
    }
    
    // Record acquisition time and task
    uint64_t acquire_time = time_us_64();
    spinlock_info[spinlock_num].last_acquired_time = acquire_time;
    spinlock_info[spinlock_num].owner_task_id = task_id;
    spinlock_info[spinlock_num].acquisition_count++;
    
    // Actually acquire the hardware spinlock
    spin_lock_t* lock = spin_lock_instance(spinlock_num);
    uint32_t save = spin_lock_blocking(lock);
    
    return save;
}

/**
 * @brief Release a spinlock
 */
void hw_spinlock_release(uint32_t spinlock_num, uint32_t save_val) {
    if (!core_initialized || spinlock_num >= HW_SPINLOCK_COUNT) {
        return;
    }
    
    // Check if this spinlock is allocated
    if (!(allocated_spinlocks & (1u << spinlock_num))) {
        log_message(LOG_LEVEL_ERROR, "SpinlockMgr", "Cannot release unallocated spinlock %lu", spinlock_num);
        return;
    }
    
    // Record release time and update statistics
    uint64_t release_time = time_us_64();
    uint64_t held_time = release_time - spinlock_info[spinlock_num].last_acquired_time;
    
    // Update statistics with manager lock
    uint32_t mgr_save = spin_lock_blocking(manager_lock);
    
    spinlock_info[spinlock_num].total_locked_time_us += held_time;
    if (held_time > spinlock_info[spinlock_num].max_locked_time_us) {
        spinlock_info[spinlock_num].max_locked_time_us = held_time;
    }
    
    spin_unlock(manager_lock, mgr_save);
    
    // Actually release the hardware spinlock
    spin_lock_t* lock = spin_lock_instance(spinlock_num);
    spin_unlock(lock, save_val);
}

/**
 * @brief Get spinlock information
 */
bool hw_spinlock_get_info(uint32_t spinlock_num, spinlock_info_t* info) {
    if (!core_initialized || spinlock_num >= HW_SPINLOCK_COUNT || info == NULL) {
        return false;
    }
    
    // Acquire manager lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Copy spinlock info
    *info = spinlock_info[spinlock_num];
    
    spin_unlock(manager_lock, save);
    return true;
}

/**
 * @brief Get count of allocated spinlocks by category
 */
uint32_t hw_spinlock_get_count_by_category(spinlock_category_t category) {
    if (!core_initialized || category >= SPINLOCK_CAT_COUNT) {
        return 0;
    }
    
    // Acquire manager lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        if (spinlock_info[i].allocated && spinlock_info[i].category == category) {
            count++;
        }
    }
    
    spin_unlock(manager_lock, save);
    return count;
}

/**
 * @brief Get total count of allocated spinlocks
 */
uint32_t hw_spinlock_get_total_count(void) {
    if (!core_initialized) {
        return 0;
    }
    
    // Count the number of bits set in allocated_spinlocks
    uint32_t count = 0;
    uint32_t bits = allocated_spinlocks;
    
    while (bits) {
        count += bits & 1;
        bits >>= 1;
    }
    
    return count;
}

/**
 * @brief Release all spinlocks owned by a specific task
 */
uint32_t hw_spinlock_release_by_task(uint32_t task_id) {
    if (!core_initialized || task_id == 0) {
        return 0;
    }
    
    // Acquire manager lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    uint32_t count = 0;
    
    // Check each spinlock
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        if (spinlock_info[i].allocated && 
            spinlock_info[i].owner_task_id == task_id) {
            
            // Log a warning - this indicates a potential leak
            log_message(LOG_LEVEL_WARN, "SpinlockMgr", "Task %lu left spinlock %lu allocated (%s)",
                   task_id, i, spinlock_info[i].owner_name);
            
            // Reset ownership but don't free the spinlock
            // This allows the component that allocated it to continue using it
            spinlock_info[i].owner_task_id = 0;
            count++;
        }
    }
    
    spin_unlock(manager_lock, save);
    
    if (count > 0) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Released %lu spinlocks owned by task %lu", 
                count, task_id);
    }
    
    return count;
}



/**
 * @brief Task completion callback
 */
static void hw_spinlock_task_completion_callback(uint32_t task_id) {
    // Release any spinlocks owned by this task
    hw_spinlock_release_by_task(task_id);
}

/**
 * @brief Convert category to string
 */
static const char* hw_spinlock_category_to_string(spinlock_category_t category) {
    switch (category) {
        case SPINLOCK_CAT_UNUSED:    return "Unused";
        case SPINLOCK_CAT_SCHEDULER: return "Scheduler";
        case SPINLOCK_CAT_SENSOR:    return "Sensor";
        case SPINLOCK_CAT_SERVO:     return "Servo";
        case SPINLOCK_CAT_I2C:       return "I2C";
        case SPINLOCK_CAT_SPI:       return "SPI";
        case SPINLOCK_CAT_FAULT:     return "Fault";
        case SPINLOCK_CAT_LOGGING:   return "Logging";
        case SPINLOCK_CAT_MEMORY:    return "Memory";
        case SPINLOCK_CAT_FILESYSTEM: return "Filesystem";
        case SPINLOCK_CAT_NETWORK:   return "Network";
        case SPINLOCK_CAT_USER:      return "User";
        case SPINLOCK_CAT_DEBUG:     return "Debug";
        default:                     return "Unknown";
    }
}

/**
 * @brief Shell command handler for spinlock management
 */
/**
 * @brief Enhanced shell command handler for spinlock management
 */
int cmd_spinlock(int argc, char *argv[]) {
    if (argc < 2) {
        // Default command is "status"
        hw_spinlock_print_usage(false);
        return 0;
    }
    
    if (strcmp(argv[1], "status") == 0) {
        hw_spinlock_print_usage(false);
    } 
    else if (strcmp(argv[1], "detail") == 0) {
        hw_spinlock_print_usage(true);
    }
    else if (strcmp(argv[1], "compact") == 0) {
        hw_spinlock_print_usage_compact(true);
    }
    else if (strcmp(argv[1], "full") == 0) {
        hw_spinlock_print_usage_multiline();
    }
    else if (strcmp(argv[1], "wide") == 0) {
        hw_spinlock_print_usage_wide(true);
    }
    else if (strcmp(argv[1], "sys") == 0) {
        hw_spinlock_print_hardware_registers();
    }
    else if (strcmp(argv[1], "free") == 0) {
        if (argc < 3) {
            printf("Usage: spinlock free <spinlock_num>\n");
            return 1;
        }
        
        uint32_t spinlock_num = strtoul(argv[2], NULL, 0);
        if (hw_spinlock_free(spinlock_num)) {
            printf("Spinlock %lu freed successfully\n", spinlock_num);
        } else {
            printf("Failed to free spinlock %lu\n", spinlock_num);
            return 1;
        }
    }
    else if (strcmp(argv[1], "help") == 0) {
        printf("Hardware Spinlock Manager Commands:\n");
        printf("  spinlock             - Display spinlock status summary\n");
        printf("  spinlock status      - Display spinlock status summary\n");
        printf("  spinlock detail      - Display detailed table view\n");
        printf("  spinlock compact     - Display compact detailed view\n");
        printf("  spinlock full        - Display full multi-line view\n");
        printf("  spinlock free <num>  - Free a specific spinlock\n");
        printf("  spinlock help        - Display this help message\n");
    }
    else {
        printf("Unknown spinlock command: %s\n", argv[1]);
        printf("Type 'spinlock help' for available commands\n");
        return 1;
    }
    
    return 0;
}

/**
 * @brief Improved spinlock usage display with better formatting
 */

/**
 * @brief Format task ID for display (handle special values)
 */
static void format_task_id(uint32_t task_id, char* buffer, size_t buffer_size) {
    if (task_id == UINT32_MAX || task_id == 0) {
        snprintf(buffer, buffer_size, "SYS");
    } else {
        snprintf(buffer, buffer_size, "%lu", task_id);
    }
}

/**
 * @brief Find optimal truncation point in a string
 * 
 * Searches backwards from a given position to find a suitable truncation point
 * such as an underscore or hyphen. This helps create more readable truncated strings.
 * 
 * @param[in] str String to search in
 * @param[in] max_pos Maximum position to start searching from
 * @return Optimal truncation position, or max_pos if no good point found
 * 
 * @note Searches backwards from max_pos to max_pos/2 to find delimiter characters
 * @note Returns max_pos if no suitable delimiter is found in the search range
 */
static size_t find_truncation_point(const char* str, size_t max_pos) {
    if (!str || max_pos == 0) {
        return max_pos;
    }
    
    size_t min_search = max_pos / 2;
    
    for (size_t i = max_pos; i > min_search && i > 0; i--) {
        if (str[i] == '_' || str[i] == '-') {
            return i;
        }
    }
    
    return max_pos;
}

/**
 * @brief Create truncated string with ellipsis
 * 
 * Truncates the source string at the optimal position and appends ellipsis ("...").
 * Ensures the total length fits exactly within the specified buffer size.
 * 
 * @param[in] source Source string to truncate
 * @param[out] buffer Destination buffer for truncated string
 * @param[in] max_chars Maximum characters allowed (excluding null terminator)
 * 
 * @note Reserves 3 characters for "..." ellipsis
 * @note Attempts to find optimal truncation point using find_truncation_point()
 * @note Ensures proper null termination
 */
static void create_truncated_with_ellipsis(const char* source, char* buffer, size_t max_chars) {
    if (!source || !buffer || max_chars <= 3) {
        return;
    }
    
    size_t chars_before_ellipsis = max_chars - 3;  // Reserve 3 chars for "..."
    size_t truncate_pos = find_truncation_point(source, chars_before_ellipsis);
    
    // Copy the truncated part
    strncpy(buffer, source, truncate_pos);
    buffer[truncate_pos] = '\0';
    
    // Add ellipsis
    strcat(buffer, "...");
    
    // Ensure we didn't exceed the buffer (defensive programming)
    buffer[max_chars] = '\0';
}

/**
 * @brief Create simple truncated string without ellipsis
 * 
 * Performs basic truncation when the buffer is too small for meaningful
 * ellipsis truncation (less than 4 characters available).
 * 
 * @param[in] source Source string to truncate
 * @param[out] buffer Destination buffer for truncated string
 * @param[in] max_chars Maximum characters to copy (excluding null terminator)
 * 
 * @note Used when buffer is too small for ellipsis (< 4 characters)
 * @note Ensures proper null termination
 */
static void create_simple_truncation(const char* source, char* buffer, size_t max_chars) {
    if (!source || !buffer) {
        return;
    }
    
    strncpy(buffer, source, max_chars);
    buffer[max_chars] = '\0';
}

/**
 * @brief Copy string ensuring proper null termination
 * 
 * Safely copies a string that fits completely within the buffer size,
 * ensuring null termination even if strncpy doesn't add it.
 * 
 * @param[in] source Source string to copy
 * @param[out] buffer Destination buffer
 * @param[in] buffer_size Total size of destination buffer
 * 
 * @note Always ensures null termination regardless of source string length
 */
static void copy_complete_string(const char* source, char* buffer, size_t buffer_size) {
    if (!source || !buffer || buffer_size == 0) {
        return;
    }
    
    strncpy(buffer, source, buffer_size);
    buffer[buffer_size - 1] = '\0';  // Ensure null termination
}

/**
 * @brief Format owner name with intelligent truncation that respects exact column width
 * 
 * Formats an owner name string to fit within a specified buffer size using intelligent
 * truncation strategies. The function attempts to preserve readability by finding
 * natural break points (underscores, hyphens) when truncation is necessary.
 * 
 * Truncation strategy:
 * 1. If name fits completely: copy as-is with null termination
 * 2. If buffer too small for ellipsis (≤3 chars): simple truncation
 * 3. If truncation needed: use ellipsis with optimal break point detection
 * 
 * @param[in] owner_name Source owner name string to format. Can be NULL.
 * @param[out] buffer Destination buffer to store formatted name
 * @param[in] buffer_size Total size of destination buffer including null terminator
 * 
 * @note If owner_name is NULL, buffer is set to "???"
 * @note Buffer is always null-terminated regardless of input
 * @note For truncation, prefers breaking at '_' or '-' characters when possible
 * @note Ellipsis ("...") is added only when buffer_size > 3
 * @note Function ensures exact column width compliance for display formatting
 * 
 * @warning buffer must be valid and buffer_size must be > 0
 * 
 * @example
 * char result[10];
 * format_owner_name("very_long_owner_name", result, sizeof(result));
 * // Result: "very_l..." (if optimal break point not found) // NOSONAR
 * // Result: "very_..." (if underscore break point found)  // NOSONAR
 * 
 * @see find_truncation_point() for break point detection algorithm
 * @see create_truncated_with_ellipsis() for ellipsis truncation implementation
 */
static void format_owner_name(const char* owner_name, char* buffer, size_t buffer_size) {
    // Early return for invalid input
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Handle null owner name
    if (!owner_name) {
        snprintf(buffer, buffer_size, "???");
        return;
    }
    
    size_t len = strlen(owner_name);
    size_t max_chars = buffer_size - 1;  // Leave space for null terminator
    
    // Early return if name fits completely
    if (len <= max_chars) {
        copy_complete_string(owner_name, buffer, buffer_size);
        return;
    }
    
    // Name needs truncation - check if buffer is too small for meaningful truncation
    if (max_chars <= 3) {
        create_simple_truncation(owner_name, buffer, max_chars);
        return;
    }
    
    // Use intelligent truncation with ellipsis
    create_truncated_with_ellipsis(owner_name, buffer, max_chars);
}

/**
 * @brief Format acquisition count with units
 */
static void format_acquisition_count(uint32_t count, char* buffer, size_t buffer_size) {
    if (count < 1000) {
        snprintf(buffer, buffer_size, "%lu", count);
    } else if (count < 1000000) {
        snprintf(buffer, buffer_size, "%.2fK", (float) count / 1000.0f);
    } else {
        snprintf(buffer, buffer_size, "%.2fM", (float) count / 1000000.0f);
    }
}

/**
 * @brief Format time duration with appropriate units
 */
static void format_time_duration(uint64_t time_us, char* buffer, size_t buffer_size) {
    if (time_us == 0) {
        snprintf(buffer, buffer_size, "0");
    } else if (time_us < 1000) {
        snprintf(buffer, buffer_size, "%lluus", time_us);
    } else if (time_us < 1000000) {
        snprintf(buffer, buffer_size, "%.2fms", (float) time_us / 1000.0f);
    } else {
        snprintf(buffer, buffer_size, "%.2fs", (float) time_us / 1000000.0f);
    }
}

/**
 * @brief Print spinlock usage information with improved formatting
 */
void hw_spinlock_print_usage(bool detailed) {
    if (!core_initialized) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Hardware spinlock manager not initialized");
        return;
    }
    
    // Get data under lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Count by category and collect detailed info
    uint32_t counts[SPINLOCK_CAT_COUNT] = {0};
    uint32_t total = 0;
    
    // For detailed view, collect all info first
    typedef struct {
        uint32_t id;
        spinlock_info_t info;
        bool allocated;
    } spinlock_display_info_t;
    
    spinlock_display_info_t display_info[HW_SPINLOCK_COUNT];
    
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        display_info[i].id = i;
        display_info[i].allocated = spinlock_info[i].allocated;
        if (spinlock_info[i].allocated) {
            display_info[i].info = spinlock_info[i];
            counts[spinlock_info[i].category]++;
            total++;
        }
    }
    
    // Release lock before logging
    spin_unlock(manager_lock, save);
    
    // Print summary
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Hardware Spinlock Usage Summary:");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "===============================");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Total allocated: %lu out of %d", total, HW_SPINLOCK_COUNT);
    
    for (int cat = 1; cat < SPINLOCK_CAT_COUNT; cat++) {
        if (counts[cat] > 0) {
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  %s: %lu", 
                      hw_spinlock_category_to_string(cat), counts[cat]);
        }
    }
    
    if (detailed && total > 0) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Detailed Spinlock Information:");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "=============================");
        
        // Method 1: Wide format with proper spacing
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "┌────┬───────────────┬─────────────────┬─────────┬───────────┬─────────────┐");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "│ ID │   Category    │      Owner      │ Task ID │ Acq Count │ Max Hold    │");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "├────┼───────────────┼─────────────────┼─────────┼───────────┼─────────────┤");
        
        for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
            if (display_info[i].allocated) {
                // Format each field with exact column widths
                char task_id_str[8];      // 7 chars + null
                char owner_str[16];       // 15 chars + null
                char acq_count_str[10];   // 9 chars + null
                char max_hold_str[12];    // 11 chars + null
                
                format_task_id(display_info[i].info.owner_task_id, task_id_str, sizeof(task_id_str));
                format_owner_name(display_info[i].info.owner_name, owner_str, sizeof(owner_str));
                format_acquisition_count(display_info[i].info.acquisition_count, acq_count_str, sizeof(acq_count_str));
                format_time_duration(display_info[i].info.max_locked_time_us, max_hold_str, sizeof(max_hold_str));
                
                log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                          "│%3lu │ %-13s │ %-15s │ %-7s │ %-9s │ %-11s │",
                          display_info[i].id,
                          hw_spinlock_category_to_string(display_info[i].info.category),
                          owner_str,
                          task_id_str,
                          acq_count_str,
                          max_hold_str);
            }
        }
        
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "└────┴───────────────┴─────────────────┴─────────┴───────────┴─────────────┘");
    }
}

/**
 * @brief Alternative compact format for limited screen width
 */
void hw_spinlock_print_usage_compact(bool detailed) {
    if (!core_initialized) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Hardware spinlock manager not initialized");
        return;
    }
    
    // Get data under lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Count by category
    uint32_t counts[SPINLOCK_CAT_COUNT] = {0};
    uint32_t total = 0;
    
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        if (spinlock_info[i].allocated) {
            counts[spinlock_info[i].category]++;
            total++;
        }
    }
    
    // Release lock
    spin_unlock(manager_lock, save);
    
    // Print summary
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Spinlock Usage: %lu/%d allocated", total, HW_SPINLOCK_COUNT);
    
    for (int cat = 1; cat < SPINLOCK_CAT_COUNT; cat++) {
        if (counts[cat] > 0) {
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  %s: %lu", 
                      hw_spinlock_category_to_string(cat), counts[cat]);
        }
    }
    
    if (detailed && total > 0) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Spinlock Details:");
        
        // Compact format - one line per spinlock with truncated info
        save = spin_lock_blocking(manager_lock);
        
        for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
            if (spinlock_info[i].allocated) {
                char task_id_str[8];
                char owner_str[12];
                char acq_count_str[8];
                char max_hold_str[10];
                
                format_task_id(spinlock_info[i].owner_task_id, task_id_str, sizeof(task_id_str));
                format_owner_name(spinlock_info[i].owner_name, owner_str, sizeof(owner_str));
                format_acquisition_count(spinlock_info[i].acquisition_count, acq_count_str, sizeof(acq_count_str));
                format_time_duration(spinlock_info[i].max_locked_time_us, max_hold_str, sizeof(max_hold_str));
                
                // Release lock for logging
                spin_unlock(manager_lock, save);
                
                log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                          "%2lu: %-8s %-11s T:%-6s A:%-7s H:%-9s",
                          i,
                          hw_spinlock_category_to_string(spinlock_info[i].category),
                          owner_str,
                          task_id_str,
                          acq_count_str,
                          max_hold_str);
                
                // Reacquire lock
                save = spin_lock_blocking(manager_lock);
            }
        }
        
        spin_unlock(manager_lock, save);
    }
}

/**
 * @brief Alternative format with wider owner column for better readability
 */
void hw_spinlock_print_usage_wide(bool detailed) {
    if (!core_initialized) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Hardware spinlock manager not initialized");
        return;
    }
    
    // Get data under lock
    uint32_t save = spin_lock_blocking(manager_lock);
    
    // Count by category and collect detailed info
    uint32_t counts[SPINLOCK_CAT_COUNT] = {0};
    uint32_t total = 0;
    
    // For detailed view, collect all info first
    typedef struct {
        uint32_t id;
        spinlock_info_t info;
        bool allocated;
    } spinlock_display_info_t;
    
    spinlock_display_info_t display_info[HW_SPINLOCK_COUNT];
    
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        display_info[i].id = i;
        display_info[i].allocated = spinlock_info[i].allocated;
        if (spinlock_info[i].allocated) {
            display_info[i].info = spinlock_info[i];
            counts[spinlock_info[i].category]++;
            total++;
        }
    }
    
    // Release lock before logging
    spin_unlock(manager_lock, save);
    
    // Print summary
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Hardware Spinlock Usage Summary:");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "===============================");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Total allocated: %lu out of %d", total, HW_SPINLOCK_COUNT);
    
    for (int cat = 1; cat < SPINLOCK_CAT_COUNT; cat++) {
        if (counts[cat] > 0) {
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  %s: %lu", 
                      hw_spinlock_category_to_string(cat), counts[cat]);
        }
    }
    
    if (detailed && total > 0) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Detailed Spinlock Information:");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "=============================");
        
        // Wide format with more space for owner names
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "┌────┬───────────────┬───────────────────────┬─────────┬───────────┬─────────────┐");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "│ ID │   Category    │         Owner         │ Task ID │ Acq Count │ Max Hold    │");
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "├────┼───────────────┼───────────────────────┼─────────┼───────────┼─────────────┤");
        
        for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
            if (display_info[i].allocated) {
                // Format each field with wider owner column
                char task_id_str[8];      // 7 chars + null
                char owner_str[22];       // 21 chars + null (wider than before)
                char acq_count_str[10];   // 9 chars + null
                char max_hold_str[12];    // 11 chars + null
                
                format_task_id(display_info[i].info.owner_task_id, task_id_str, sizeof(task_id_str));
                format_owner_name(display_info[i].info.owner_name, owner_str, sizeof(owner_str));
                format_acquisition_count(display_info[i].info.acquisition_count, acq_count_str, sizeof(acq_count_str));
                format_time_duration(display_info[i].info.max_locked_time_us, max_hold_str, sizeof(max_hold_str));
                
                log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                          "│%3lu │ %-13s │ %-21s │ %-7s │ %-9s │ %-11s │",
                          display_info[i].id,
                          hw_spinlock_category_to_string(display_info[i].info.category),
                          owner_str,
                          task_id_str,
                          acq_count_str,
                          max_hold_str);
            }
        }
        
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                  "└────┴───────────────┴───────────────────────┴─────────┴───────────┴─────────────┘");
    }
}

void hw_spinlock_print_usage_multiline(void) {
    if (!core_initialized) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Hardware spinlock manager not initialized");
        return;
    }
    
    uint32_t save = spin_lock_blocking(manager_lock);
    
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Detailed Spinlock Report:");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "========================");
    
    for (uint32_t i = 0; i < HW_SPINLOCK_COUNT; i++) {
        if (spinlock_info[i].allocated) {
            // Copy info while locked
            spinlock_info_t info = spinlock_info[i];
            
            // Release lock for logging
            spin_unlock(manager_lock, save);
            
            char task_id_str[20];
            format_task_id(info.owner_task_id, task_id_str, sizeof(task_id_str));
            
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Spinlock %lu:", i);
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Category:        %s", 
                      hw_spinlock_category_to_string(info.category));
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Owner:           %s", 
                      info.owner_name ? info.owner_name : "Unknown");
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Current Task:    %s", task_id_str);
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Acquisitions:    %lu", info.acquisition_count);
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Total Hold Time: %llu us", info.total_locked_time_us);
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Max Hold Time:   %llu us", info.max_locked_time_us);
            
            if (info.acquisition_count > 0) {
                uint64_t avg_time = info.total_locked_time_us / info.acquisition_count;
                log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Avg Hold Time:   %llu us", avg_time);
            }
            
            // Reacquire lock
            save = spin_lock_blocking(manager_lock);
        }
    }
    
    spin_unlock(manager_lock, save);
}

/**
 * @brief Display hardware spinlock registers with detailed descriptions
 */
/**
 * @brief Corrected function to display hardware spinlock registers
 */
void hw_spinlock_print_hardware_registers(void) {
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "RP2350 Hardware Spinlock Registers:");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "==========================================");
    
    // Read spinlock state register - this shows CURRENTLY CLAIMED locks
    uint32_t spinlock_state = READ_SPINLOCK_STATE();
    
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "SIO_SPINLOCK_STATE (0x%08x): 0x%08x", 
              SIO_SPINLOCK_STATE, spinlock_state);
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Description: Bitmap of currently CLAIMED (held) spinlocks");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  Note: This only shows locks actively held right now");
    
    // Count currently claimed locks
    int currently_claimed = 0;
    for (int i = 0; i < 32; i++) {
        if (spinlock_state & (1u << i)) {
            currently_claimed++;
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", "    Spinlock %d: CURRENTLY CLAIMED", i);
        }
    }
    
    if (currently_claimed == 0) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "    No spinlocks currently claimed (all free)");
    }
    
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Allocation vs. Claim Status Summary:");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "===================================");
    
    // Show allocation vs claim status in a cleaner format
    for (int i = 0; i < HW_SPINLOCK_COUNT; i++) {
        bool hw_claimed = (spinlock_state & (1u << i)) != 0;
        bool sw_allocated = spinlock_info[i].allocated;
        
        if (sw_allocated || hw_claimed) {
            const char* status;
            if (sw_allocated && hw_claimed) {
                status = "ALLOCATED + CLAIMED";
            } else if (sw_allocated && !hw_claimed) {
                status = "ALLOCATED (not currently held)";
            } else if (!sw_allocated && hw_claimed) {
                status = "CLAIMED but not tracked (LEAK!)";
            } else {
                status = "FREE";
            }
            
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                      "  Spinlock %2d: %-30s Owner: %s", 
                      i, status, 
                      spinlock_info[i].owner_name ? spinlock_info[i].owner_name : "Unknown");
        }
    }
    
    // Check for potential leaks
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Potential Issues:");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "================");
    
    bool issues_found = false;
    for (int i = 0; i < HW_SPINLOCK_COUNT; i++) {
        bool hw_claimed = (spinlock_state & (1u << i)) != 0;
        bool sw_allocated = spinlock_info[i].allocated;
        
        if (!sw_allocated && hw_claimed) {
            log_message(LOG_LEVEL_INFO, "SpinlockMgr", 
                      "  LEAK: Spinlock %d claimed in hardware but not tracked in software", i);
            issues_found = true;
        }
    }
    
    if (!issues_found) {
        log_message(LOG_LEVEL_INFO, "SpinlockMgr", "  No issues detected");
    }
    
    // Don't try to read individual spinlock registers during this diagnostic
    // as reading them could affect their state
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "Note: Individual register reads not shown to avoid");
    log_message(LOG_LEVEL_INFO, "SpinlockMgr", "      interfering with active spinlock operations");
}


/**
 * @brief Check if spinlock manager is fully initialized
 */
bool hw_spinlock_manager_is_fully_initialized(void) {
    return core_initialized && logging_initialized;
}

/**
 * @brief Check if spinlock manager core is initialized
 */
bool hw_spinlock_manager_is_core_initialized(void) {
    return core_initialized;
}

void register_spinlock_commands(void) {
    static const shell_command_t spinlock_command = {
        cmd_spinlock,
        "spinlock",
        "Hardware spinlock manager commands"
        
    };
    
    shell_register_command(&spinlock_command);
}