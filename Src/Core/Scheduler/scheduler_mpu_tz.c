/**
* @file scheduler_mpu_tz.c
* @brief Memory Protection Unit and TrustZone configuration for the scheduler
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-13
*/

#include "scheduler_mpu_tz.h"
#include "scheduler.h"
#include "hardware/sync.h"
#include "hardware/structs/mpu.h"
#include "hardware/structs/sio.h"
#include "hardware/address_mapped.h"
#include "pico/platform.h"
#include <string.h>

/* Define maximum number of configurable regions per task */
#define MAX_MPU_REGIONS_PER_TASK 8

/* Define maximum number of tasks that can have MPU configurations */
#define MAX_MPU_TASKS 16

/* RP2350 has 8 MPU regions per core */
#define RP2350_MPU_REGIONS 8

/* ARM v8-M MPU register definitions */
#define MPU_TYPE        0xE000ED90  /* MPU Type Register */
#define MPU_CTRL        0xE000ED94  /* MPU Control Register */
#define MPU_RNR         0xE000ED98  /* MPU Region Number Register */
#define MPU_RBAR        0xE000ED9C  /* MPU Region Base Address Register */
#define MPU_RLAR        0xE000EDA0  /* MPU Region Limit Address Register */
#define MPU_RBAR_A1     0xE000EDA4  /* MPU alias registers */
#define MPU_RLAR_A1     0xE000EDA8
#define MPU_RBAR_A2     0xE000EDAC
#define MPU_RLAR_A2     0xE000EDB0
#define MPU_RBAR_A3     0xE000EDB4
#define MPU_RLAR_A3     0xE000EDB8

/* MPU Control Register bits */
#define MPU_CTRL_ENABLE          (1 << 0)
#define MPU_CTRL_HFNMIENA        (1 << 1)
#define MPU_CTRL_PRIVDEFENA      (1 << 2)

/* Region attribute bits for MPU_RLAR */
#define MPU_ATTR_XN              (1 << 0)    /* Execute Never */
#define MPU_ATTR_AP_RW_PRIV_ONLY (1 << 1)    /* RW by privileged code only */
#define MPU_ATTR_AP_RW_ANY       (3 << 1)    /* RW by any privilege level */
#define MPU_ATTR_AP_RO_PRIV_ONLY (5 << 1)    /* RO by privileged code only */
#define MPU_ATTR_AP_RO_ANY       (7 << 1)    /* RO by any privilege level */
#define MPU_ATTR_SH_NONE         (0 << 3)    /* Non-shareable */
#define MPU_ATTR_SH_OUTER        (2 << 3)    /* Outer shareable */
#define MPU_ATTR_SH_INNER        (3 << 3)    /* Inner shareable */
#define MPU_ATTR_ENABLE          (1 << 0)    /* Region enable bit */

/* Memory attributes for MPU region */
#define ATTR_DEVICE_nGnRnE       0x0         /* Device, non-Gathering, non-Reordering, no Early Write Acknowledgement */
#define ATTR_DEVICE_nGnRE        0x4         /* Device, non-Gathering, non-Reordering, Early Write Acknowledgement */
#define ATTR_NORMAL_WT_NT        0x5         /* Normal, Write-through, non-transient */
#define ATTR_NORMAL_WB_NT        0x7         /* Normal, Write-back, non-transient */
#define ATTR_NORMAL_NC           0x1         /* Normal, Non-cacheable */

/* Spin lock for MPU configuration synchronization between cores */
static spin_lock_t* mpu_spinlock;
static uint32_t mpu_spinlock_num;

/* Storage for task MPU configurations */
typedef struct {
    uint32_t task_id;
    bool configured;
    mpu_region_config_t regions[MAX_MPU_REGIONS_PER_TASK];
    uint8_t region_count;
} task_mpu_state_t;

static task_mpu_state_t task_mpu_states[MAX_MPU_TASKS];
static uint8_t num_configured_tasks = 0;

/* Private helper functions */

/**
 * @brief Get memory attribute encoding for the MPU
 */
__attribute__((section(".time_critical")))
static uint8_t get_memory_attr(bool cacheable, bool bufferable) {
    if (!cacheable && !bufferable) {
        return ATTR_NORMAL_NC;
    } else if (cacheable && !bufferable) {
        return ATTR_NORMAL_WT_NT;
    } else if (cacheable && bufferable) {
        return ATTR_NORMAL_WB_NT;
    } else {
        return ATTR_DEVICE_nGnRE;
    }
}

/**
 * @brief Get task MPU state by task ID
 */
__attribute__((section(".time_critical")))
static task_mpu_state_t* get_task_mpu_state(uint32_t task_id) {
    for (int i = 0; i < num_configured_tasks; i++) {
        if (task_mpu_states[i].task_id == task_id) {
            return &task_mpu_states[i];
        }
    }
    return NULL;
}

/**
 * @brief Calculate power of 2 size and alignment
 * 
 * MPU regions must be sized as powers of 2 and aligned to their size
 */
__attribute__((section(".time_critical")))
static void calculate_mpu_size(void *addr, size_t size, 
                            void **aligned_addr, size_t *aligned_size) {
    // Get closest power of 2 that is >= size
    size_t pow2_size = 32; // Minimum MPU region size is 32 bytes
    while (pow2_size < size) {
        pow2_size *= 2;
    }
    
    // Align address down to pow2_size boundary
    uintptr_t addr_val = (uintptr_t)addr;
    uintptr_t aligned = addr_val & ~(pow2_size - 1);
    
    // If original address was not aligned, we need a larger region
    if (aligned != addr_val) {
        pow2_size *= 2;
        aligned = addr_val & ~(pow2_size - 1);
    }
    
    *aligned_addr = (void *)aligned;
    *aligned_size = pow2_size;
}

/**
 * @brief Apply MPU region settings directly to hardware
 */
__attribute__((section(".time_critical")))
static void apply_mpu_region(int region_num, const mpu_region_config_t *config) {
    uint32_t rbar, rlar;
    uint32_t attr = 0;
    
    // Calculate aligned address and size (MPU requires power-of-2 alignment and size)
    void *aligned_addr;
    size_t aligned_size;
    calculate_mpu_size(config->start_addr, config->size, &aligned_addr, &aligned_size);
    
    // Base address must be aligned to region size
    rbar = (uint32_t)aligned_addr;
    
    // Configure attributes based on access permissions
    switch (config->access) {
        case MPU_NO_ACCESS:
            attr = 0; // No access for any privilege level
            break;
            
        case MPU_READ_ONLY:
            attr = MPU_ATTR_AP_RO_ANY | MPU_ATTR_XN;
            break;
            
        case MPU_READ_WRITE:
            attr = MPU_ATTR_AP_RW_ANY | MPU_ATTR_XN;
            break;
            
        case MPU_READ_EXEC:
            attr = MPU_ATTR_AP_RO_ANY;
            break;
            
        case MPU_READ_WRITE_EXEC:
            attr = MPU_ATTR_AP_RW_ANY;
            break;
    }
    
    // Configure shareability
    if (config->shareable) {
        attr |= MPU_ATTR_SH_INNER;
    } else {
        attr |= MPU_ATTR_SH_NONE;
    }
    
    // Configure memory type attributes
    uint8_t memattr = get_memory_attr(config->cacheable, config->bufferable);
    attr |= (memattr << 4);
    
    // Configure security attributes (TrustZone)
    if (config->security == TZ_NON_SECURE) {
        attr |= (1 << 5);
    } else if (config->security == TZ_NON_SECURE_CALLABLE) {
        attr |= (1 << 6);
    }
    
    // Set region limit address (note: RLAR contains limit address AND attributes)
    rlar = (uint32_t)aligned_addr + aligned_size - 1;
    rlar |= (attr << 8) | MPU_ATTR_ENABLE;
    
    // Write to MPU registers
    hw_write_masked(
        (io_rw_32*)(MPU_RNR),
        region_num,
        0xFF
    );
    hw_write_masked(
        (io_rw_32*)(MPU_RBAR),
        rbar,
        0xFFFFFFFF
    );
    hw_write_masked(
        (io_rw_32*)(MPU_RLAR),
        rlar,
        0xFFFFFFFF
    );
}

/**
 * @brief Disable an MPU region
 */
__attribute__((section(".time_critical")))
static void disable_mpu_region(int region_num) {
    // Select the region
    hw_write_masked(
        (io_rw_32*)(MPU_RNR),
        region_num,
        0xFF
    );
    
    // Clear the enable bit in RLAR
    hw_clear_bits((io_rw_32*)(MPU_RLAR), MPU_ATTR_ENABLE);
}

/* Public API implementation */

bool scheduler_mpu_tz_init(void) {
    // Initialize the spinlock for MPU configuration
    mpu_spinlock_num = next_striped_spin_lock_num();
    mpu_spinlock = spin_lock_init(mpu_spinlock_num);
    
    // Initialize MPU task state storage
    memset(task_mpu_states, 0, sizeof(task_mpu_states));
    num_configured_tasks = 0;
    
    // Enable MPU with default settings
    uint32_t ctrl = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;
    hw_write_masked(
        (io_rw_32*)(MPU_CTRL),
        ctrl,
        0x7 // Three lowest bits
    );
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_mpu_configure_task(const task_mpu_config_t *config) {
    if (!config || !config->regions || config->region_count == 0 || 
        config->region_count > MAX_MPU_REGIONS_PER_TASK) {
        return false;
    }
    
    // Acquire spinlock to prevent race conditions between cores
    uint32_t owner_irq = spin_lock_blocking(mpu_spinlock);
    
    // Check if task already has a configuration
    task_mpu_state_t *state = get_task_mpu_state(config->task_id);
    
    if (!state) {
        // No existing configuration, create new one if space available
        if (num_configured_tasks >= MAX_MPU_TASKS) {
            spin_unlock(mpu_spinlock, owner_irq);
            return false;
        }
        
        state = &task_mpu_states[num_configured_tasks++];
        state->task_id = config->task_id;
        state->configured = false;
        state->region_count = 0;
    }
    
    // Copy the configuration
    state->region_count = config->region_count;
    for (int i = 0; i < config->region_count; i++) {
        memcpy(&state->regions[i], &config->regions[i], sizeof(mpu_region_config_t));
    }
    
    state->configured = true;
    
    // Release spinlock
    spin_unlock(mpu_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_mpu_apply_task_settings(uint32_t task_id) {
    task_mpu_state_t *state;

    // Acquire spinlock
    uint32_t owner_irq = spin_lock_blocking(mpu_spinlock);
    
    // Find task configuration
    state = get_task_mpu_state(task_id);
    
    if (!state || !state->configured) {
        // No configuration found, use system defaults
        spin_unlock(mpu_spinlock, owner_irq);
        return false;
    }
    
    // First disable all MPU regions
    for (int i = 0; i < RP2350_MPU_REGIONS; i++) {
        disable_mpu_region(i);
    }
    
    // Apply task-specific regions
    int num_regions = state->region_count <= RP2350_MPU_REGIONS 
                     ? state->region_count : RP2350_MPU_REGIONS;
    
    for (int i = 0; i < num_regions; i++) {
        apply_mpu_region(i, &state->regions[i]);
    }
    
    // Release spinlock
    spin_unlock(mpu_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_mpu_reset_task_settings(uint32_t task_id) {
    uint32_t owner_irq = spin_lock_blocking(mpu_spinlock);
    
    // Disable all MPU regions
    for (int i = 0; i < RP2350_MPU_REGIONS; i++) {
        disable_mpu_region(i);
    }
    
    // Release spinlock
    spin_unlock(mpu_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_mpu_create_default_config(uint32_t task_id, 
    void *stack_start, size_t stack_size, void *code_start, size_t code_size,
    task_mpu_config_t *config) {

    static mpu_region_config_t regions[4];  // Stack, code, peripheral, shared
    
    if (!config || !stack_start || !code_start) {
        return false;
    }
    
    // Configure access to task's stack (RW, non-executable)
    regions[0].start_addr = stack_start;
    regions[0].size = stack_size;
    regions[0].access = MPU_READ_WRITE;
    regions[0].security = TZ_SECURE;
    regions[0].cacheable = true;
    regions[0].bufferable = true;
    regions[0].shareable = false;  // Stack is typically not shared between cores
    
    // Configure access to task's code (RX, not writable)
    regions[1].start_addr = code_start;
    regions[1].size = code_size;
    regions[1].access = MPU_READ_EXEC;
    regions[1].security = TZ_SECURE;
    regions[1].cacheable = true;
    regions[1].bufferable = false;  // Code shouldn't be buffered
    regions[1].shareable = true;    // Code can be shared between cores
    
    // Configure access to peripherals (device memory)
    regions[2].start_addr = (void *)0x40000000;  // Peripheral region in Cortex-M33
    regions[2].size = 0x20000000;               // 512MB peripheral space
    regions[2].access = MPU_READ_WRITE;
    regions[2].security = TZ_SECURE;
    regions[2].cacheable = false;
    regions[2].bufferable = false;
    regions[2].shareable = true;    // Peripherals are shared
    
    // Configure access to shared RAM region (if applicable)
    regions[3].start_addr = (void *)0x20000000;  // SRAM region
    regions[3].size = 0x100000;                 // 1MB (adjust as needed)
    regions[3].access = MPU_READ_WRITE;
    regions[3].security = TZ_SECURE;
    regions[3].cacheable = true;
    regions[3].bufferable = true;
    regions[3].shareable = true;    // Explicitly shared
    
    // Set up the configuration
    config->task_id = task_id;
    config->regions = regions;
    config->region_count = 4;
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_mpu_get_task_config(uint32_t task_id, task_mpu_config_t *config) {
    if (!config) {
        return false;
    }
    
    uint32_t owner_irq = spin_lock_blocking(mpu_spinlock);
    
    // Find task configuration
    task_mpu_state_t *state = get_task_mpu_state(task_id);
    
    if (!state || !state->configured) {
        spin_unlock(mpu_spinlock, owner_irq);
        return false;
    }
    
    // Copy configuration to output parameter
    config->task_id = task_id;
    config->region_count = state->region_count;
    config->regions = state->regions;
    
    // Release spinlock
    spin_unlock(mpu_spinlock, owner_irq);
    
    return true;
}

__attribute__((section(".time_critical")))
bool scheduler_mpu_is_accessible(void *addr, size_t size, bool write_access) {
    // This is a simplified implementation - a real one would need to check
    // against the actual MPU settings currently applied
    
    // For now, just check some basic ranges
    uintptr_t address = (uintptr_t)addr;
    
    // Check if address is in valid RAM range
    if (address >= 0x20000000 && address + size <= 0x20100000) {
        return true;
    }
    
    // Check if address is in valid flash range
    if (address >= 0x10000000 && address + size <= 0x11000000) {
        return true; // Allow read access to flash
    }
    
    // Check if address is in peripheral range and only allow if write_access is required
    if (address >= 0x40000000 && address + size <= 0x60000000) {
        return true; // Allow peripheral access
    }
    
    // Default: not accessible
    return false;
}