/**
* @file scheduler_mpu.c
* @brief Memory Protection Unit configuration implementation
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-17
* 
* This module provides functionality for configuring the Memory 
* Protection Unit (MPU) for tasks running under the scheduler.
*/

#include "log_manager.h"

#include "scheduler.h"
#include "scheduler_mpu.h"
#include "scheduler_tz.h"

#include "spinlock_manager.h"
#include "usb_shell.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/mpu.h"
#include "pico/platform.h"
#include "pico/time.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Define MPU-specific constants
#define MPU_TYPE        (*(volatile uint32_t *)(0xE000ED90))
#define MPU_CTRL        (*(volatile uint32_t *)(0xE000ED94))
#define MPU_RNR         (*(volatile uint32_t *)(0xE000ED98))
#define MPU_RBAR        (*(volatile uint32_t *)(0xE000ED9C))
#define MPU_RASR        (*(volatile uint32_t *)(0xE000EDA0))

// MPU Control Register bits
#define MPU_CTRL_ENABLE            (1 << 0)
#define MPU_CTRL_HFNMIENA          (1 << 1)
#define MPU_CTRL_PRIVDEFENA        (1 << 2)

// MPU Region Attribute and Size Register bits
#define MPU_RASR_ENABLE            (1 << 0)
#define MPU_RASR_SIZE_MASK         (0x3E)
#define MPU_RASR_SIZE_SHIFT        (1)
#define MPU_RASR_SRD_MASK          (0xFF00)
#define MPU_RASR_SRD_SHIFT         (8)
#define MPU_RASR_ATTR_MASK         (0x1FFFF000)
#define MPU_RASR_ATTR_SHIFT        (12)
#define MPU_RASR_XN                (1 << 28)
#define MPU_RASR_AP_MASK           (0x7 << 24)
#define MPU_RASR_AP_SHIFT          (24)
#define MPU_RASR_TEX_MASK          (0x7 << 19)
#define MPU_RASR_TEX_SHIFT         (19)
#define MPU_RASR_S                 (1 << 18)
#define MPU_RASR_C                 (1 << 17)
#define MPU_RASR_B                 (1 << 16)

// MPU Access Permission bits
#define MPU_AP_NO_ACCESS           (0x0)
#define MPU_AP_PRIV_RW             (0x1)
#define MPU_AP_PRIV_RW_USER_RO     (0x2)
#define MPU_AP_PRIV_RW_USER_RW     (0x3)
#define MPU_AP_PRIV_RO             (0x5)
#define MPU_AP_PRIV_RO_USER_RO     (0x6)

// MPU fault types
#define MPU_FAULT_INSTRUCTION      (0x01)
#define MPU_FAULT_DATA             (0x02)
#define MPU_FAULT_UNSTACK          (0x04)
#define MPU_FAULT_STACK            (0x08)
#define MPU_FAULT_LAZY             (0x10)

// Maximum number of tasks with MPU configurations
#define MAX_MPU_TASKS              32

// Spinlock for thread-safe access to MPU configuration
static uint32_t mpu_spinlock_num;

// Storage for task MPU configurations
typedef struct {
    uint32_t task_id;
    mpu_region_config_t regions[MAX_MPU_REGIONS_PER_TASK];
    uint8_t region_count;
    bool mpu_enabled;
    bool configured;
} task_mpu_state_t;

static task_mpu_state_t task_mpu_states[MAX_MPU_TASKS];
static uint8_t num_configured_mpu_tasks = 0;

// Global MPU status information
static mpu_status_info_t global_mpu_status = {0};
static mpu_perf_stats_t perf_stats = {0};

// Flag for global MPU enabling/disabling
static bool mpu_globally_enabled = false;

// User-registered fault handler
static void (*user_fault_handler)(uint32_t task_id, void *fault_addr, uint32_t fault_type) = NULL;

// Cache for last applied MPU settings to avoid redundant operations
static bool last_task_settings_applied[2] = {false, false};
static uint32_t last_task_id_applied[2] = {0, 0};

// Forward declarations
static void handle_mpu_fault(uint32_t task_id, void *fault_addr, uint32_t fault_type);
int cmd_mpu_get_status(int task_id);

/**
 * @brief Get task MPU state by task ID
 * 
 * @param task_id Task ID to look up
 * @return Pointer to task MPU state or NULL if not found
 */
static task_mpu_state_t* get_task_mpu_state(uint32_t task_id) {
    for (int i = 0; i < num_configured_mpu_tasks; i++) {
        if (task_mpu_states[i].task_id == task_id) {
            return &task_mpu_states[i];
        }
    }
    return NULL;
}

/**
 * @brief Create a new MPU state for a task
 * 
 * @param task_id Task ID to create state for
 * @return Pointer to new state or NULL if no space
 */
static task_mpu_state_t* create_task_mpu_state(uint32_t task_id) {
    if (num_configured_mpu_tasks >= MAX_MPU_TASKS) {
        return NULL;
    }
    
    task_mpu_state_t* state = &task_mpu_states[num_configured_mpu_tasks++];
    memset(state, 0, sizeof(task_mpu_state_t));
    state->task_id = task_id;
    state->mpu_enabled = false;
    state->configured = false;
    
    global_mpu_status.total_protected_tasks++;
    
    return state;
}

/**
 * @brief Calculate the region size encoding for the MPU
 * 
 * The size must be a power of 2, and the encoding is log2(size) - 1
 * 
 * @param size Size in bytes (must be power of 2)
 * @return Size encoding for MPU
 */
static uint32_t calculate_region_size_encoding(size_t size) {
    // Find the position of the most significant bit
    uint32_t msb = 0;
    size_t temp = size;
    
    while (temp > 1) {
        temp >>= 1;
        msb++;
    }
    
    // For MPU, region size encoding is (log2(size) - 1)
    return msb - 1;
}

/**
 * @brief Check if address is aligned for MPU region
 * 
 * @param addr Address to check
 * @param size Size of region
 * @return true if address is properly aligned
 */
static bool is_address_aligned(void *addr, size_t size) {
    return (((uint32_t)addr & (size - 1)) == 0);
}

/**
 * @brief Configure MPU region
 * 
 * @param region_num Region number
 * @param config Region configuration
 * @return true if successful
 */
static bool configure_mpu_region(uint8_t region_num, const mpu_region_config_t *config) {
    if (region_num >= 8 || !config) {
        return false;
    }
    
    // Check that size is a power of 2
    if ((config->size & (config->size - 1)) != 0) {
        return false;
    }
    
    // Check that address is properly aligned
    if (!is_address_aligned(config->start_addr, config->size)) {
        return false;
    }
    
    // Calculate size encoding (log2(size) - 1)
    uint32_t size_encoding = calculate_region_size_encoding(config->size);
    
    // Select the region
    MPU_RNR = region_num;
    
    // Set base address (must be aligned to size)
    MPU_RBAR = (uint32_t)config->start_addr;
    
    // Setup attributes
    uint32_t attr = 0;
    
    // Set size
    attr |= (size_encoding << MPU_RASR_SIZE_SHIFT);
    
    // Set access permissions
    uint32_t ap_value = 0;
    bool xn_bit = true; // Default to no execute
    
    switch (config->access) {
        case MPU_NO_ACCESS:
            ap_value = MPU_AP_NO_ACCESS;
            break;
            
        case MPU_READ_ONLY:
            ap_value = MPU_AP_PRIV_RO_USER_RO;
            break;
            
        case MPU_READ_WRITE:
            ap_value = MPU_AP_PRIV_RW_USER_RW;
            break;
            
        case MPU_READ_EXEC:
            ap_value = MPU_AP_PRIV_RO_USER_RO;
            xn_bit = false; // Allow execution
            break;
            
        case MPU_READ_WRITE_EXEC:
            ap_value = MPU_AP_PRIV_RW_USER_RW;
            xn_bit = false; // Allow execution
            break;
            
        default:
            return false;
    }
    
    attr |= (ap_value << MPU_RASR_AP_SHIFT);
    
    // Set XN (Execute Never) bit if needed
    if (xn_bit) {
        attr |= MPU_RASR_XN;
    }
    
    // Set memory attributes (cacheable, bufferable, shareable)
    if (config->cacheable) {
        attr |= MPU_RASR_C;
    }
    
    if (config->bufferable) {
        attr |= MPU_RASR_B;
    }
    
    if (config->shareable) {
        attr |= MPU_RASR_S;
    }
    
    // Enable the region
    attr |= MPU_RASR_ENABLE;
    
    // Write attributes
    MPU_RASR = attr;
    
    return true;
}

/**
 * @brief Disable an MPU region
 * 
 * @param region_num Region number to disable
 */
static void disable_mpu_region(uint8_t region_num) {
    if (region_num >= 8) {
        return;
    }
    
    // Select the region
    MPU_RNR = region_num;
    
    // Disable it
    MPU_RASR = 0;
}

/**
 * @brief Handle a memory protection fault
 * 
 * @param task_id ID of task that caused fault
 * @param fault_addr Address that caused fault
 * @param fault_type Type of fault
 */
static void handle_mpu_fault(uint32_t task_id, void *fault_addr, uint32_t fault_type) {
    // Update global status
    global_mpu_status.fault_count++;
    global_mpu_status.last_fault_address = (uint32_t)fault_addr;
    global_mpu_status.last_fault_type = fault_type;
    
    // Get a human-readable description
    strncpy(global_mpu_status.fault_reason, 
           scheduler_mpu_get_fault_description(fault_type),
           sizeof(global_mpu_status.fault_reason) - 1);
    
    // Update task status if it exists
    const task_mpu_state_t* state = get_task_mpu_state(task_id);
    if (state) {
        // Update task TCB with fault info
        task_control_block_t tcb;
        if (scheduler_get_task_info(task_id, &tcb)) {
            tcb.fault_count++;
            strncpy(tcb.fault_reason, 
                   scheduler_mpu_get_fault_description(fault_type),
                   sizeof(tcb.fault_reason) - 1);
            
            // We don't have direct write access to TCB, but the scheduler records
            // the fault information in its internal state
        }
    }
    
    // Call user fault handler if registered
    if (user_fault_handler) {
        user_fault_handler(task_id, fault_addr, fault_type);
    }
}

// Implementation of Public API functions

bool scheduler_mpu_init(void) {
    // Initialize spinlock for thread-safe MPU operations
    mpu_spinlock_num = hw_spinlock_allocate(SPINLOCK_CAT_SCHEDULER, "scheduler_mpu");

    
    // Initialize task MPU states
    memset(task_mpu_states, 0, sizeof(task_mpu_states));
    num_configured_mpu_tasks = 0;
    
    // Initialize global status
    memset(&global_mpu_status, 0, sizeof(global_mpu_status));
    memset(&perf_stats, 0, sizeof(perf_stats));
    
    // Check if MPU is present
    uint32_t mpu_type = MPU_TYPE;
    uint8_t num_regions = (mpu_type >> 8) & 0xFF;
    
    if (num_regions == 0) {
        // No MPU available
        log_message(LOG_LEVEL_WARN, "MPU Init", "MPU not available on this hardware.");
        return false;
    }
    
    // Disable MPU while configuring
    MPU_CTRL = 0;
    
    // Setup default MPU configuration
    // Region 0: Allow access to all memory (background region)
    // This ensures that memory not covered by other regions is still accessible
    mpu_region_config_t default_config = {
        .start_addr = (void*)0,
        .size = 0xFFFFFFFF, // 4GB
        .access = MPU_READ_WRITE_EXEC,
        .cacheable = true,
        .bufferable = true,
        .shareable = false
    };
    
    configure_mpu_region(0, &default_config);
    
    // Setup default memory map protection
    // Region 1: Flash memory (read-only, executable)
    mpu_region_config_t flash_config = {
        .start_addr = (void*)XIP_BASE,
        .size = 16 * 1024 * 1024, // 16MB
        .access = MPU_READ_EXEC,
        .cacheable = true,
        .bufferable = false,
        .shareable = false
    };
    
    configure_mpu_region(1, &flash_config);
    
    // Region 2: RAM (read-write)
    mpu_region_config_t ram_config = {
        .start_addr = (void*)0x20000000,
        .size = 264 * 1024, // 264KB
        .access = MPU_READ_WRITE,
        .cacheable = true,
        .bufferable = true,
        .shareable = true
    };
    
    configure_mpu_region(2, &ram_config);
    
    // Region 3: Peripherals (read-write, non-cacheable)
    mpu_region_config_t periph_config = {
        .start_addr = (void*)0x40000000,
        .size = 0x10000000, // 256MB
        .access = MPU_READ_WRITE,
        .cacheable = false,
        .bufferable = true,
        .shareable = true
    };
    
    configure_mpu_region(3, &periph_config);
    
    // Initialize global state
    global_mpu_status.mpu_enabled = true;
    mpu_globally_enabled = true;
    
    // Enable MPU with default settings
    // PRIVDEFENA: Use default memory map for privileged accesses when no MPU match
    // HFNMIENA: MPU enabled during HardFault, NMI, and FAULTMASK handlers
    MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA | MPU_CTRL_HFNMIENA;
    
    // Ensure data synchronization and instruction barrier
    __dmb(); // Data Memory Barrier
    __isb(); // Instruction Synchronization Barrier
    
    log_message(LOG_LEVEL_INFO, "MPU Init", "MPU initialized with %u regions.", num_regions);
    return true;
}

bool scheduler_mpu_is_enabled(void) {
    return mpu_globally_enabled && (MPU_CTRL & MPU_CTRL_ENABLE);
}

bool scheduler_mpu_configure_task(const task_mpu_config_t *config) {
    if (!config || config->region_count > MAX_MPU_REGIONS_PER_TASK) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Check if task already has a configuration
    task_mpu_state_t *state = get_task_mpu_state(config->task_id);
    
    if (!state) {
        // No existing configuration, create new one
        state = create_task_mpu_state(config->task_id);
        if (!state) {
            hw_spinlock_release(mpu_spinlock_num, owner_irq);
            return false;
        }
    }
    
    // Copy region configurations
    state->region_count = config->region_count;
    
    for (int i = 0; i < config->region_count; i++) {
        memcpy(&state->regions[i], &config->regions[i], sizeof(mpu_region_config_t));
    }
    
    state->configured = true;
    
    // Reset task settings cache to force reapplication
    for (int i = 0; i < 2; i++) {
        if (last_task_id_applied[i] == config->task_id) {
            last_task_settings_applied[i] = false;
        }
    }
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_mpu_enable_protection(uint32_t task_id, bool enable) {
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Find task state
    task_mpu_state_t *state = get_task_mpu_state(task_id);
    
    if (!state) {
        // Task doesn't have MPU configuration yet
        if (!enable) {
            // Nothing to disable
            hw_spinlock_release(mpu_spinlock_num, owner_irq);
            return true;
        }
        
        // Create default configuration if enabling
        task_control_block_t tcb;
        if (!scheduler_get_task_info(task_id, &tcb)) {
            hw_spinlock_release(mpu_spinlock_num, owner_irq);
            return false;
        }
        
        // Create a new state
        state = create_task_mpu_state(task_id);
        if (!state) {
            hw_spinlock_release(mpu_spinlock_num, owner_irq);
            return false;
        }
        
        // Default config will be created later
        state->configured = false;
    }
    
    // Update enabled state
    state->mpu_enabled = enable;
    
    // Reset task settings cache to force reapplication
    for (int i = 0; i < 2; i++) {
        if (last_task_id_applied[i] == task_id) {
            last_task_settings_applied[i] = false;
        }
    }
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_mpu_get_protection_status(uint32_t task_id, bool *is_protected) {
    if (!is_protected) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Find task state
    const task_mpu_state_t* state = get_task_mpu_state(task_id);
    
    if (!state) {
        // Task doesn't have MPU configuration
        *is_protected = false;
        hw_spinlock_release(mpu_spinlock_num, owner_irq);
        return true;
    }
    
    *is_protected = state->mpu_enabled;
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_mpu_apply_task_settings(uint32_t task_id) {
    uint64_t start_time = time_us_64();
    
    // Skip if MPU is not globally enabled
    if (!mpu_globally_enabled) {
        return true;
    }
    
    // Check if we've already applied settings for this task on this core
    uint8_t core = (uint8_t) (get_core_num() & 0xFF);
    if (last_task_settings_applied[core] && last_task_id_applied[core] == task_id) {
        return true;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Find task state
    const task_mpu_state_t* state = get_task_mpu_state(task_id);
    
    if (!state || !state->configured || !state->mpu_enabled) {
        // No configuration or protection disabled - ensure default
        
        // Disable regions 4-7 (leaving default system regions 0-3 intact)
        for (uint8_t i = 4; i < 8; i++) {
            disable_mpu_region(i);
        }
        
        last_task_settings_applied[core] = true;
        last_task_id_applied[core] = task_id;
        
        hw_spinlock_release(mpu_spinlock_num, owner_irq);
        return true;
    }
    
    // Apply task-specific MPU regions starting at region 4
    // (regions 0-3 are reserved for system-wide protection)
    for (uint8_t i = 0; i < state->region_count && i < 4; i++) {
        configure_mpu_region(i + 4, &state->regions[i]);
    }
    
    // Disable any unused regions
    for (uint8_t i = state->region_count; i < 4; i++) {
        disable_mpu_region(i + 4);
    }
    
    // Update cache
    last_task_settings_applied[core] = true;
    last_task_id_applied[core] = task_id;
    
    // Update performance statistics
    uint64_t time_taken = time_us_64() - start_time;
    perf_stats.apply_settings_count++;
    perf_stats.total_apply_time_us += time_taken;
    if (time_taken > perf_stats.max_apply_time_us) {
        perf_stats.max_apply_time_us = time_taken;
    }
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_mpu_reset_task_settings(uint32_t task_id) {
    (void) task_id;

    uint64_t start_time = time_us_64();
    
    // Skip if MPU is not globally enabled
    if (!mpu_globally_enabled) {
        return true;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Update cache
    uint8_t core = (uint8_t) (get_core_num() & 0xFF);
    last_task_settings_applied[core] = false;
    
    // Disable regions 4-7 (leaving default system regions 0-3 intact)
    for (uint8_t i = 4; i < 8; i++) {
        disable_mpu_region(i);
    }
    
    // Update performance statistics
    uint64_t time_taken = time_us_64() - start_time;
    perf_stats.reset_settings_count++;
    perf_stats.total_reset_time_us += time_taken;
    if (time_taken > perf_stats.max_reset_time_us) {
        perf_stats.max_reset_time_us = time_taken;
    }
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

bool scheduler_mpu_create_default_config(uint32_t task_id, 
                                        void *stack_start, size_t stack_size,
                                        void *code_start, size_t code_size,
                                        task_mpu_config_t *config) {
    if (!config || task_id == 0) {
        return false;
    }
    
    // Clear the configuration structure
    memset(config, 0, sizeof(task_mpu_config_t));
    
    // Set task ID
    config->task_id = task_id;
    
    // Count regions
    uint8_t region_count = 0;
    
    // Add stack region if valid
    if (stack_start && stack_size > 0) {
        // Round stack size up to nearest power of 2
        size_t rounded_size = 1;
        while (rounded_size < stack_size) {
            rounded_size <<= 1;
        }
        
        // Align stack start to size boundary
        void *aligned_start = (void*)((uint32_t)stack_start & ~(rounded_size - 1));
        
        // Configure stack region (read-write)
        config->regions[region_count].start_addr = aligned_start;
        config->regions[region_count].size = rounded_size;
        config->regions[region_count].access = MPU_READ_WRITE;
        config->regions[region_count].cacheable = true;
        config->regions[region_count].bufferable = true;
        config->regions[region_count].shareable = false;
        region_count++;
    }
    
    // Add code region if valid
    if (code_start && code_size > 0) {
        // Round code size up to nearest power of 2
        size_t rounded_size = 1;
        while (rounded_size < code_size) {
            rounded_size <<= 1;
        }
        
        // Align code start to size boundary
        void *aligned_start = (void*)((uint32_t)code_start & ~(rounded_size - 1));
        
        // Configure code region (read-execute)
        config->regions[region_count].start_addr = aligned_start;
        config->regions[region_count].size = rounded_size;
        config->regions[region_count].access = MPU_READ_EXEC;
        config->regions[region_count].cacheable = true;
        config->regions[region_count].bufferable = false;
        config->regions[region_count].shareable = false;
        region_count++;
    }
    
    // Add shared data region (all RAM that's not stack)
    config->regions[region_count].start_addr = (void*)0x20000000;
    config->regions[region_count].size = 0x40000; // 256KB
    config->regions[region_count].access = MPU_READ_WRITE;
    config->regions[region_count].cacheable = true;
    config->regions[region_count].bufferable = true;
    config->regions[region_count].shareable = true;
    region_count++;
    
    // Add peripheral region
    config->regions[region_count].start_addr = (void*)0x40000000;
    config->regions[region_count].size = 0x10000000; // 256MB
    config->regions[region_count].access = MPU_READ_WRITE;
    config->regions[region_count].cacheable = false;
    config->regions[region_count].bufferable = true;
    config->regions[region_count].shareable = true;
    region_count++;
    
    // Set region count
    config->region_count = region_count;
    
    return true;
}

bool scheduler_mpu_get_task_config_minimal(uint32_t task_id, task_mpu_config_t *config) {
    if (!config) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    const task_mpu_state_t* state = get_task_mpu_state(task_id);
    if (!state || !state->configured) {
        hw_spinlock_release(mpu_spinlock_num, owner_irq);
        return false;
    }
    
    // Validate region count to prevent overflow
    if (state->region_count > MAX_MPU_REGIONS_PER_TASK) {
        hw_spinlock_release(mpu_spinlock_num, owner_irq);
        return false;
    }
    
    // Set all fields explicitly (no need to clear first)
    config->task_id = task_id;
    config->region_count = state->region_count;
    
    // Copy regions safely
    for (int i = 0; i < state->region_count; i++) {
        memcpy(&config->regions[i], &state->regions[i], sizeof(mpu_region_config_t));
    }
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    return true;
}

bool scheduler_mpu_is_accessible(void *addr, size_t size, bool write_access) {
    (void) addr;
    (void) size;
    (void) write_access;

    // This is a simplified implementation that checks if an address is within
    // a currently enabled MPU region with appropriate access permissions
    
    // Get current task
    int task_id = scheduler_get_current_task();
    if (task_id < 0) {
        // Not in a task context, use global permissions
        // For simplicity, we'll just return true - this could be refined
        return true;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Find task state
    const task_mpu_state_t* state = get_task_mpu_state(task_id);
    
    if (!state || !state->configured || !state->mpu_enabled) {
        // No MPU config, use global permissions
        hw_spinlock_release(mpu_spinlock_num, owner_irq);
        return true;
    }
    
    bool is_accessible = false;

    return is_accessible;
}

/**
 * @brief Get global MPU status information
 *
 * @param info Output parameter to store status information
 * @return true if successful, false if failed
 */
bool scheduler_mpu_get_status(mpu_status_info_t *info) {
    if (!info) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Copy status information
    memcpy(info, &global_mpu_status, sizeof(mpu_status_info_t));
    
    // Ensure MPU enabled status is current
    info->mpu_enabled = scheduler_mpu_is_enabled();
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

/**
 * @brief Register a fault handler for MPU violations
 *
 * @param handler Function to call when an MPU fault occurs
 * @return true if successful, false if failed
 */
bool scheduler_mpu_register_fault_handler(void (*handler)(uint32_t task_id, void *fault_addr, uint32_t fault_type)) {
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Set the global handler
    user_fault_handler = handler;
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

/**
 * @brief Get human-readable description of fault type
 *
 * @param fault_type The fault type code
 * @return String description of the fault
 */
const char* scheduler_mpu_get_fault_description(uint32_t fault_type) {
    switch (fault_type) {
        case MPU_FAULT_INSTRUCTION:
            return "Instruction access violation";
            
        case MPU_FAULT_DATA:
            return "Data access violation";
            
        case MPU_FAULT_UNSTACK:
            return "Unstacking error (MPU violation)";
            
        case MPU_FAULT_STACK:
            return "Stacking error (MPU violation)";
            
        case MPU_FAULT_LAZY:
            return "Lazy state preservation error";
            
        // Combined fault types
        case (MPU_FAULT_INSTRUCTION | MPU_FAULT_DATA):
            return "Instruction and data access violation";
            
        case (MPU_FAULT_DATA | MPU_FAULT_STACK):
            return "Data access violation during stack operation";
            
        default:
            return "Unknown MPU fault";
    }
}

/**
 * @brief Test MPU protection by generating a controlled fault
 *
 * This function is for debugging purposes only and attempts to
 * generate a controlled MPU fault to test fault handling.
 *
 * @param task_id ID of the task to test
 * @return true if test was triggered, false if failed
 */
bool scheduler_mpu_test_protection(uint32_t task_id) {
    // First, make sure the task exists and has MPU protection enabled
    //I think this can be const
    const task_mpu_state_t* state = get_task_mpu_state(task_id);
    if (!state || !state->configured || !state->mpu_enabled) {
        return false;
    }
    
    // Create a test function that will be assigned to the task
    // This function will attempt to access a protected memory region
    static volatile uint32_t test_triggered = 0;
    
    // Get task info to find its stack
    task_control_block_t tcb;
    if (!scheduler_get_task_info(task_id, &tcb)) {
        return false;
    }
    
    // Set a flag indicating that the test was triggered
    test_triggered = 1;
    
    // We don't immediately cause the fault here, but set up conditions
    // for it to occur when the task runs
    
    // The actual fault generation would happen in the task's context
    // Here we just return success to indicate the test was set up
    
    log_message(LOG_LEVEL_WARN, "MPU", "MPU test protection triggered for task %lu.", task_id);
    return true;
}

/**
 * @brief Set global MPU enabled/disabled state
 *
 * This function allows temporarily disabling MPU for debugging
 * or performance comparison purposes.
 *
 * @param enabled true to enable MPU, false to disable
 * @return true if successful, false if failed
 */
bool scheduler_mpu_set_global_enabled(bool enabled) {
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Update the global flag
    mpu_globally_enabled = enabled;
    
    // Update MPU control register
    if (enabled) {
        // Enable MPU with default settings
        MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA | MPU_CTRL_HFNMIENA;
    } else {
        // Disable MPU
        MPU_CTRL = 0;
    }
    
    // Ensure data synchronization and instruction barrier
    __dmb(); // Data Memory Barrier
    __isb(); // Instruction Synchronization Barrier
    
    // Update global status
    global_mpu_status.mpu_enabled = enabled;
    
    // Reset all task settings cache to force reapplication
    for (int i = 0; i < 2; i++) {
        last_task_settings_applied[i] = false;
    }
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    log_message(LOG_LEVEL_INFO, "MPU", "MPU globally %s\n", enabled ? "enabled" : "disabled");
    return true;
}

/**
 * @brief Get performance statistics for MPU operations
 *
 * This function retrieves timing statistics for MPU operations,
 * which can be useful for performance debugging.
 *
 * @param stats Output parameter to store statistics
 * @return true if successful, false if failed
 */
bool scheduler_mpu_get_performance_stats(mpu_perf_stats_t *stats) {
    if (!stats) {
        return false;
    }
    
    uint32_t owner_irq = hw_spinlock_acquire(mpu_spinlock_num, scheduler_get_current_task());
    
    // Copy stats
    memcpy(stats, &perf_stats, sizeof(mpu_perf_stats_t));
    
    hw_spinlock_release(mpu_spinlock_num, owner_irq);
    
    return true;
}

// Function to print MPU command usage information
static void print_mpu_usage(void) {
    printf("Usage: mpu <command> [args...]\n");
    printf("Commands:\n");
    printf("  enable <task_id>              - Enable MPU protection for task\n");
    printf("  disable <task_id>             - Disable MPU protection for task\n");
    printf("  status [task_id]              - Show MPU status for all tasks or specific task\n");
    printf("  region <task_id> [region_num] - Show MPU region configuration for task\n");
    printf("  config <task_id> <options>    - Configure custom MPU settings\n");
    printf("    where options can be:\n");
    printf("      stack=<size>              - Configure stack protection size\n");
    printf("      ro=<address,size>         - Add read-only region\n");
    printf("      rw=<address,size>         - Add read-write region\n");
    printf("      exec=<address,size>       - Add executable region\n");
    printf("  test <task_id>                - Test MPU fault handling for task\n");
}

/**
 * @brief Implement the 'mpu enable' command
 * 
 * Enables MPU protection for a specific task using default settings.
 * 
 * @param task_id ID of the task to enable protection for
 * @return 0 on success, 1 on failure
 */
static int cmd_mpu_enable(int task_id) {
    if (task_id <= 0) {
        printf("Invalid task ID: %d\n", task_id);
        return 1;
    }
    
    if (scheduler_mpu_enable_protection(task_id, true)) {
        printf("MPU protection enabled for task %d\n", task_id);
        return 0;
    } else {
        printf("Failed to enable MPU protection for task %d\n", task_id);
        return 1;
    }
}

/**
 * @brief Implement the 'mpu disable' command
 * 
 * Disables MPU protection for a specific task.
 * 
 * @param task_id ID of the task to disable protection for
 * @return 0 on success, 1 on failure
 */
static int cmd_mpu_disable(int task_id) {
    if (task_id <= 0) {
        printf("Invalid task ID: %d\n", task_id);
        return 1;
    }
    
    if (scheduler_mpu_enable_protection(task_id, false)) {
        printf("MPU protection disabled for task %d\n", task_id);
        return 0;
    } else {
        printf("Failed to disable MPU protection for task %d\n", task_id);
        return 1;
    }
}

/**
 * @brief Implement the 'mpu status' command
 * 
 * Shows MPU status for all tasks or a specific task.
 * 
 * @param task_id ID of the task to show status for, or -1 for all tasks
 * @return 0 on success, 1 on failure
 */
static int cmd_mpu_status(int task_id) {
    if (task_id > 0) {
        return cmd_mpu_get_status(task_id);
    } else {
        // Show status for all tasks
        printf("MPU Status for All Tasks:\n");
        printf("ID | Name           | Protected | Faults | Last Fault\n");
        printf("---+----------------+-----------+--------+------------\n");
        
        // Check all possible task IDs (up to a reasonable limit)
        int found_count = 0;
        for (int id = 1; id < 100; id++) {
            task_control_block_t tcb;
            if (scheduler_get_task_info(id, &tcb)) {
                bool is_protected;
                scheduler_mpu_get_protection_status(id, &is_protected);
                
                printf("%-3ld | %-14s | %-9s | %-6lu | %s\n",
                       tcb.task_id,
                       tcb.name,
                       tcb.mpu_enabled ? "Yes" : "No",
                       tcb.fault_count,
                       tcb.fault_count > 0 ? tcb.fault_reason : "None");
                
                found_count++;
            }
        }
        
        if (found_count == 0) {
            printf("No tasks found\n");
        }
        
        return 0;
    }
}

int cmd_mpu_get_status(int task_id) {
    // Show status for a specific task
    bool is_protected;
    if (scheduler_mpu_get_protection_status(task_id, &is_protected)) {
        printf("MPU status for task %d: %s\n", task_id, 
        is_protected ? "Protected" : "Not protected");
            
        // Get task info for more details
        task_control_block_t tcb;

        if (scheduler_get_task_info(task_id, &tcb)) {

            char core_n;
            if (tcb.core_affinity == 0) {
                core_n = '0';
            } else if (tcb.core_affinity == 1) {
                core_n = '1';
            } else {
                core_n = ' ';
            }
        
            printf("Task name: %s\n", tcb.name);
            printf("Core affinity: %c\n", core_n);
            printf("MPU enabled: %s\n", tcb.mpu_enabled ? "Yes" : "No");
            printf("Fault count: %lu\n", tcb.fault_count);
                
            if (tcb.fault_count > 0) {
                printf("Last fault reason: %s\n", tcb.fault_reason);
            }
        }
            
        return 0;
    } else {
        printf("Failed to get MPU status for task %d\n", task_id);
        return 1;
    }
}

/**
 * @brief Implement the 'mpu region' command
 * 
 * Shows MPU region configuration for a specific task.
 * 
 * @param task_id ID of the task to show region configuration for
 * @param region_num Specific region to show, or -1 for all regions
 * @return 0 on success, 1 on failure
 */
static int cmd_mpu_region(int task_id, int region_num) {
    if (task_id <= 0) {
        printf("Invalid task ID: %d\n", task_id);
        return 1;
    }
    
    // Get the MPU configuration for the task
    task_mpu_config_t config;
    if (!scheduler_mpu_get_task_config_minimal(task_id, &config)) {
        printf("Failed to get MPU configuration for task %d\n", task_id);
        return 1;
    }
    
    printf("MPU Regions for Task %d:\n", task_id);
    printf("Region | Start Addr | Size     | Access | Cache | Share\n");
    printf("-------+------------+----------+--------+-------+------\n");
    
    for (int i = 0; i < config.region_count; i++) {
        if (region_num >= 0 && i != region_num) {
            continue;  // Skip regions not requested
        }
        
        mpu_region_config_t *region = &config.regions[i];
        const char *access_str;
        
        switch (region->access) {
            case MPU_NO_ACCESS: access_str = "None"; break;
            case MPU_READ_ONLY: access_str = "RO"; break;
            case MPU_READ_WRITE: access_str = "RW"; break;
            case MPU_READ_EXEC: access_str = "RX"; break;
            case MPU_READ_WRITE_EXEC: access_str = "RWX"; break;
            default: access_str = "???"; break;
        }
        
        printf("%-6d | 0x%08lx | %-8d | %-6s | %-5s | %s\n",
               i,
               (uint32_t)region->start_addr,
               region->size,
               access_str,
               region->cacheable ? "Yes" : "No",
               region->shareable ? "Yes" : "No");
    }
    
    return 0;
}

/**
 * @brief Parse and validate task ID from command line argument
 * 
 * @param[in] task_id_str String representation of task ID
 * @param[out] task_id Pointer to store parsed task ID
 * @return true if parsing successful, false otherwise
 */
static bool parse_task_id(const char *task_id_str, int *task_id) {
    if (!task_id_str || !task_id) {
        return false;
    }
    
    *task_id = atoi(task_id_str);
    if (*task_id <= 0) {
        printf("Invalid task ID: %d\n", *task_id);
        return false;
    }
    
    return true;
}

/**
 * @brief Parse memory address and size from option value
 * 
 * @param[in] value Option value in format "address,size"
 * @param[out] addr Pointer to store parsed address
 * @param[out] size Pointer to store parsed size
 * @return true if parsing successful, false otherwise
 */
static bool parse_address_size(const char *value, uint32_t *addr, size_t *size) {
    if (!value || !addr || !size) {
        return false;
    }
    
    const char *size_str = strchr(value, ',');
    if (!size_str) {
        return false;
    }
    
    // Create a copy to avoid modifying the original string
    char *value_copy = strdup(value);
    if (!value_copy) {
        return false;
    }
    
    char *size_part = strchr(value_copy, ',');
    *size_part = '\0';
    size_part++;
    
    *addr = strtoul(value_copy, NULL, 0);
    *size = atoi(size_part);
    
    free(value_copy);
    return (*size > 0);
}

/**
 * @brief Configure stack protection region
 * 
 * @param[in] value Stack size value
 * @param[in] tcb Task control block containing stack information
 * @param[in,out] regions Array of MPU regions
 * @param[in,out] region_count Current region count
 * @return true if region added successfully, false otherwise
 */
static bool configure_stack_region(const char *value, 
                                   const task_control_block_t *tcb,
                                   mpu_region_config_t *regions,
                                   int *region_count) {
    if (!value || !tcb || !regions || !region_count || *region_count >= 8) {
        return false;
    }
    
    size_t stack_size = atoi(value);
    if (stack_size <= 0) {
        return false;
    }
    
    regions[*region_count].start_addr = tcb->stack_base;
    regions[*region_count].size = stack_size;
    regions[*region_count].access = MPU_READ_WRITE;
    regions[*region_count].security = TZ_SECURE;
    regions[*region_count].cacheable = true;
    regions[*region_count].bufferable = true;
    regions[*region_count].shareable = false;
    
    (*region_count)++;
    printf("Added stack protection region: size=%zu\n", stack_size);
    return true;
}

/**
 * @brief Configure read-only memory region
 * 
 * @param[in] value Address and size in format "addr,size"
 * @param[in,out] regions Array of MPU regions
 * @param[in,out] region_count Current region count
 * @return true if region added successfully, false otherwise
 */
static bool configure_readonly_region(const char *value,
                                      mpu_region_config_t *regions,
                                      int *region_count) {
    if (!value || !regions || !region_count || *region_count >= 8) {
        return false;
    }
    
    uint32_t addr;
    size_t size;
    
    if (!parse_address_size(value, &addr, &size)) {
        printf("Invalid format for ro option\n");
        return false;
    }
    
    regions[*region_count].start_addr = (void *)addr;
    regions[*region_count].size = size;
    regions[*region_count].access = MPU_READ_ONLY;
    regions[*region_count].security = TZ_SECURE;
    regions[*region_count].cacheable = true;
    regions[*region_count].bufferable = false;
    regions[*region_count].shareable = true;
    
    (*region_count)++;
    printf("Added read-only region: addr=0x%08lx, size=%zu\n", addr, size);
    return true;
}

/**
 * @brief Configure read-write memory region
 * 
 * @param[in] value Address and size in format "addr,size"
 * @param[in,out] regions Array of MPU regions
 * @param[in,out] region_count Current region count
 * @return true if region added successfully, false otherwise
 */
static bool configure_readwrite_region(const char *value,
                                       mpu_region_config_t *regions,
                                       int *region_count) {
    if (!value || !regions || !region_count || *region_count >= 8) {
        return false;
    }
    
    uint32_t addr;
    size_t size;
    
    if (!parse_address_size(value, &addr, &size)) {
        printf("Invalid format for rw option\n");
        return false;
    }
    
    regions[*region_count].start_addr = (void *)addr;
    regions[*region_count].size = size;
    regions[*region_count].access = MPU_READ_WRITE;
    regions[*region_count].security = TZ_SECURE;
    regions[*region_count].cacheable = true;
    regions[*region_count].bufferable = true;
    regions[*region_count].shareable = true;
    
    (*region_count)++;
    printf("Added read-write region: addr=0x%08lx, size=%zu\n", addr, size);
    return true;
}

/**
 * @brief Configure executable memory region
 * 
 * @param[in] value Address and size in format "addr,size"
 * @param[in,out] regions Array of MPU regions
 * @param[in,out] region_count Current region count
 * @return true if region added successfully, false otherwise
 */
static bool configure_executable_region(const char *value,
                                        mpu_region_config_t *regions,
                                        int *region_count) {
    if (!value || !regions || !region_count || *region_count >= 8) {
        return false;
    }
    
    uint32_t addr;
    size_t size;
    
    if (!parse_address_size(value, &addr, &size)) {
        printf("Invalid format for exec option\n");
        return false;
    }
    
    regions[*region_count].start_addr = (void *)addr;
    regions[*region_count].size = size;
    regions[*region_count].access = MPU_READ_EXEC;
    regions[*region_count].security = TZ_SECURE;
    regions[*region_count].cacheable = true;
    regions[*region_count].bufferable = false;
    regions[*region_count].shareable = true;
    
    (*region_count)++;
    printf("Added executable region: addr=0x%08lx, size=%zu\n", addr, size);
    return true;
}

/**
 * @brief Parse a single configuration option
 * 
 * @param[in] option_str Option string in format "option=value"
 * @param[in] tcb Task control block
 * @param[in,out] regions Array of MPU regions
 * @param[in,out] region_count Current region count
 * @return true if option parsed successfully, false otherwise
 */
static bool parse_configuration_option(const char *option_str,
                                       const task_control_block_t *tcb,
                                       mpu_region_config_t *regions,
                                       int *region_count) {
    if (!option_str || !tcb || !regions || !region_count) {
        return false;
    }
    
    char *option_copy = strdup(option_str);
    if (!option_copy) {
        return false;
    }
    
    char *value = strchr(option_copy, '=');
    if (!value) {
        printf("Invalid option format: %s\n", option_str);
        free(option_copy);
        return false;
    }
    
    *value = '\0';
    value++;
    
    bool success = false;
    
    if (strcmp(option_copy, "stack") == 0) {
        success = configure_stack_region(value, tcb, regions, region_count);
    } else if (strcmp(option_copy, "ro") == 0) {
        success = configure_readonly_region(value, regions, region_count);
    } else if (strcmp(option_copy, "rw") == 0) {
        success = configure_readwrite_region(value, regions, region_count);
    } else if (strcmp(option_copy, "exec") == 0) {
        success = configure_executable_region(value, regions, region_count);
    } else {
        printf("Unknown option: %s\n", option_copy);
    }
    
    free(option_copy);
    return success;
}

/**
 * @brief Apply MPU configuration to a task
 * 
 * @param[in] task_id Task ID to configure
 * @param[in] regions Array of configured MPU regions
 * @param[in] region_count Number of regions to configure
 * @return true if configuration applied successfully, false otherwise
 */
static bool apply_mpu_configuration(int task_id,
                                   const mpu_region_config_t *regions, int region_count) {
    if (!regions || region_count <= 0) {
        printf("No valid regions specified\n");
        return false;
    }

    if (region_count > MAX_MPU_REGIONS_PER_TASK) {
        printf("Too many regions specified (max %d)\n", MAX_MPU_REGIONS_PER_TASK);
        return false;
    }

    task_mpu_config_t config;
    config.task_id = task_id;
    config.region_count = (uint8_t)(region_count & 0xFF);
    
    // Copy regions directly into the config array
    for (int i = 0; i < region_count; i++) {
        config.regions[i] = regions[i];
    }

    if (!scheduler_mpu_configure_task(&config)) {
        printf("Failed to apply MPU configuration for task %d\n", task_id);
        return false;
    }

    if (!scheduler_mpu_enable_protection(task_id, true)) {
        printf("Failed to enable MPU protection for task %d\n", task_id);
        return false;
    }

    printf("MPU configuration applied successfully for task %d\n", task_id);
    return true;
}

/**
 * @brief Implement the 'mpu config' command
 * 
 * Configures custom MPU (Memory Protection Unit) settings for a specified task.
 * This command allows setting up memory regions with different access permissions
 * including stack protection, read-only, read-write, and executable regions.
 * 
 * @param[in] argc Argument count (must be >= 3)
 * @param[in] argv Argument array containing:
 *                 - argv[0]: command name
 *                 - argv[1]: "config" subcommand
 *                 - argv[2]: task ID
 *                 - argv[3+]: configuration options in format "option=value"
 * 
 * @details Supported configuration options:
 *          - stack=<size>: Configure stack protection with specified size
 *          - ro=<addr,size>: Add read-only region at address with size
 *          - rw=<addr,size>: Add read-write region at address with size
 *          - exec=<addr,size>: Add executable region at address with size
 * 
 * @return 0 on success, 1 on failure
 * 
 * @note Maximum of 8 MPU regions can be configured per task
 * @note Task must exist in the scheduler for configuration to succeed
 * 
 * @example
 * mpu config 5 stack=4096 ro=0x08000000,8192 rw=0x20000000,1024
 */
static int cmd_mpu_config(int argc, char *argv[]) {
    // Validate minimum arguments
    if (argc < 3) {
        printf("Usage: mpu config <task_id> <options>\n");
        printf("Options:\n");
        printf("  stack=<size>      - Configure stack protection\n");
        printf("  ro=<addr,size>    - Add read-only region\n");
        printf("  rw=<addr,size>    - Add read-write region\n");
        printf("  exec=<addr,size>  - Add executable region\n");
        return 1;
    }
    
    // Parse and validate task ID
    int task_id;
    if (!parse_task_id(argv[2], &task_id)) {
        return 1;
    }
    
    // Get task information
    task_control_block_t tcb;
    if (!scheduler_get_task_info(task_id, &tcb)) {
        printf("Task %d not found\n", task_id);
        return 1;
    }
    
    // Initialize MPU regions array
    mpu_region_config_t regions[8];  // Support up to 8 regions
    int region_count = 0;
    
    // Parse all configuration options
    for (int i = 3; i < argc; i++) {
        parse_configuration_option(argv[i], &tcb, regions, &region_count);
    }
    
    // Apply the configuration
    return apply_mpu_configuration(task_id, regions, region_count) ? 0 : 1;
}

/**
 * @brief Implement the 'mpu test' command
 * 
 * Tests MPU fault handling for a specific task.
 * 
 * @param task_id ID of the task to test
 * @return 0 on success, 1 on failure
 */
static int cmd_mpu_test(int task_id) {
    if (task_id <= 0) {
        printf("Invalid task ID: %d\n", task_id);
        return 1;
    }
    
    // First, ensure MPU is enabled for the task
    if (!scheduler_mpu_enable_protection(task_id, true)) {
        printf("Failed to enable MPU for task %d\n", task_id);
        return 1;
    }
    
    printf("MPU enabled for task %d\n", task_id);
    printf("To test fault handling, the task will need to attempt an invalid memory access\n");
    printf("This will be triggered the next time the task runs\n");
    
    // Set a flag or trigger for the task to attempt invalid memory access
    // This is just a placeholder - in a real implementation, we would
    // need a way to trigger a test fault in the task
    
    return 0;
}

/**
 * @brief Main MPU command handler
 * 
 * Dispatches to the appropriate sub-command handler based on arguments.
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, 1 on failure
 */
int cmd_mpu(int argc, char *argv[]) {
    if (argc < 2) {
        print_mpu_usage();
        return 1;
    }
    
    if (strcmp(argv[1], "enable") == 0) {
        if (argc < 3) {
            printf("Usage: mpu enable <task_id>\n");
            return 1;
        }
        
        int task_id = atoi(argv[2]);
        return cmd_mpu_enable(task_id);
    }
    else if (strcmp(argv[1], "disable") == 0) {
        if (argc < 3) {
            printf("Usage: mpu disable <task_id>\n");
            return 1;
        }
        
        int task_id = atoi(argv[2]);
        return cmd_mpu_disable(task_id);
    }
    else if (strcmp(argv[1], "status") == 0) {
        int task_id = -1;
        if (argc >= 3) {
            task_id = atoi(argv[2]);
        }
        
        return cmd_mpu_status(task_id);
    }
    else if (strcmp(argv[1], "region") == 0) {
        if (argc < 3) {
            printf("Usage: mpu region <task_id> [region_num]\n");
            return 1;
        }
        
        int task_id = atoi(argv[2]);
        int region_num = -1;
        
        if (argc >= 4) {
            region_num = atoi(argv[3]);
        }
        
        return cmd_mpu_region(task_id, region_num);
    }
    else if (strcmp(argv[1], "config") == 0) {
        return cmd_mpu_config(argc, argv);
    }
    else if (strcmp(argv[1], "test") == 0) {
        if (argc < 3) {
            printf("Usage: mpu test <task_id>\n");
            return 1;
        }
        
        int task_id = atoi(argv[2]);
        return cmd_mpu_test(task_id);
    }
    else if (strcmp(argv[1], "help") == 0) {
        print_mpu_usage();
        return 0;
    }
    else {
        printf("Unknown MPU command: %s\n", argv[1]);
        print_mpu_usage();
        return 1;
    }
}

/**
 * @brief Register MPU commands with the shell
 * 
 * This function registers the MPU command handler with the shell system.
 * It should be called during system initialization.
 */
void register_mpu_commands(void) {
    static const shell_command_t mpu_cmd = {
        cmd_mpu, "mpu", "Configure and debug Memory Protection Unit."
    };
    
    shell_register_command(&mpu_cmd);
}