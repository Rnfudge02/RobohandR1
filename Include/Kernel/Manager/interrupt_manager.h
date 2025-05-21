/**
* @file interrupt_manager.h
* @brief Interrupt management with coalescing support for the scheduler
* @author Based on Robert Fudge's work
* @date 2025-05-17
* 
* This module provides interrupt registration, coalescing, and
* handling functionality for the Raspberry Pi Pico 2W (RP2350)
* scheduler system. It supports both direct and deferred interrupt
* handling with optional coalescing to reduce overhead.
*/

#ifndef INTERRUPT_MANAGER_H
#define INTERRUPT_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "hardware/irq.h"


/**
 * @defgroup int_const Interrupt Configuration Constants
 * @{
 */

/**
 * @brief Maximum number of registered interrupts.
 */
#define MAX_MANAGED_INTERRUPTS 32

/** @} */ // end of int_const group

/**
 * @brief Interrupt handler function prototype.
 * 
 * @param irq_num IRQ number.
 * @param context User context pointer.
 */
typedef void (*interrupt_handler_t)(uint32_t irq_num, void *context);

/**
 * @defgroup int_enum Interrupt Configuration Constants
 * @{
 */

/**
 * @brief Interrupt coalescing modes.
 */
typedef enum {
    INT_COALESCE_NONE = 0,     /**< No coalescing, immediate handling. */
    INT_COALESCE_TIME,         /**< Time-based coalescing. */
    INT_COALESCE_COUNT,        /**< Count-based coalescing. */
    INT_COALESCE_HYBRID        /**< Both time and count based. */
} interrupt_coalesce_mode_t;

/** @} */ // end of int_enum group

/**
 * @defgroup int_struct Interrupt Data structures
 * @{
 */

/**
 * @brief Interrupt handler registration structure
 */
typedef struct {
    uint64_t last_handled;             /**< Timestamp of last handling. */
    uint64_t last_triggered;           /**< Timestamp of last trigger. */
    uint32_t irq_num;                  /**< IRQ number. */
    uint32_t priority;                 /**< Interrupt priority. (0-3, lower is higher) */
    uint32_t total_interrupts;         /**< Total count of this interrupt. */
    uint32_t coalesced_count;          /**< Number of coalesced events. */
    uint32_t coalesce_time_us;         /**< Time-based coalescing period. (μs) */
    uint32_t coalesce_count;           /**< Count-based coalescing threshold. */
    interrupt_handler_t handler;       /**< User interrupt handler. */
    interrupt_coalesce_mode_t mode;    /**< Coalescing mode. */
    void *context;                     /**< User context pointer. */
    bool enabled;                      /**< Whether interrupt is enabled. */
    bool coalescing_enabled;           /**< Whether coalescing is enabled. */
} interrupt_config_t;

/**
 * @brief Interrupt statistics structure
 */
typedef struct {
    uint64_t total_processing_time_us; /**< Total time spent in handlers. */
    uint32_t total_interrupts;         /**< Total interrupts received. */
    uint32_t immediate_handled;        /**< Immediately handled interrupts. */
    uint32_t coalesced_events;         /**< Total coalesced events. */
    uint32_t coalesce_triggers;        /**< Number of coalesce handler triggers. */
    uint32_t max_coalesce_depth;       /**< Maximum coalescence depth. */
    uint32_t time_triggered_counts;    /**< Time-based trigger count. */
    uint32_t count_triggered_counts;   /**< Count-based trigger count. */
    uint32_t active_interrupt_count;   /**< Currently active interrupt count. */
} interrupt_stats_t;

/** @} */ // end of int_struct group

/**
 * @defgroup int_api Interrupt Application Programming Interface
 * @{
 */

/**
 * @brief Configure interrupt coalescing.
 * 
 * @param irq_num IRQ number to configure.
 * @param enabled Whether coalescing is enabled.
 * @param mode Coalescing mode.
 * @param time_us Time period for coalescing. (for time-based modes)
 * @param count Count threshold. (for count-based modes)
 * @return true if configuration successful.
 * @return false if configuration failed.
 */
__attribute__((section(".time_critical")))
bool interrupt_configure_coalescing(uint32_t irq_num, bool enabled,
    interrupt_coalesce_mode_t mode, uint32_t time_us, uint32_t count);

/**
 * @brief Get configuration for a specific interrupt.
 * 
 * @param irq_num IRQ number to query.
 * @param config Pointer to configuration structure to fill.
 * @return true if successful.
 * @return false if failed.
 */
__attribute__((section(".time_critical")))
bool interrupt_get_config(uint32_t irq_num, interrupt_config_t *config);

/**
 * @brief Get interrupt statistics.
 * 
 * @param stats Pointer to statistics structure to fill.
 * @return true if successful.
 * @return false if failed.
 */
__attribute__((section(".time_critical")))
bool interrupt_get_stats(interrupt_stats_t *stats);

/**
 * @brief Initialize the interrupt manager.
 * 
 * @return true if initialization successful.
 * @return false if initialization failed.
 */
bool interrupt_manager_init(void);

/**
 * @brief Process coalesced interrupts.
 * 
 * This should be called regularly from a task to handle
 * coalesced interrupts. It does not need to be called for
 * non-coalesced interrupts which are handled immediately.
 * 
 * @return Number of coalesced interrupts processed.
 */
__attribute__((section(".time_critical")))
uint32_t interrupt_process_coalesced(void);

/**
 * @brief Register an interrupt handler.
 * 
 * @param irq_num Hardware IRQ number to register.
 * @param handler Handler function.
 * @param context User context pointer passed to handler.
 * @param priority Interrupt priority. (0-3, lower is higher)
 * @return true if registration successful.
 * @return false if registration failed.
 */
bool interrupt_register(uint32_t irq_num, interrupt_handler_t handler,
    void *context, uint32_t priority);

/**
 * @brief Register a callback for global interrupt events.
 * 
 * @param callback Function to call for global events.
 * @param context User context pointer.
 * @return true if successful.
 * @return false if failed.
 */
__attribute__((section(".time_critical")))
bool interrupt_register_global_callback(void (*callback)
    (uint32_t event_type, void *context), void *context);

/**
 * @brief Reset interrupt statistics.
 * 
 * @return true if successful.
 * @return false if failed.
 */
__attribute__((section(".time_critical")))
bool interrupt_reset_stats(void);

/**
 * @brief Enable or disable an interrupt.
 * 
 * @param irq_num IRQ number to modify.
 * @param enabled true to enable, false to disable.
 * @return true if successful.
 * @return false if failed.
 */
__attribute__((section(".time_critical")))
bool interrupt_set_enabled(uint32_t irq_num, bool enabled);

/**
 * @brief Trigger software interrupt. (for testing)
 * 
 * @param irq_num IRQ number to trigger.
 * @return true if successful.
 * @return false if failed.
 */
bool interrupt_trigger_test(uint32_t irq_num);

/** @} */ // end of int_api group

/**
 * @defgroup int_cmd Interrupt Command Interface
 * @{
 */

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
int cmd_interrupt(int argc, char *argv[]);

/**
 * @brief Register interrupt commands with the shell.
 * 
 * This function registers the interrupt-related commands with the shell
 * system. It should be called during system initialization.
 */
void register_interrupt_commands(void);

/** @} */ // end of int_cmd group

#ifdef __cplusplus
}
#endif

#endif // INTERRUPT_MANAGER_H