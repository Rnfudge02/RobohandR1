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

#include "usb_shell.h"

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
 * @brief Process coalesced interrupts
 * 
 * Called from the interrupt task to handle batched interrupts
 */
uint32_t interrupt_process_coalesced(void) {
    if (!g_interrupt_manager_initialized) {
        return 0;
    }
    
    uint32_t processed_count = 0;
    absolute_time_t current_time = get_absolute_time();
    
    uint32_t save = spin_lock_blocking(g_interrupt_lock);
    
    // Copy active bitset to avoid modifications during processing
    uint32_t active_bitset = g_coalesced_active;
    
    spin_unlock(g_interrupt_lock, save);
    
    // Process each active interrupt
    for (uint32_t irq = 0; irq < MAX_MANAGED_INTERRUPTS; irq++) {
        if (active_bitset & (1 << irq)) {
            save = spin_lock_blocking(g_interrupt_lock);
            
            interrupt_config_t *config = &g_interrupts[irq];
            
            // Check if handler is registered and coalescing is enabled
            if (config->handler != NULL && config->coalescing_enabled) {
                bool should_process = false;
                
                // Determine if we should process based on mode
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
                
                // Process if needed
                if (should_process && config->coalesced_count > 0) {
                    // Update statistics
                    if (config->coalesced_count > g_int_stats.max_coalesce_depth) {
                        g_int_stats.max_coalesce_depth = config->coalesced_count;
                    }
                    
                    // Save a local copy of count for processing
                    uint32_t count = config->coalesced_count;
                    
                    // Reset the counter
                    config->coalesced_count = 0;
                    
                    // Clear from active bitset if counter is now 0
                    g_coalesced_active &= ~(1 << irq);
                    
                    // Update timestamp
                    config->last_handled = current_time;
                    
                    // Release lock during processing
                    spin_unlock(g_interrupt_lock, save);
                    
                    // Notify global callback
                    if (g_global_callback) {
                        g_global_callback(EVENT_COALESCE_TRIGGERED, g_global_callback_context);
                    }
                    
                    // Execute handler with the coalesced count
                    uint64_t start_time = time_us_64();
                    
                    // Call the handler
                    for (uint32_t i = 0; i < count; i++) {
                        config->handler(irq, config->context);
                        processed_count++;
                    }
                    
                    uint64_t end_time = time_us_64();
                    
                    // Update processing time statistic
                    save = spin_lock_blocking(g_interrupt_lock);
                    g_int_stats.total_processing_time_us += (end_time - start_time);
                    g_int_stats.coalesce_triggers++;
                    spin_unlock(g_interrupt_lock, save);
                } else {
                    spin_unlock(g_interrupt_lock, save);
                }
            } else {
                spin_unlock(g_interrupt_lock, save);
            }
        }
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
__attribute__((section(".time_critical")))
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

// Forward declarations for shell commands
static int cmd_interrupt(int argc, char *argv[]);
static void print_interrupt_usage(void);
static int cmd_interrupt_stats(void);
static int cmd_interrupt_reset(void);
static int cmd_interrupt_test(int argc, char *argv[]);
static int cmd_interrupt_coalesce(int argc, char *argv[]);
static int cmd_interrupt_list(void);

/**
 * @brief Print usage information for interrupt commands
 */
static void print_interrupt_usage(void) {
    printf("Usage: interrupt <command> [args...]\n");
    printf("Commands:\n");
    printf("  stats                    - Show interrupt statistics\n");
    printf("  reset                    - Reset interrupt statistics\n");
    printf("  list                     - List registered interrupts\n");
    printf("  test <irq> [count]       - Test interrupt with optional count\n");
    printf("  coalesce <irq> <mode> [time] [count] - Configure coalescing\n");
    printf("    mode: none, time, count, hybrid\n");
    printf("    time: time in microseconds (for time/hybrid mode)\n");
    printf("    count: interrupt count (for count/hybrid mode)\n");
    printf("  help                     - Show this help\n");
}

/**
 * @brief Show interrupt statistics
 */
static int cmd_interrupt_stats(void) {
    interrupt_stats_t stats;
    
    if (!interrupt_get_stats(&stats)) {
        printf("Error: Failed to get interrupt statistics\n");
        return 1;
    }
    
    printf("Interrupt Manager Statistics:\n");
    printf("=============================\n");
    printf("Active interrupts: %lu\n", stats.active_interrupt_count);
    printf("Total interrupts: %lu\n", stats.total_interrupts);
    printf("Immediate handled: %lu\n", stats.immediate_handled);
    printf("Coalesced events: %lu\n", stats.coalesced_events);
    printf("Coalesce triggers: %lu\n", stats.coalesce_triggers);
    printf("Time triggered: %lu\n", stats.time_triggered_counts);
    printf("Count triggered: %lu\n", stats.count_triggered_counts);
    printf("Max coalesce depth: %lu\n", stats.max_coalesce_depth);
    printf("Total processing time: %llu us\n", stats.total_processing_time_us);
    
    if (stats.immediate_handled > 0) {
        printf("Avg processing time: %llu us\n", 
               stats.total_processing_time_us / stats.immediate_handled);
    }
    
    return 0;
}

/**
 * @brief Reset interrupt statistics
 */
static int cmd_interrupt_reset(void) {
    if (!interrupt_reset_stats()) {
        printf("Error: Failed to reset interrupt statistics\n");
        return 1;
    }
    
    printf("Interrupt statistics reset successfully\n");
    return 0;
}

/**
 * @brief List registered interrupts
 */
static int cmd_interrupt_list(void) {
    printf("Registered Interrupts:\n");
    printf("======================\n");
    
    // Try to get configuration for common interrupt numbers
    bool found_any = false;
    for (uint32_t irq = 0; irq < 32; irq++) {
        interrupt_config_t config;
        if (interrupt_get_config(irq, &config)) {
            found_any = true;
            const char* mode_str;
            switch (config.mode) {
                case INT_COALESCE_NONE: mode_str = "None"; break;
                case INT_COALESCE_TIME: mode_str = "Time"; break;
                case INT_COALESCE_COUNT: mode_str = "Count"; break;
                case INT_COALESCE_HYBRID: mode_str = "Hybrid"; break;
                default: mode_str = "Unknown"; break;
            }
            
            printf("IRQ %2lu: Priority=%lu, Enabled=%s, Coalescing=%s",
                   irq, config.priority, 
                   config.enabled ? "Yes" : "No",
                   config.coalescing_enabled ? "Yes" : "No");
            
            if (config.coalescing_enabled) {
                printf(" (%s", mode_str);
                if (config.coalesce_time_us > 0) {
                    printf(", %lu us", config.coalesce_time_us);
                }
                if (config.coalesce_count > 0) {
                    printf(", count=%lu", config.coalesce_count);
                }
                printf(")");
            }
            
            printf("\n");
            printf("      Interrupts: %lu, Coalesced: %lu\n",
                   config.total_interrupts, config.coalesced_count);
        }
    }
    
    if (!found_any) {
        printf("No interrupts currently registered\n");
    }
    
    return 0;
}

/**
 * @brief Test interrupt functionality
 */
static int cmd_interrupt_test(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: interrupt test <irq_num> [count]\n");
        return 1;
    }
    
    uint32_t irq_num = strtoul(argv[2], NULL, 0);
    uint32_t count = 1;
    
    if (argc >= 4) {
        count = strtoul(argv[3], NULL, 0);
        if (count == 0) count = 1;
        if (count > 1000) count = 1000; // Limit for safety
    }
    
    printf("Testing interrupt %lu with %lu triggers...\n", irq_num, count);
    
    // Get initial stats
    interrupt_stats_t stats_before;
    if (!interrupt_get_stats(&stats_before)) {
        printf("Error: Failed to get initial statistics\n");
        return 1;
    }
    
    // Trigger test interrupts
    uint32_t successful_triggers = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (interrupt_trigger_test(irq_num)) {
            successful_triggers++;
        }
        
        // Small delay between triggers
        sleep_us(1000); // 1ms
    }
    
    // Allow time for processing
    sleep_ms(100);
    
    // Get final stats
    interrupt_stats_t stats_after;
    if (!interrupt_get_stats(&stats_after)) {
        printf("Error: Failed to get final statistics\n");
        return 1;
    }
    
    printf("Test Results:\n");
    printf("  Triggers sent: %lu\n", count);
    printf("  Successful triggers: %lu\n", successful_triggers);
    printf("  Total interrupts before: %lu\n", stats_before.total_interrupts);
    printf("  Total interrupts after: %lu\n", stats_after.total_interrupts);
    printf("  Interrupts processed: %lu\n", 
           stats_after.total_interrupts - stats_before.total_interrupts);
    
    return 0;
}

/**
 * @brief Configure interrupt coalescing
 */
static int cmd_interrupt_coalesce(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: interrupt coalesce <irq> <mode> [time] [count]\n");
        printf("Modes: none, time, count, hybrid\n");
        return 1;
    }
    
    uint32_t irq_num = strtoul(argv[2], NULL, 0);
    const char* mode_str = argv[3];
    
    interrupt_coalesce_mode_t mode;
    uint32_t time_us = 0;
    uint32_t count = 0;
    bool enabled = true;
    
    // Parse mode
    if (strcmp(mode_str, "none") == 0) {
        mode = INT_COALESCE_NONE;
        enabled = false;
    } else if (strcmp(mode_str, "time") == 0) {
        mode = INT_COALESCE_TIME;
        if (argc >= 5) {
            time_us = strtoul(argv[4], NULL, 0);
        } else {
            time_us = 10000; // Default 10ms
        }
    } else if (strcmp(mode_str, "count") == 0) {
        mode = INT_COALESCE_COUNT;
        if (argc >= 5) {
            count = strtoul(argv[4], NULL, 0);
        } else {
            count = 5; // Default count of 5
        }
    } else if (strcmp(mode_str, "hybrid") == 0) {
        mode = INT_COALESCE_HYBRID;
        if (argc >= 5) {
            time_us = strtoul(argv[4], NULL, 0);
        } else {
            time_us = 10000; // Default 10ms
        }
        if (argc >= 6) {
            count = strtoul(argv[5], NULL, 0);
        } else {
            count = 5; // Default count of 5
        }
    } else {
        printf("Error: Unknown mode '%s'\n", mode_str);
        return 1;
    }
    
    // Configure coalescing
    if (!interrupt_configure_coalescing(irq_num, enabled, mode, time_us, count)) {
        printf("Error: Failed to configure coalescing for IRQ %lu\n", irq_num);
        return 1;
    }
    
    printf("Coalescing configured for IRQ %lu:\n", irq_num);
    printf("  Mode: %s\n", mode_str);
    printf("  Enabled: %s\n", enabled ? "Yes" : "No");
    if (time_us > 0) {
        printf("  Time threshold: %lu us\n", time_us);
    }
    if (count > 0) {
        printf("  Count threshold: %lu\n", count);
    }
    
    return 0;
}

/**
 * @brief Interrupt command handler.
 * 
 * This function handles the 'interrupt' shell command which provides
 * configuration and monitoring of interrupts and coalescing.
 * 
 * Usage: interrupt <subcommand> [options]
 * 
 * Subcommands:
 *   list         - List all registered interrupts.
 *   stats        - Show interrupt statistics.
 *   enable <irq> - Enable an interrupt.
 *   disable <irq> - Disable an interrupt.
 *   coalesce <irq> <mode> <time_us> <count> - Configure coalescing.
 *   test <irq>   - Trigger a test interrupt.
 *   reset        - Reset interrupt statistics.
 * 
 * @param argc Argument count.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero on error.
 */
static int cmd_interrupt(int argc, char *argv[]) {
    if (argc < 2) {
        print_interrupt_usage();
        return 1;
    }
    
    if (strcmp(argv[1], "stats") == 0) {
        return cmd_interrupt_stats();
    }
    else if (strcmp(argv[1], "reset") == 0) {
        return cmd_interrupt_reset();
    }
    else if (strcmp(argv[1], "list") == 0) {
        return cmd_interrupt_list();
    }
    else if (strcmp(argv[1], "test") == 0) {
        return cmd_interrupt_test(argc, argv);
    }
    else if (strcmp(argv[1], "coalesce") == 0) {
        return cmd_interrupt_coalesce(argc, argv);
    }
    else if (strcmp(argv[1], "help") == 0) {
        print_interrupt_usage();
        return 0;
    }
    else {
        printf("Unknown interrupt command: %s\n", argv[1]);
        print_interrupt_usage();
        return 1;
    }
}

/**
 * @brief Register interrupt manager commands with the shell
 */
void register_interrupt_manager_commands(void) {
    static const shell_command_t interrupt_cmd = {
        cmd_interrupt,
        "interrupt",
        "Interrupt manager testing and debugging commands"
    };
    
    shell_register_command(&interrupt_cmd);
    
    log_message(LOG_LEVEL_INFO, "Interrupt Mgr", "Shell commands registered");
}