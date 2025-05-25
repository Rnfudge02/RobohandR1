/**
* @file scheduler_tz.c
* @brief TrustZone security implementation for the scheduler
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-17
* 
* This module provides TrustZone-specific configuration for tasks running
* under the scheduler, enabling secure/non-secure transitions and proper
* isolation between security domains. This is separated from MPU functionality
* to allow independent operation.
*/

#include "log_manager.h"
#include "scheduler_tz.h"
#include "scheduler.h"
#include "scheduler_tz.h"
#include "spinlock_manager.h"
#include "usb_shell.h"

#include "pico/platform.h"
#include "pico/time.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Maximum number of secure functions */
#define MAX_SECURE_FUNCTIONS 32

/* Maximum number of tasks with TrustZone configurations */
#define MAX_TZ_TASKS 16

/* Maximum number of SAU regions */
#define MAX_SAU_REGIONS 8


/* SAU Control Register bits */
#define SAU_CTRL_ENABLE   (1 << 0)
#define SAU_CTRL_ALLNS    (1 << 1)

/* SAU Region Attribute bits */
#define SAU_REGION_ENABLE (1 << 0)
#define SAU_REGION_NSC    (1 << 1)  /* Non-Secure Callable */

/* ARMv8-M TrustZone control registers */
#define TZ_SAU_CTRL       0xE000EDD0  /* SAU Control Register */
#define TZ_SAU_TYPE       0xE000EDCC  /* SAU Type Register */
#define TZ_SAU_RNR        0xE000EDD8  /* SAU Region Number Register */
#define TZ_SAU_RBAR       0xE000EDDC  /* SAU Region Base Address Register */
#define TZ_SAU_RLAR       0xE000EDE0  /* SAU Region Limit Address Register */
#define TZ_NSACR          0xE000ED8C  /* Non-Secure Access Control Register */

/* Storage for task TrustZone configurations */
typedef struct {
    uint32_t task_id;
    task_security_state_t security_state;
    secure_function_t secure_functions[MAX_SECURE_FUNCTIONS];
    uint8_t function_count;
    bool configured;
} task_tz_state_t;

static tz_status_info_t global_tz_status = {0};
static uint32_t last_task_id_applied[2] = {0, 0};
static bool last_task_settings_applied[2] = {false, false};
static uint8_t num_configured_tz_tasks = 0;
static uint8_t num_registered_functions = 0;
static tz_perf_stats_t perf_stats = {0};
static secure_function_t secure_function_registry[MAX_SECURE_FUNCTIONS];

static task_tz_state_t task_tz_states[MAX_TZ_TASKS];
static bool tz_enabled = false;
static bool tz_globally_enabled = false;
static bool tz_hardware_supported = false;
static uint32_t tz_spinlock_num;


int cmd_tz(int argc, char *argv[]);

/* Private helper functions */
/**
 * @brief Configure SAU (Security Attribution Unit) regions
 * 
 * @param region_num Region number to configure
 * @param start_addr Start address of region
 * @param end_addr End address of region (inclusive)
 * @param nsc Whether region is Non-Secure Callable
 */

static void configure_sau_region(uint32_t region_num, 
                               void *start_addr, 
                               void *end_addr, 
                               bool nsc) {
    // Select the region
    hw_write_masked(
        (io_rw_32*)(TZ_SAU_RNR),
        region_num,
        0xFF
    );
    
    // Set base address
    hw_write_masked(
        (io_rw_32*)(TZ_SAU_RBAR),
        (uint32_t)start_addr,
        0xFFFFFFFF
    );
    
    // Set limit address and attributes
    uint32_t rlar = (uint32_t)end_addr;
    rlar |= SAU_REGION_ENABLE;
    if (nsc) {
        rlar |= SAU_REGION_NSC;
    }
    
    hw_write_masked(
        (io_rw_32*)(TZ_SAU_RLAR),
        rlar,
        0xFFFFFFFF
    );
}

/**
 * @brief Create a new task TrustZone state
 * 
 * This function creates a new TrustZone state entry for a task.
 * It must be called with the spinlock held.
 */

static task_tz_state_t* create_task_tz_state(uint32_t task_id) {
    if (num_configured_tz_tasks >= MAX_TZ_TASKS) {
        return NULL;
    }
    
    task_tz_state_t* state = &task_tz_states[num_configured_tz_tasks++];
    memset(state, 0, sizeof(task_tz_state_t));
    state->task_id = task_id;
    state->security_state = TASK_SECURITY_SECURE; // Default to secure
    state->function_count = 0;
    state->configured = false;
    
    // Update global stats based on security state
    global_tz_status.secure_tasks++;
    
    return state;
}

/**
 * @brief Get task TrustZone state by task ID
 */

static task_tz_state_t* get_task_tz_state(uint32_t task_id) {
    for (int i = 0; i < num_configured_tz_tasks; i++) {
        if (task_tz_states[i].task_id == task_id) {
            return &task_tz_states[i];
        }
    }
    return NULL;
}

/**
 * @brief Check if we're currently in secure state
 * 
 * @return true if in secure state, false if in non-secure state
 */

static bool is_secure_state(void) {
    // On ARMv8-M, we can check the AIRCR.BFHFNMINS bit
    // For the RP2350, we'll use a hardware-specific approach
    
    // This is a simplified implementation - in a real system,
    // you would use the proper hardware-specific registers
    
    // For now, assume we're always in secure state during initialization
    return true;
}


/* Public API implementation */

bool scheduler_tz_init(void) {
    tz_spinlock_num = hw_spinlock_allocate(SPINLOCK_CAT_SCHEDULER, "scheduler_tz");

    // Initialize TrustZone task state storage
    memset(task_tz_states, 0, sizeof(task_tz_states));
    num_configured_tz_tasks = 0;
    
    // Initialize secure function registry
    memset(secure_function_registry, 0, sizeof(secure_function_registry));
    num_registered_functions = 0;
    
    // Initialize global TZ status
    memset(&global_tz_status, 0, sizeof(tz_status_info_t));
    
    // Initialize performance statistics
    memset(&perf_stats, 0, sizeof(tz_perf_stats_t));
    
    // Check if TrustZone is supported by the hardware
    tz_hardware_supported = scheduler_tz_is_supported();
    global_tz_status.available = tz_hardware_supported;
    
    if (!tz_hardware_supported) {
        log_message(LOG_LEVEL_WARN, "Trustzone Init", "TrustZone not supported on this hardware.");
        return false;
    }
    
    // Read the SAU Type register to determine number of regions
    uint32_t sau_type = *(io_ro_32*)(TZ_SAU_TYPE);
    uint8_t num_regions = sau_type & 0xFF;
    
    // Update global status
    global_tz_status.sau_region_count = num_regions;
    
    // Enable SAU with default settings
    hw_write_masked(
        (io_rw_32*)(TZ_SAU_CTRL),
        SAU_CTRL_ENABLE,
        SAU_CTRL_ENABLE
    );
    
    // Configure default SAU regions
    
    // Region 0: Mark SRAM as secure by default
    configure_sau_region(0, (void*)0x20000000, (void*)0x20040000, false);
    
    // Region 1: Mark Flash as secure by default
    configure_sau_region(1, (void*)0x10000000, (void*)0x10080000, false);
    
    // Region 2: Mark peripheral space as secure by default
    configure_sau_region(2, (void*)0x40000000, (void*)0x50000000, false);
    
    // Region 3: NSC veneer table area (to be populated at runtime)
    // This region will be used for non-secure callable functions
    configure_sau_region(3, (void*)0x10080000, (void*)0x10081000, true);
    
    tz_enabled = true;
    tz_globally_enabled = true;
    global_tz_status.enabled = true;
    
    return true;
    log_message(LOG_LEVEL_INFO, "Trustzone Init", "TrustZone not supported on this hardware.");
}

bool scheduler_tz_is_enabled(void) {
    return tz_hardware_supported && tz_enabled && tz_globally_enabled;
}


bool scheduler_tz_is_supported(void) {
    // Read the SAU Type register to check if TrustZone is supported
    uint32_t sau_type = *(io_ro_32*)(TZ_SAU_TYPE);
    
    // If SAU Type is zero, TrustZone is not supported
    return (sau_type != 0);
}


bool scheduler_tz_apply_task_settings(uint32_t task_id) {
    uint64_t start_time = time_us_64();
    
    // Skip if TrustZone is not supported or enabled
    if (!tz_hardware_supported || !tz_enabled || !tz_globally_enabled) {
        return true;
    }
    
    // Check if we've already applied settings for this task on this core
    uint8_t core = (uint8_t) (get_core_num() & 0xFF);
    if (last_task_settings_applied[core] && last_task_id_applied[core] == task_id) {
        return true;
    }
    
    // Try to find the task state without locking first
    task_tz_state_t* state = get_task_tz_state(task_id);    // NOSONAR - state is reassigned below
    if (!state || !state->configured) {
        return true;  // No configuration - use defaults (remain in secure state)
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    // Find task configuration again with lock held
    state = get_task_tz_state(task_id);
    
    if (!state || !state->configured) {
        hw_spinlock_release(tz_spinlock_num, owner_irq);
        return true;  // No configuration - use defaults
    }
    
    // Apply security state transition if needed
    task_security_state_t current_state = scheduler_tz_get_security_state();
    
    if (state->security_state != current_state) {
        // Need to transition security state
        
        // In a real implementation, this would involve:
        // 1. Saving current context
        // 2. Setting up transition parameters
        // 3. Executing transition instruction (if needed)
        
        // For development purposes, just log the transition
        log_message(LOG_LEVEL_INFO, "Trustzone", "Task %lu transitioning from %s to %s state.",
            task_id,
            current_state == TASK_SECURITY_SECURE ? "secure" : "non-secure",
            state->security_state == TASK_SECURITY_SECURE ? "secure" : "non-secure");
        
        // Update performance stats
        perf_stats.state_transition_count++;
        
        // Update global status
        global_tz_status.current_state = state->security_state;
    }
    
    // Remember that we've applied settings for this task
    last_task_settings_applied[core] = true;
    last_task_id_applied[core] = task_id;
    
    // Update performance statistics
    uint64_t time_taken = time_us_64() - start_time;
    perf_stats.apply_settings_count++;
    perf_stats.total_apply_time_us += time_taken;
    if (time_taken > perf_stats.max_apply_time_us) {
        perf_stats.max_apply_time_us = time_taken;
    }
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_tz_configure_task(const task_tz_config_t *config) {
    if (!config) {
        return false;
    }
    
    // Skip if TrustZone is not supported
    if (!tz_hardware_supported) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    // Check if task already has a configuration
    task_tz_state_t *state = get_task_tz_state(config->task_id);
    
    if (!state) {
        // No existing configuration, create new one
        state = create_task_tz_state(config->task_id);
        if (!state) {
            hw_spinlock_release(tz_spinlock_num, owner_irq);
            return false;
        }
    } else {
        // Update global stats based on security state change
        if (state->security_state == TASK_SECURITY_SECURE) {
            global_tz_status.secure_tasks--;
        } else if (state->security_state == TASK_SECURITY_NON_SECURE) {
            global_tz_status.non_secure_tasks--;
        } else {
            global_tz_status.transitional_tasks--;
        }
    }
    
    // Copy the security state
    state->security_state = config->security_state;
    
    // Update global stats based on new security state
    if (config->security_state == TASK_SECURITY_SECURE) {
        global_tz_status.secure_tasks++;
    } else if (config->security_state == TASK_SECURITY_NON_SECURE) {
        global_tz_status.non_secure_tasks++;
    } else {
        global_tz_status.transitional_tasks++;
    }
    
    // Copy secure function references
    uint8_t function_count = config->function_count;
    if (function_count > MAX_SECURE_FUNCTIONS) {
        function_count = MAX_SECURE_FUNCTIONS;
    }
    
    state->function_count = function_count;
    for (int i = 0; i < function_count; i++) {
        memcpy(&state->secure_functions[i], &config->secure_functions[i], sizeof(secure_function_t));
    }
    
    state->configured = true;
    
    // Reset task settings cache to force reapplication
    for (int i = 0; i < 2; i++) {
        if (last_task_id_applied[i] == config->task_id) {
            last_task_settings_applied[i] = false;
        }
    }
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return true;
}



bool scheduler_tz_reset_task_settings(uint32_t task_id) {
    uint64_t start_time = time_us_64();
    
    // Skip if TrustZone is not supported or enabled
    if (!tz_hardware_supported || !tz_enabled || !tz_globally_enabled) {
        return true;
    }
    
    uint8_t core = (uint8_t) (get_core_num() & 0xFF);
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    // Clear the last applied task flag
    last_task_settings_applied[core] = false;
    
    // If current state is non-secure, transition back to secure
    task_security_state_t current_state = scheduler_tz_get_security_state();
    if (current_state != TASK_SECURITY_SECURE) {
        // In a real implementation, this would involve:
        // 1. Saving non-secure context
        // 2. Setting up transition parameters
        // 3. Executing transition instruction
        
        // For development purposes, just log the transition
        log_message(LOG_LEVEL_INFO, "Trustzone", "Resetting to secure state from task %lu.", task_id);
        
        // Update performance stats
        perf_stats.state_transition_count++;
        
        // Update global status
        global_tz_status.current_state = TASK_SECURITY_SECURE;
    }
    
    // Update performance statistics
    uint64_t time_taken = time_us_64() - start_time;
    perf_stats.reset_settings_count++;
    perf_stats.total_reset_time_us += time_taken;
    if (time_taken > perf_stats.max_reset_time_us) {
        perf_stats.max_reset_time_us = time_taken;
    }
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return true;
}

task_security_state_t scheduler_tz_get_security_state(void) {
    // This is a placeholder implementation
    // In a real implementation, this would check processor state
    return is_secure_state() ? TASK_SECURITY_SECURE : TASK_SECURITY_NON_SECURE;
}

bool scheduler_tz_register_secure_function(const char *name, 
    void *secure_function, void **non_secure_callable) {
    if (!name || !secure_function || !non_secure_callable) {
        return false;
    }
    
    // Skip if TrustZone is not supported
    if (!tz_hardware_supported) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    bool success = false;
    
    // Check if we have space in the registry
    if (num_registered_functions < MAX_SECURE_FUNCTIONS) {
        // Add the function to the registry
        secure_function_t *reg = &secure_function_registry[num_registered_functions++];
        reg->name = name;
        reg->secure_gateway = secure_function;
        
        // Create NSC (Non-Secure Callable) veneer
        // In a real implementation, this would involve setting up a proper
        // veneer function in a non-secure callable memory region
        
        // For this implementation, we'll use a placeholder approach:
        // Calculate an address in the NSC region for this function
        void *veneer_addr = (void*)(0x10080000 + (num_registered_functions - 1) * 32);
        
        // Return the veneer address as the non-secure callable function
        *non_secure_callable = veneer_addr;
        reg->non_secure_callable = veneer_addr;
        
        success = true;
    }
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return success;
}

bool scheduler_tz_get_status(tz_status_info_t *status) {
    if (!status) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    // Copy global status information
    memcpy(status, &global_tz_status, sizeof(tz_status_info_t));
    
    // Update dynamic fields
    status->enabled = tz_enabled && tz_globally_enabled;
    status->current_state = scheduler_tz_get_security_state();
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_tz_get_performance_stats(tz_perf_stats_t *stats) {
    if (!stats) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    // Copy performance statistics
    memcpy(stats, &perf_stats, sizeof(tz_perf_stats_t));
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_tz_set_global_enabled(bool enabled) {
    uint32_t owner_irq = hw_spinlock_acquire(tz_spinlock_num, scheduler_get_current_task());
    
    // Update global enabled flag
    tz_globally_enabled = enabled;
    
    // Update hardware registers if needed
    if (enabled) {
        // Enable SAU
        hw_write_masked(
            (io_rw_32*)(TZ_SAU_CTRL),
            SAU_CTRL_ENABLE,
            SAU_CTRL_ENABLE
        );
    } else {
        // Disable SAU 
        hw_clear_bits((io_rw_32*)(TZ_SAU_CTRL), SAU_CTRL_ENABLE);
        
        // Make sure we're in secure state
        task_security_state_t current_state = scheduler_tz_get_security_state();
        if (current_state != TASK_SECURITY_SECURE) {
            // Transition back to secure state
            // This would involve hardware-specific operations
            
            // Update global status
            global_tz_status.current_state = TASK_SECURITY_SECURE;
        }
    }
    
    // Update global status
    global_tz_status.enabled = enabled;
    
    // Reset task settings cache
    for (int i = 0; i < 2; i++) {
        last_task_settings_applied[i] = false;
    }
    
    // Release spinlock
    hw_spinlock_release(tz_spinlock_num, owner_irq);
    
    return true;
}

// Function to print TrustZone command usage information
static void print_tz_usage(void) {
    printf("Usage: tz <command> [args...]\n");
    printf("Commands:\n");
    printf("  status               - Show TrustZone status\n");
    printf("  enable               - Enable TrustZone globally\n");
    printf("  disable              - Disable TrustZone globally\n");
    printf("  task <task_id> <sec> - Set task security state\n");
    printf("    where <sec> is: secure, non-secure, transitional\n");
    printf("  function <name> <s>  - Register secure function\n");
    printf("  perfstats            - Show performance statistics\n");
    printf("  help                 - Show this help\n");
}

/**
 * @brief Implement the 'tz status' command
 * 
 * Shows TrustZone status information.
 * 
 * @return 0 on success, 1 on failure
 */
static int cmd_tz_status(void) {
    tz_status_info_t status;
    
    if (!scheduler_tz_get_status(&status)) {
        printf("Error: Failed to get TrustZone status\n");
        return 1;
    }

    const char* security_status;
    if (status.current_state == TASK_SECURITY_SECURE) {
        security_status = "Secure";
    } else if (status.current_state == TASK_SECURITY_NON_SECURE) {
        security_status = "Non-Secure";
    } else {
        security_status = "Transitional";
    }
    
    printf("TrustZone Status:\n");
    printf("  Hardware Support: %s\n", status.available ? "Yes" : "No");
    printf("  Enabled: %s\n", status.enabled ? "Yes" : "No");
    printf("  Current State: %s\n", security_status);
    printf("  Secure Tasks: %lu\n", status.secure_tasks);
    printf("  Non-Secure Tasks: %lu\n", status.non_secure_tasks);
    printf("  Transitional Tasks: %lu\n", status.transitional_tasks);
    printf("  SAU Regions: %lu\n", status.sau_region_count);
    
    return 0;
}

/**
 * @brief Implement the 'tz enable' command
 * 
 * Enables TrustZone globally.
 * 
 * @return 0 on success, 1 on failure
 */
static int cmd_tz_enable(void) {
    if (!scheduler_tz_is_supported()) {
        printf("Error: TrustZone is not supported on this hardware\n");
        return 1;
    }
    
    if (!scheduler_tz_set_global_enabled(true)) {
        printf("Error: Failed to enable TrustZone\n");
        return 1;
    }
    
    printf("TrustZone enabled successfully\n");
    return 0;
}

/**
 * @brief Implement the 'tz disable' command
 * 
 * Disables TrustZone globally.
 * 
 * @return 0 on success, 1 on failure
 */
static int cmd_tz_disable(void) {
    if (!scheduler_tz_is_enabled()) {
        printf("TrustZone is already disabled\n");
        return 0;
    }
    
    if (!scheduler_tz_set_global_enabled(false)) {
        printf("Error: Failed to disable TrustZone\n");
        return 1;
    }
    
    printf("TrustZone disabled successfully\n");
    return 0;
}

/**
 * @brief Implement the 'tz task' command
 * 
 * Sets the security state for a task.
 * 
 * @param task_id ID of the task to configure
 * @param state_str String representation of desired security state
 * @return 0 on success, 1 on failure
 */
static int cmd_tz_task(int task_id, const char *state_str) {
    if (task_id <= 0) {
        printf("Error: Invalid task ID\n");
        return 1;
    }
    
    // Parse security state
    task_security_state_t state;
    if (strcmp(state_str, "secure") == 0) {
        state = TASK_SECURITY_SECURE;
    } else if (strcmp(state_str, "non-secure") == 0) {
        state = TASK_SECURITY_NON_SECURE;
    } else if (strcmp(state_str, "transitional") == 0) {
        state = TASK_SECURITY_TRANSITIONAL;
    } else {
        printf("Error: Invalid security state: %s\n", state_str);
        printf("Valid states: secure, non-secure, transitional\n");
        return 1;
    }
    
    // Create TrustZone configuration
    task_tz_config_t config;
    config.task_id = task_id;
    config.security_state = state;
    config.secure_functions = NULL;
    config.function_count = 0;
    
    // Apply configuration
    if (!scheduler_tz_configure_task(&config)) {
        printf("Error: Failed to configure task security state\n");
        return 1;
    }
    
    printf("Set task %d security state to %s\n", task_id, state_str);
    return 0;
}

/**
 * @brief Implement the 'tz function' command
 * 
 * Registers a secure function for non-secure access.
 * 
 * @param name Name of the function
 * @param secure_addr Address of the secure function
 * @return 0 on success, 1 on failure
 */
static int cmd_tz_function(const char *name, const char *secure_addr_str) {
    // Parse function address
    char *endptr;
    void *secure_addr = (void *)strtoul(secure_addr_str, &endptr, 0);
    
    if (*endptr != '\0') {
        printf("Error: Invalid function address: %s\n", secure_addr_str);
        return 1;
    }
    
    // Register function
    void *non_secure_callable = NULL;
    if (!scheduler_tz_register_secure_function(name, secure_addr, &non_secure_callable)) {
        printf("Error: Failed to register secure function\n");
        return 1;
    }
    
    printf("Registered secure function '%s':\n", name);
    printf("  Secure Address: 0x%p\n", secure_addr);
    printf("  Non-Secure Callable: 0x%p\n", non_secure_callable);
    
    return 0;
}

/**
 * @brief Implement the 'tz perfstats' command
 * 
 * Shows TrustZone performance statistics.
 * 
 * @return 0 on success, 1 on failure
 */
static int cmd_tz_perfstats(void) {
    tz_perf_stats_t stats;
    
    if (!scheduler_tz_get_performance_stats(&stats)) {
        printf("Error: Failed to get TrustZone performance statistics\n");
        return 1;
    }
    
    printf("TrustZone Performance Statistics:\n");
    printf("  Apply Settings Count: %lu\n", stats.apply_settings_count);
    printf("  Reset Settings Count: %lu\n", stats.reset_settings_count);
    printf("  State Transitions: %lu\n", stats.state_transition_count);
    
    printf("  Average Apply Time: %llu us\n", 
           stats.apply_settings_count > 0 ? 
           stats.total_apply_time_us / stats.apply_settings_count : 0);
    
    printf("  Average Reset Time: %llu us\n", 
           stats.reset_settings_count > 0 ? 
           stats.total_reset_time_us / stats.reset_settings_count : 0);
    
    printf("  Maximum Apply Time: %llu us\n", stats.max_apply_time_us);
    printf("  Maximum Reset Time: %llu us\n", stats.max_reset_time_us);
    
    return 0;
}

/**
 * @brief Main TrustZone command handler
 * 
 * Dispatches to the appropriate sub-command handler based on arguments.
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, 1 on failure
 */
int cmd_tz(int argc, char *argv[]) {
    // First check if TrustZone is supported
    if (!scheduler_tz_is_supported() && argc > 1 && 
        strcmp(argv[1], "status") != 0 && strcmp(argv[1], "help") != 0) {
        printf("Error: TrustZone is not supported on this hardware\n");
        return 1;
    }
    
    if (argc < 2) {
        print_tz_usage();
        return 1;
    }
    
    if (strcmp(argv[1], "status") == 0) {
        return cmd_tz_status();
    }
    else if (strcmp(argv[1], "enable") == 0) {
        return cmd_tz_enable();
    }
    else if (strcmp(argv[1], "disable") == 0) {
        return cmd_tz_disable();
    }
    else if (strcmp(argv[1], "task") == 0) {
        if (argc < 4) {
            printf("Usage: tz task <task_id> <security_state>\n");
            return 1;
        }
        
        int task_id = atoi(argv[2]);
        return cmd_tz_task(task_id, argv[3]);
    }
    else if (strcmp(argv[1], "function") == 0) {
        if (argc < 4) {
            printf("Usage: tz function <name> <secure_address>\n");
            return 1;
        }
        
        return cmd_tz_function(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "perfstats") == 0) {
        return cmd_tz_perfstats();
    }
    else if (strcmp(argv[1], "help") == 0) {
        print_tz_usage();
        return 0;
    }
    else {
        printf("Unknown TrustZone command: %s\n", argv[1]);
        print_tz_usage();
        return 1;
    }
}

/**
 * @brief Register TrustZone commands with the shell
 * 
 * This function registers the TrustZone command with the shell system.
 * It should be called during system initialization.
 */
void register_tz_commands(void) {
    static const shell_command_t tz_cmd = {
        cmd_tz, "tz", "TrustZone security extension commands"
    };
    
    shell_register_command(&tz_cmd);
}