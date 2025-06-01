/**
 * @file led_interrupt_driver.h
 * @brief LED driver with interrupt support for testing interrupt manager
 * @date 2025-05-25
 */

#ifndef LED_INTERRUPT_DRIVER_H
#define LED_INTERRUPT_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "interrupt_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// LED driver configuration
typedef struct {
    uint8_t led_pin;              // GPIO pin for LED (25 for Pico, varies for Pico W)
    uint32_t timer_period_us;     // Timer interrupt period in microseconds
    bool enable_gpio_interrupt;   // Enable GPIO interrupt on button press
    uint8_t button_pin;           // GPIO pin for button (optional)
} led_driver_config_t;

// LED patterns for testing
typedef enum {
    LED_PATTERN_OFF = 0,
    LED_PATTERN_ON,
    LED_PATTERN_BLINK_SLOW,
    LED_PATTERN_BLINK_FAST,
    LED_PATTERN_PULSE,
    LED_PATTERN_MORSE_SOS,
    LED_PATTERN_BURST,
    LED_PATTERN_COUNT
} led_pattern_t;

// LED driver context (opaque handle)
typedef struct led_driver_ctx_s led_driver_ctx_t;

// LED driver statistics
typedef struct {
    uint32_t timer_interrupts;
    uint32_t gpio_interrupts;
    uint32_t pattern_changes;
    uint32_t coalesced_events;
    uint32_t total_toggles;
} led_driver_stats_t;

/**
 * @brief External access to kernel LED driver context
 * 
 * This allows test applications to access the LED driver
 * context for testing and statistics gathering.
 * 
 * @note This is only available when LED driver is initialized via kernel
 */
extern led_driver_ctx_t* g_kernel_led_ctx;

// LED driver functions
led_driver_ctx_t* led_driver_init(const led_driver_config_t* config);
bool led_driver_deinit(led_driver_ctx_t* ctx);

// Basic LED control
bool led_driver_set_state(led_driver_ctx_t* ctx, bool state);
bool led_driver_toggle(led_driver_ctx_t* ctx);
bool led_driver_get_state(led_driver_ctx_t* ctx);

// Pattern control
bool led_driver_set_pattern(led_driver_ctx_t* ctx, led_pattern_t pattern);
led_pattern_t led_driver_get_pattern(led_driver_ctx_t* ctx);

// Interrupt testing functions
bool led_driver_test_timer_interrupts(led_driver_ctx_t* ctx, uint32_t count);
bool led_driver_test_coalescing(led_driver_ctx_t* ctx, interrupt_coalesce_mode_t mode);
bool led_driver_trigger_burst(led_driver_ctx_t* ctx, uint32_t burst_count);

// Statistics
bool led_driver_get_stats(led_driver_ctx_t* ctx, led_driver_stats_t* stats);
bool led_driver_reset_stats(led_driver_ctx_t* ctx);

// Configuration
void led_driver_get_default_config(led_driver_config_t* config);

// Kernel integration functions
bool led_driver_kernel_init(void);
void led_driver_kernel_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // LED_INTERRUPT_DRIVER_H