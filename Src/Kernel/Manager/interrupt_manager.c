/**
* @file interrupt_manager.c
* @brief Implementation of interrupt manager with coalescing support
* @author Based on Robert Fudge's work
* @date 2025-05-17
*/

#include "interrupt_manager.h"
#include "log_manager.h"
#include "scheduler.h"
#include "stats.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "hardware/claim.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "pico/stdlib.h"

// Module private definitions

// Interrupt manager task ID from scheduler
static int g_interrupt_task_id = -1;

// Spinlocks for critical section protection
static spin_lock_t* g_interrupt_lock;
static uint g_interrupt_lock_num;

// Status flag for initialization
static bool g_interrupt_manager_initialized = false;

// Array of registered interrupt handlers
static interrupt_config_t g_interrupts[MAX_MANAGED_INTERRUPTS];

// Global interrupt statistics
static interrupt_stats_t g_int_stats;

// Global event callback
static void (*g_global_callback)(uint32_t event_type, void *context) = NULL;
static void *g_global_callback_context = NULL;

// Event types for global callback
#define EVENT_INTERRUPT_TRIGGERED  0x01
#define EVENT_COALESCE_TRIGGERED   0x02
#define EVENT_TASK_CREATED         0x03
#define EVENT_IRQ_ERROR            0xFF

// Bitset of active coalesced interrupts
static uint32_t g_coalesced_active = 0;

// Forward declarations
static void interrupt_manager_task(void *param);
static void interrupt_handler_wrapper(uint irq_num);
static bool setup_interrupt_task(void);
static void process_interrupt(interrupt_config_t *config);

/**
 * @brief Initialize the interrupt manager
 */
bool interrupt_manager_init(void) {
    // Check if already initialized
    if (g_interrupt_manager_initialized) {
        return true;
    }
    
    // Initialize spinlock
    g_interrupt_lock_num = spin_lock_claim_unused(true);
    if (g_interrupt_lock_num == UINT_MAX) {
        log_message(LOG_LEVEL_ERROR, "Interrupt Manager", "Failed to claim spinlock");
        return false;
    }
    g_interrupt_lock = spin_lock_instance(g_interrupt_lock_num);
    
    // Initialize interrupt handler array
    memset(g_interrupts, 0, sizeof(g_interrupts));
    memset(&g_int_stats, 0, sizeof(g_int_stats));
    
    // Create task for coalesced interrupt processing
    if (!setup_interrupt_task()) {
        log_message(LOG_LEVEL_ERROR, "Interrupt Manager", "Failed to create interrupt processing task");
        return false;
    }
    
    g_interrupt_manager_initialized = true;
    log_message(LOG_LEVEL_INFO, "Interrupt Manager", "Initialized successfully");
    
    return true;
}

/**
 * @brief Setup the interrupt processing task
 */
static bool setup_interrupt_task(void) {
    // Create task for processing coalesced interrupts
    g_interrupt_task_id = scheduler_create_task(
        interrupt_manager_task,       // Task function
        NULL,                         // No parameters
        2048,                         // Stack size
        TASK_PRIORITY_HIGH,           // High priority but not critical
        "int_mgr",                    // Task name
        0,                            // Core 0
        TASK_TYPE_PERSISTENT          // Persistent task
    );
    
    if (g_interrupt_task_id < 0) {
        return false;
    }
    
    // Set deadline for task to ensure regular execution
    // The deadline is soft to allow other critical tasks to run
    if (!scheduler_set_deadline(g_interrupt_task_id, DEADLINE_SOFT, 
                               5, // 5ms period
                               2, // 2ms deadline
                               1000)) { // 1000μs budget
        log_message(LOG_LEVEL_WARN, "Interrupt Manager", "Failed to set task deadline");
        // Non-critical failure, continue
    }
    
    return true;
}

/**
 * @brief Interrupt manager task function
 * 
 * This task processes coalesced interrupts at regular intervals.
 * 
 * @param param Unused parameter
 */
static void interrupt_manager_task(void *param) {
    (void)param; // Unused
    
    while (1) {
        // Process all coalesced interrupts
        interrupt_process_coalesced();
        
        // Yield to other tasks
        scheduler_delay(5); // 5ms delay
    }
}

/**
 * @brief Register a new interrupt handler
 */
bool interrupt_register(uint32_t irq_num, interrupt_handler_t handler, 
                       void *context, uint32_t priority) {
    if (!g_interrupt_manager_initialized) {
        return false;
    }
    
    if (irq_num >= MAX_MANAGED_INTERRUPTS || handler == NULL || priority > 3) {
        log_message(LOG_LEVEL_ERROR, "Interrupt Manager", "Invalid parameters for interrupt registration");
        return false;
    }
    
    // Check if IRQ already registered
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    if (g_interrupts[irq_num].handler != NULL) {
        // Already registered
        spin_unlock(g_interrupt_lock, save);
        log_message(LOG_LEVEL_WARN, "Interrupt Manager", "IRQ %lu already registered", irq_num);
        return false;
    }
    
    // Register the interrupt handler
    interrupt_config_t *config = &g_interrupts[irq_num];
    config->irq_num = irq_num;
    config->priority = priority;
    config->enabled = false; // Not enabled by default
    config->coalescing_enabled = false;
    config->mode = INT_COALESCE_NONE;
    config->coalesce_time_us = 0;
    config->coalesce_count = 0;
    config->handler = handler;
    config->context = context;
    config->total_interrupts = 0;
    config->coalesced_count = 0;
    config->last_handled = get_absolute_time();
    config->last_triggered = get_absolute_time();
    
    g_int_stats.active_interrupt_count++;
    
    // Connect the wrapper to hardware
    irq_set_exclusive_handler(irq_num, (void*) interrupt_handler_wrapper);
    irq_set_priority(irq_num, (uint8_t) (priority & 0xFF));
    
    spin_unlock(g_interrupt_lock, save);
    
    log_message(LOG_LEVEL_INFO, "Interrupt Manager", "Registered IRQ %lu with priority %lu", 
             irq_num, priority);
    
    return true;
}

/**
 * @brief Enable or disable an interrupt
 */
bool interrupt_set_enabled(uint32_t irq_num, bool enabled) {
    if (!g_interrupt_manager_initialized || irq_num >= MAX_MANAGED_INTERRUPTS) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    interrupt_config_t *config = &g_interrupts[irq_num];
    if (config->handler == NULL) {
        // Not registered
        spin_unlock(g_interrupt_lock, save);
        return false;
    }
    
    if (config->enabled != enabled) {
        config->enabled = enabled;
        
        // Configure hardware
        if (enabled) {
            irq_set_enabled(irq_num, true);
        } else {
            irq_set_enabled(irq_num, false);
        }
        
        log_message(LOG_LEVEL_INFO, "Interrupt Manager", "IRQ %lu %s", 
                 irq_num, enabled ? "enabled" : "disabled");
    }
    
    spin_unlock(g_interrupt_lock, save);
    return true;
}

/**
 * @brief Configure interrupt coalescing
 */
bool interrupt_configure_coalescing(uint32_t irq_num, bool enabled,
                                 interrupt_coalesce_mode_t mode,
                                 uint32_t time_us, uint32_t count) {
    if (!g_interrupt_manager_initialized || irq_num >= MAX_MANAGED_INTERRUPTS) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    interrupt_config_t *config = &g_interrupts[irq_num];
    if (config->handler == NULL) {
        // Not registered
        spin_unlock(g_interrupt_lock, save);
        return false;
    }
    
    // Configure coalescing
    config->coalescing_enabled = enabled;
    config->mode = mode;
    config->coalesce_time_us = time_us;
    config->coalesce_count = count;
    
    // Reset coalesced count
    config->coalesced_count = 0;
    
    // Log the change
    if (enabled) {
        log_message(LOG_LEVEL_INFO, "Interrupt Manager", "Coalescing enabled for IRQ %lu, mode=%d, time=%lu, count=%lu",
                 irq_num, mode, time_us, count);
    } else {
        log_message(LOG_LEVEL_INFO, "Interrupt Manager", "Coalescing disabled for IRQ %lu", irq_num);
    }
    
    spin_unlock(g_interrupt_lock, save);
    return true;
}

/**
 * @brief Get interrupt statistics
 */
bool interrupt_get_stats(interrupt_stats_t *stats) {
    if (!g_interrupt_manager_initialized || stats == NULL) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    memcpy(stats, &g_int_stats, sizeof(interrupt_stats_t));
    spin_unlock(g_interrupt_lock, save);
    
    return true;
}

/**
 * @brief Get configuration for a specific interrupt
 */
bool interrupt_get_config(uint32_t irq_num, interrupt_config_t *config) {
    if (!g_interrupt_manager_initialized || irq_num >= MAX_MANAGED_INTERRUPTS || config == NULL) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    if (g_interrupts[irq_num].handler == NULL) {
        // Not registered
        spin_unlock(g_interrupt_lock, save);
        return false;
    }
    
    memcpy(config, &g_interrupts[irq_num], sizeof(interrupt_config_t));
    
    spin_unlock(g_interrupt_lock, save);
    return true;
}

/**
 * @brief Check if the interrupt manager is initialized
 * 
 * @return true if initialized, false otherwise
 */
static bool is_interrupt_manager_initialized(void) {
    return g_interrupt_manager_initialized;
}

/**
 * @brief Get a thread-safe snapshot of active interrupts
 * 
 * @return Bitmap of currently active interrupts
 */
static uint32_t get_active_interrupts_snapshot(void) {
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    uint32_t active_bitset = g_coalesced_active;
    spin_unlock(g_interrupt_lock, save);
    return active_bitset;
}

/**
 * @brief Determine if an interrupt should be processed based on its mode
 * 
 * @param config Pointer to interrupt configuration
 * @param current_time Current system time
 * @return true if interrupt should be processed, false otherwise
 */
static bool should_process_interrupt(interrupt_config_t *config, absolute_time_t current_time) {
    if (config->handler == NULL || !config->coalescing_enabled || config->coalesced_count == 0) {
        return false;
    }
    
    bool should_process = false;
    
    switch (config->mode) {
        case INT_COALESCE_TIME:
            if (absolute_time_diff_us(config->last_handled, current_time) >= 
                config->coalesce_time_us) {
                should_process = true;
                g_int_stats.time_triggered_counts++;
            }
            break;
            
        case INT_COALESCE_COUNT:
            if (config->coalesced_count >= config->coalesce_count) {
                should_process = true;
                g_int_stats.count_triggered_counts++;
            }
            break;
            
        case INT_COALESCE_HYBRID:
            if (config->coalesced_count >= config->coalesce_count ||
                absolute_time_diff_us(config->last_handled, current_time) >= 
                config->coalesce_time_us) {
                should_process = true;
                
                if (config->coalesced_count >= config->coalesce_count) {
                    g_int_stats.count_triggered_counts++;
                } else {
                    g_int_stats.time_triggered_counts++;
                }
            }
            break;
            
        default:
            should_process = false;
            break;
    }
    
    return should_process;
}

/**
 * @brief Update maximum coalesce depth statistic if needed
 * 
 * @param count The coalesced count to check against current maximum
 */
static void update_max_coalesce_depth(uint32_t count) {
    if (count > g_int_stats.max_coalesce_depth) {
        g_int_stats.max_coalesce_depth = count;
    }
}

/**
 * @brief Reset coalesced interrupt state after processing
 * 
 * @param irq Interrupt number
 * @param config Pointer to interrupt configuration
 * @param current_time Current system time
 * @return Coalesced count before reset
 */
static uint32_t reset_coalesced_interrupt(uint32_t irq, interrupt_config_t *config, absolute_time_t current_time) {
    uint32_t count = config->coalesced_count;
    
    // Reset the counter
    config->coalesced_count = 0;
    
    // Clear from active bitset if counter is now 0
    g_coalesced_active &= ~(1 << irq);
    
    // Update timestamp
    config->last_handled = current_time;
    
    return count;
}

/**
 * @brief Execute the interrupt handler for coalesced interrupts
 * 
 * @param irq Interrupt number
 * @param config Pointer to interrupt configuration
 * @param count Number of coalesced interrupts to process
 * @return Number of processed interrupts
 */
static uint32_t execute_interrupt_handler(uint32_t irq, interrupt_config_t *config, uint32_t count) {
    // Notify global callback
    if (g_global_callback) {
        g_global_callback(EVENT_COALESCE_TRIGGERED, g_global_callback_context);
    }
    
    // Execute handler with the coalesced count
    uint64_t start_time = time_us_64();
    
    // Call the handler for each coalesced interrupt
    for (uint32_t i = 0; i < count; i++) {
        config->handler(irq, config->context);
    }
    
    uint64_t end_time = time_us_64();
    
    // Update processing time statistic
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    g_int_stats.total_processing_time_us += (end_time - start_time);
    g_int_stats.coalesce_triggers++;
    spin_unlock(g_interrupt_lock, save);
    
    return count;
}

/**
 * @brief Process a single coalesced interrupt
 * 
 * @param irq Interrupt number
 * @param active_bitset Bitmap of active interrupts
 * @param current_time Current system time
 * @return Number of processed interrupts
 */
static uint32_t process_single_interrupt(uint32_t irq, uint32_t active_bitset, absolute_time_t current_time) {
    if (!(active_bitset & (1 << irq))) {
        return 0;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    interrupt_config_t *config = &g_interrupts[irq];
    
    if (!should_process_interrupt(config, current_time)) {
        spin_unlock(g_interrupt_lock, save);
        return 0;
    }
    
    update_max_coalesce_depth(config->coalesced_count);
    uint32_t count = reset_coalesced_interrupt(irq, config, current_time);
    
    // Release lock during processing
    spin_unlock(g_interrupt_lock, save);
    
    return execute_interrupt_handler(irq, config, count);
}

/**
 * @brief Process coalesced interrupts
 * 
 * Called from the interrupt task to handle batched interrupts
 * 
 * @return Number of processed interrupts
 */
uint32_t interrupt_process_coalesced(void) {
    if (!is_interrupt_manager_initialized()) {
        return 0;
    }
    
    uint32_t processed_count = 0;
    absolute_time_t current_time = get_absolute_time();
    
    // Get a thread-safe snapshot of active interrupts
    uint32_t active_bitset = get_active_interrupts_snapshot();
    
    // Process each active interrupt
    for (uint32_t irq = 0; irq < MAX_MANAGED_INTERRUPTS; irq++) {
        processed_count += process_single_interrupt(irq, active_bitset, current_time);
    }
    
    return processed_count;
}



/**
 * @brief Reset interrupt statistics
 */
bool interrupt_reset_stats(void) {
    if (!g_interrupt_manager_initialized) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    // Preserve active interrupt count
    uint32_t active_count = g_int_stats.active_interrupt_count;
    
    // Reset stats
    memset(&g_int_stats, 0, sizeof(g_int_stats));
    
    // Restore active count
    g_int_stats.active_interrupt_count = active_count;
    
    spin_unlock(g_interrupt_lock, save);
    
    log_message(LOG_LEVEL_INFO, "Interrupt Manager", "Statistics reset");
    return true;
}

/**
 * @brief Register a callback for global interrupt events
 */
bool interrupt_register_global_callback(void (*callback)(uint32_t event_type, 
                                                      void *context),
                                     void *context) {
    if (!g_interrupt_manager_initialized) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    g_global_callback = callback;
    g_global_callback_context = context;
    
    spin_unlock(g_interrupt_lock, save);
    
    log_message(LOG_LEVEL_INFO, "Interrupt Manager", "Global callback registered");
    return true;
}

/**
 * @brief Process an interrupt immediately (non-coalesced)
 */
static void process_interrupt(interrupt_config_t *config) {
    if (config == NULL || config->handler == NULL) {
        return;
    }
    
    // Update statistics
    g_int_stats.immediate_handled++;
    
    uint64_t start_time = time_us_64();
    
    // Call the handler
    config->handler(config->irq_num, config->context);
    
    uint64_t end_time = time_us_64();
    
    // Update processing time statistic
    g_int_stats.total_processing_time_us += (end_time - start_time);
}

/**
 * @brief Main interrupt handler wrapper
 * 
 * This function is registered with hardware and dispatches
 * to the appropriate user handler.
 * 
 * @param irq_num Hardware IRQ number
 */

static void interrupt_handler_wrapper(uint irq_num) {
    if (irq_num >= MAX_MANAGED_INTERRUPTS) {
        // Invalid IRQ number
        return;
    }
    
    // No need for spinlock on fast path reading
    interrupt_config_t *config = &g_interrupts[irq_num];
    
    // Check if handler is registered and enabled
    if (config->handler == NULL || !config->enabled) {
        return;
    }
    
    // Update statistics
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    config->total_interrupts++;
    g_int_stats.total_interrupts++;
    config->last_triggered = get_absolute_time();
    
    // Notify global callback if registered
    if (g_global_callback) {
        // Call callback within lock to avoid race conditions
        g_global_callback(EVENT_INTERRUPT_TRIGGERED, g_global_callback_context);
    }
    
    if (config->coalescing_enabled) {
        // Coalesced handling
        config->coalesced_count++;
        g_int_stats.coalesced_events++;
        
        // Set active bit
        g_coalesced_active |= (1 << irq_num);
        
        // Check if we should process immediately based on count threshold
        bool process_now = false;
        
        if ((config->mode == INT_COALESCE_COUNT || config->mode == INT_COALESCE_HYBRID) && (config->coalesced_count >= config->coalesce_count)) {
            process_now = true;
        }
        
        spin_unlock(g_interrupt_lock, save);
        
        // Trigger the interrupt processing task to run sooner
        // This is a hint to the scheduler
        if (process_now && (g_interrupt_task_id >= 0)) {
            // Note: this is a non-blocking operation
            scheduler_resume_task(g_interrupt_task_id);
        }

    } else {
        // Immediate handling
        spin_unlock(g_interrupt_lock, save);
        process_interrupt(config);
    }
}

/**
 * @brief Trigger software interrupt for testing
 */
bool interrupt_trigger_test(uint32_t irq_num) {
    if (!g_interrupt_manager_initialized || irq_num >= MAX_MANAGED_INTERRUPTS) {
        return false;
    }
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    if (g_interrupts[irq_num].handler == NULL) {
        // Not registered
        spin_unlock(g_interrupt_lock, save);
        return false;
    }
    
    // Directly simulate an interrupt
    spin_unlock(g_interrupt_lock, save);
    
    // Call the wrapper as if hardware triggered it
    interrupt_handler_wrapper(irq_num);
    
    return true;
}