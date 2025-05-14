/**
* @file scheduler_tz.c
* @brief TrustZone security configuration for the scheduler
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-13
*/

#include "scheduler_tz.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "pico/platform.h"
#include <string.h>

/* ARMv8-M TrustZone control registers */
#define TZ_SAU_CTRL       0xE000EDD0  /* SAU Control Register */
#define TZ_SAU_RNR        0xE000EDD8  /* SAU Region Number Register */
#define TZ_SAU_RBAR       0xE000EDDC  /* SAU Region Base Address Register */
#define TZ_SAU_RLAR       0xE000EDE0  /* SAU Region Limit Address Register */
#define TZ_NSACR          0xE000ED8C  /* Non-Secure Access Control Register */

/* SAU Control Register bits */
#define SAU_CTRL_ENABLE   (1 << 0)
#define SAU_CTRL_ALLNS    (1 << 1)

/* SAU Region Attribute bits */
#define SAU_REGION_ENABLE (1 << 0)
#define SAU_REGION_NSC    (1 << 1)  /* Non-Secure Callable */

/* Maximum number of secure functions */
#define MAX_SECURE_FUNCTIONS 32

/* Maximum number of tasks with TrustZone configurations */
#define MAX_TZ_TASKS 16

/* Spin lock for TrustZone configuration synchronization between cores */
static spin_lock_t* tz_spinlock;
static uint32_t tz_spinlock_num;

/* Flag indicating whether TrustZone is enabled */
static bool tz_enabled = false;

/* Storage for task TrustZone configurations */
typedef struct {
    uint32_t task_id;
    task_security_state_t security_state;
    secure_function_t secure_functions[MAX_SECURE_FUNCTIONS];
    uint8_t function_count;
    bool configured;
} task_tz_state_t;

static task_tz_state_t task_tz_states[MAX_TZ_TASKS];
static uint8_t num_configured_tz_tasks = 0;

/* Registry of secure functions */
static secure_function_t secure_function_registry[MAX_SECURE_FUNCTIONS];
static uint8_t num_registered_functions = 0;

/* Private helper functions */

/**
 * @brief Get task TrustZone state by task ID
 */
__attribute__((section(".time_critical")))
static task_tz_state_t* get_task_tz_state(uint32_t task_id) {
    for (int i = 0; i < num_configured_tz_tasks; i++) {
        if (task_tz_states[i].task_id == task_id) {
            return &task_tz_states[i];
        }
    }
    return NULL;
}

/**
 * @brief Check if we're currently running in secure state
 * 
 * @return true if in secure state, false if in non-secure state
 */
__attribute__((section(".time_critical")))
static bool is_secure_state(void) {
    // On ARMv8-M, we can check the AIRCR.BFHFNMINS bit
    // For the RP2350, we'll use a hardware-specific approach
    
    // This is a simplified implementation - in a real system,
    // you would use the proper hardware-specific registers
    
    // For the purposes of this implementation, assume we're 
    // always in secure state during initialization
    return true;
}

/**
 * @brief Configure SAU (Security Attribution Unit) regions
 * 
 * @param region_num Region number to configure
 * @param start_addr Start address of region
 * @param end_addr End address of region (inclusive)
 * @param nsc Whether region is Non-Secure Callable
 */
__attribute__((section(".time_critical")))
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

/* Public API implementation */
__attribute__((section(".time_critical")))
bool scheduler_tz_init(void) {
    // Initialize the spinlock for TrustZone configuration
    tz_spinlock_num = next_striped_spin_lock_num();
    tz_spinlock = spin_lock_init(tz_spinlock_num);
    
    // Initialize TrustZone task state storage
    memset(task_tz_states, 0, sizeof(task_tz_states));
    num_configured_tz_tasks = 0;
    
    // Initialize secure function registry
    memset(secure_function_registry, 0, sizeof(secure_function_registry));
    num_registered_functions = 0;
    
    // Check if TrustZone extensions are present
    // This is a simplified check for RP2350
    uint32_t sau_type = *(io_ro_32*)(TZ_SAU_CTRL - 0x4);  // SAU_TYPE register
    if ((sau_type & 0xFF) == 0) {
        // No SAU regions available, TrustZone not supported
        tz_enabled = false;
        return false;
    }
    
    // Enable SAU
    hw_write_masked(
        (io_rw_32*)(TZ_SAU_CTRL),
        SAU_CTRL_ENABLE,
        SAU_CTRL_ENABLE
    );
    
    // Configure default SAU regions
    // Region 0: Mark SRAM as secure
    configure_sau_region(0, (void*)0x20000000, (void*)0x20040000, false);
    
    // Region 1: Mark Flash as secure
    configure_sau_region(1, (void*)0x10000000, (void*)0x10080000, false);
    
    // Region 2: Mark peripheral space as secure
    configure_sau_region(2, (void*)0x40000000, (void*)0x50000000, false);
    
    // Region 3: NSC veneer table area (to be populated at runtime)
    configure_sau_region(3, (void*)0x10080000, (void*)0x10081000, true);
    
    tz_enabled = true;
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_tz_configure_task(const task_tz_config_t *config) {
    if (!tz_enabled || !config) {
        return false;
    }
    

    // Acquire spinlock to prevent race conditions between cores
    uint32_t owner_irq = spin_lock_blocking(tz_spinlock);
    
    // Check if task already has a configuration
    task_tz_state_t *state = get_task_tz_state(config->task_id);
    
    if (!state) {
        // No existing configuration, create new one if space available
        if (num_configured_tz_tasks >= MAX_TZ_TASKS) {
            spin_unlock(tz_spinlock, owner_irq);
            return false;
        }
        
        state = &task_tz_states[num_configured_tz_tasks++];
        state->task_id = config->task_id;
        state->function_count = 0;
        state->configured = false;
    }
    
    // Copy the configuration
    state->security_state = config->security_state;
    
    // Copy secure function references (up to the maximum allowed)
    uint8_t function_count = config->function_count;
    if (function_count > MAX_SECURE_FUNCTIONS) {
        function_count = MAX_SECURE_FUNCTIONS;
    }
    
    state->function_count = function_count;
    for (int i = 0; i < function_count; i++) {
        memcpy(&state->secure_functions[i], &config->secure_functions[i], sizeof(secure_function_t));
    }
    
    state->configured = true;
    
    // Release spinlock
    spin_unlock(tz_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_tz_apply_task_settings(uint32_t task_id) {
    if (!tz_enabled) {
        return false;
    }
    
    task_tz_state_t *state;
    uint32_t owner_irq = spin_lock_blocking(tz_spinlock);
    
    // Find task configuration
    state = get_task_tz_state(task_id);
    
    if (!state || !state->configured) {
        // No configuration found, default to secure state
        spin_unlock(tz_spinlock, owner_irq);
        return true; // Not an error, just use defaults
    }
    
    // Apply security state transition if needed
    if (state->security_state == TASK_SECURITY_NON_SECURE) {
        // Set up transition to non-secure state
        // In a real implementation, this would involve setting up the
        // appropriate processor state and executing secure-to-non-secure transition
        
        // For RP2350, this would typically involve:
        // 1. Saving secure context
        // 2. Setting up NSACR (Non-Secure Access Control Register)
        // 3. Executing proper transition instruction
        
        // This is a placeholder - actual implementation would be hardware-specific
    }
    
    // Release spinlock
    spin_unlock(tz_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_tz_reset_task_settings(uint32_t task_id) {
    if (!tz_enabled) {
        return false;
    }
    
    uint32_t owner_irq = spin_lock_blocking(tz_spinlock);
    
    // Find task configuration
    task_tz_state_t *state = get_task_tz_state(task_id);
    
    if (state && state->configured) {
        // If task was in non-secure state, ensure we're back in secure state
        if (state->security_state == TASK_SECURITY_NON_SECURE) {
            // Return to secure state
            // In a real implementation, this would involve executing
            // a non-secure-to-secure transition sequence
            
            // This is a placeholder - actual implementation would be hardware-specific
        }
    }
    
    // Release spinlock
    spin_unlock(tz_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_tz_is_enabled(void) {
    return tz_enabled;
}

__attribute__((section(".time_critical")))
task_security_state_t scheduler_tz_get_security_state(void) {
    return is_secure_state() ? TASK_SECURITY_SECURE : TASK_SECURITY_NON_SECURE;
}

__attribute__((section(".time_critical")))
bool scheduler_tz_register_secure_function(const char *name, 
                                          void *secure_function,
                                          void **non_secure_callable) {
    if (!tz_enabled || !name || !secure_function || !non_secure_callable) {
        return false;
    }
    
    uint32_t owner_irq = spin_lock_blocking(tz_spinlock);
    
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
        // - Allocate a small block in the NSC region
        // - Create a veneer function that transitions to secure state,
        //   calls the secure function, and returns to non-secure state
        
        // This is just a placeholder - actual implementation would be hardware-specific
        void *veneer_addr = (void*)(0x10080000 + (num_registered_functions - 1) * 32);
        
        // Return the veneer address as the non-secure callable function
        *non_secure_callable = veneer_addr;
        reg->non_secure_callable = veneer_addr;
        
        success = true;
    }
    
    // Release spinlock
    spin_unlock(tz_spinlock, owner_irq);
    
    return success;
}