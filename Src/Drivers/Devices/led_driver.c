/**
 * @file led_driver.c
 * @brief LED driver implementation with interrupt manager integration
 * @date 2025-05-25
 */

#include "interrupt_manager.h"
#include "led_driver.h"
#include "log_manager.h"
#include "scheduler.h"

#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include <stdlib.h>
#include <string.h>

// Default pin definitions
#define PICO_DEFAULT_LED_PIN 25
#define DEFAULT_BUTTON_PIN 22

// Timer hardware interrupts for testing
#define TIMER_IRQ_0 0
#define TIMER_IRQ_1 1

// LED driver context structure
struct led_driver_ctx_s {
    led_driver_config_t config;
    
    // Current state
    bool led_state;
    led_pattern_t current_pattern;
    uint32_t pattern_step;
    uint32_t pattern_counter;
    
    // Interrupt management
    int timer_irq_id;
    int gpio_irq_id;
    bool interrupts_enabled;
    
    // SDK timer for more reliable operation
    struct repeating_timer sdk_timer;
    bool using_sdk_timer;
    
    // Statistics
    led_driver_stats_t stats;
    
    // Pattern timing
    absolute_time_t last_pattern_time;
    uint32_t pattern_period_us;
    
    // Test state
    bool test_active;
    uint32_t test_remaining;
};

// Global context for interrupt handlers
static led_driver_ctx_t* g_led_ctx = NULL;

// Global context for kernel integration
led_driver_ctx_t* g_kernel_led_ctx = NULL;

// LED pattern timings in microseconds
static const uint32_t pattern_timings[] = {
    [LED_PATTERN_OFF] = 0,
    [LED_PATTERN_ON] = 0,
    [LED_PATTERN_BLINK_SLOW] = 1000000,  // 1 second
    [LED_PATTERN_BLINK_FAST] = 200000,   // 200ms
    [LED_PATTERN_PULSE] = 50000,         // 50ms
    [LED_PATTERN_MORSE_SOS] = 150000,    // 150ms
    [LED_PATTERN_BURST] = 25000,         // 25ms
};

// Forward declarations
static void led_timer_interrupt_handler(uint32_t irq_num, void* context);
static void led_gpio_interrupt_handler(uint32_t irq_num, void* context);
static void led_pattern_update(led_driver_ctx_t* ctx);
static bool led_setup_timer_interrupt(led_driver_ctx_t* ctx);
static bool led_setup_gpio_interrupt(led_driver_ctx_t* ctx);

// SDK timer callback function
static bool led_timer_callback(struct repeating_timer *t) {
    (void)t;
    
    led_driver_ctx_t* ctx = g_led_ctx;
    if (ctx) {
        // Call the interrupt handler directly
        led_timer_interrupt_handler(TIMER_IRQ_0, ctx);
    }
    
    return true; // Keep repeating
}

// Hardware timer interrupt handler (if using direct hardware)
static void __isr timer_0_irq_handler(void) {
    // Clear the interrupt
    hw_clear_bits(&timer_hw->intr, 1u << 0);
    
    // Call our handler
    if (g_led_ctx) {
        led_timer_interrupt_handler(TIMER_IRQ_0, g_led_ctx);
    }
}

// GPIO interrupt handler
static void __isr gpio_irq_handler(void) {
    led_driver_ctx_t* ctx = g_led_ctx;
    if (!ctx || !ctx->config.enable_gpio_interrupt) {
        return;
    }
    
    // Get which GPIO caused the interrupt
    uint32_t events = gpio_get_irq_event_mask(ctx->config.button_pin);
    
    if (events & GPIO_IRQ_EDGE_FALL) {
        // Clear the interrupt
        gpio_acknowledge_irq(ctx->config.button_pin, GPIO_IRQ_EDGE_FALL);
        
        // Call our handler
        led_gpio_interrupt_handler(IO_IRQ_BANK0, ctx);
    }
}

led_driver_ctx_t* led_driver_init(const led_driver_config_t* config) {
    if (config == NULL) {
        return NULL;
    }
    
    led_driver_ctx_t* ctx = malloc(sizeof(led_driver_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    
    memset(ctx, 0, sizeof(led_driver_ctx_t));
    ctx->config = *config;
    ctx->current_pattern = LED_PATTERN_OFF;
    ctx->last_pattern_time = get_absolute_time();
    ctx->timer_irq_id = -1;
    ctx->gpio_irq_id = -1;
    
    // Initialize LED GPIO
    gpio_init(ctx->config.led_pin);
    gpio_set_dir(ctx->config.led_pin, GPIO_OUT);
    gpio_put(ctx->config.led_pin, false);
    ctx->led_state = false;
    
    // Initialize button GPIO if specified
    if (ctx->config.enable_gpio_interrupt && ctx->config.button_pin < 30) {
        gpio_init(ctx->config.button_pin);
        gpio_set_dir(ctx->config.button_pin, GPIO_IN);
        gpio_pull_up(ctx->config.button_pin);
    }
    
    // Set global context for interrupt handlers
    g_led_ctx = ctx;
    
    // Set up interrupts
    if (!led_setup_timer_interrupt(ctx)) {
        log_message(LOG_LEVEL_WARN, "LED Driver", "Failed to setup timer interrupt");
    }
    
    if (ctx->config.enable_gpio_interrupt && !led_setup_gpio_interrupt(ctx)) {
        log_message(LOG_LEVEL_WARN, "LED Driver", "Failed to setup GPIO interrupt");
    }
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "LED driver initialized on pin %u", ctx->config.led_pin);
    return ctx;
}

static bool led_setup_timer_interrupt(led_driver_ctx_t* ctx) {
    // Try SDK repeating timer first (more reliable)
    int32_t period_ms = ctx->config.timer_period_us / 1000;
    if (period_ms < 1) period_ms = 1;
    
    if (add_repeating_timer_ms(period_ms, led_timer_callback, NULL, &ctx->sdk_timer)) {
        ctx->using_sdk_timer = true;
        log_message(LOG_LEVEL_INFO, "LED Driver", "Using SDK repeating timer (%d ms)", period_ms);
        return true;
    }
    
    // Fallback to interrupt manager integration
    log_message(LOG_LEVEL_INFO, "LED Driver", "Using interrupt manager timer");
    
    // Register timer interrupt with interrupt manager
    if (!interrupt_register(TIMER_IRQ_0, led_timer_interrupt_handler, ctx, 2)) {
        log_message(LOG_LEVEL_ERROR, "LED Driver", "Failed to register timer interrupt");
        return false;
    }
    
    // Configure coalescing for timer interrupts
    if (!interrupt_configure_coalescing(TIMER_IRQ_0, true, INT_COALESCE_TIME, 10000, 0)) {
        log_message(LOG_LEVEL_WARN, "LED Driver", "Failed to configure timer interrupt coalescing");
    }
    
    // Enable the interrupt
    if (!interrupt_set_enabled(TIMER_IRQ_0, true)) {
        log_message(LOG_LEVEL_ERROR, "LED Driver", "Failed to enable timer interrupt");
        return false;
    }
    
    // Set up hardware timer
    hw_set_bits(&timer_hw->inte, 1u << 0);
    irq_set_exclusive_handler(TIMER_IRQ_0, timer_0_irq_handler);
    irq_set_enabled(TIMER_IRQ_0, true);
    
    // Set initial alarm
    uint64_t target = timer_hw->timerawl + ctx->config.timer_period_us;
    timer_hw->alarm[0] = (uint32_t) target;
    
    ctx->timer_irq_id = TIMER_IRQ_0;
    ctx->using_sdk_timer = false;
    
    return true;
}

static bool led_setup_gpio_interrupt(led_driver_ctx_t* ctx) {
    if (!ctx->config.enable_gpio_interrupt || ctx->config.button_pin >= 30) {
        return true; // Not enabled or invalid pin
    }
    
    // Register GPIO interrupt with interrupt manager
    if (!interrupt_register(IO_IRQ_BANK0, led_gpio_interrupt_handler, ctx, 1)) {
        log_message(LOG_LEVEL_WARN, "LED Driver", "Failed to register GPIO interrupt");
        return false;
    }
    
    // Configure coalescing for GPIO interrupts
    if (!interrupt_configure_coalescing(IO_IRQ_BANK0, true, INT_COALESCE_COUNT, 0, 3)) {
        log_message(LOG_LEVEL_WARN, "LED Driver", "Failed to configure GPIO interrupt coalescing");
    }
    
    // Enable the interrupt in interrupt manager
    if (!interrupt_set_enabled(IO_IRQ_BANK0, true)) {
        log_message(LOG_LEVEL_WARN, "LED Driver", "Failed to enable GPIO interrupt");
        return false;
    }
    
    // Set up hardware GPIO interrupt
    gpio_set_irq_enabled(ctx->config.button_pin, GPIO_IRQ_EDGE_FALL, true);
    irq_set_exclusive_handler(IO_IRQ_BANK0, gpio_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
    
    ctx->gpio_irq_id = IO_IRQ_BANK0;
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "GPIO interrupt set up on pin %u", ctx->config.button_pin);
    return true;
}

static void led_timer_interrupt_handler(uint32_t irq_num, void* context) {
    (void) irq_num;
    
    led_driver_ctx_t* ctx = (led_driver_ctx_t*)context;
    if (ctx == NULL) {
        return;
    }
    
    ctx->stats.timer_interrupts++;
    
    // Update LED pattern
    led_pattern_update(ctx);
    
    // Handle test mode
    if (ctx->test_active && ctx->test_remaining > 0) {
        ctx->test_remaining--;
        if (ctx->test_remaining == 0) {
            ctx->test_active = false;
            log_message(LOG_LEVEL_INFO, "LED Driver", "Timer interrupt test completed");
        }
    }
    
    // Re-arm the hardware timer if not using SDK timer
    if (!ctx->using_sdk_timer) {
        uint64_t target = timer_hw->timerawl + ctx->config.timer_period_us;
        timer_hw->alarm[0] = (uint32_t) target;
    }
}

static void led_gpio_interrupt_handler(uint32_t irq_num, void* context) {
    (void)irq_num;
    led_driver_ctx_t* ctx = (led_driver_ctx_t*)context;
    if (ctx == NULL) {
        return;
    }
    
    ctx->stats.gpio_interrupts++;
    
    // Toggle LED pattern on button press
    led_pattern_t next_pattern = (ctx->current_pattern + 1) % LED_PATTERN_COUNT;
    led_driver_set_pattern(ctx, next_pattern);
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "Button pressed, pattern changed to %d", next_pattern);
}

static void led_pattern_update(led_driver_ctx_t* ctx) {
    if (ctx->current_pattern == LED_PATTERN_OFF) {
        led_driver_set_state(ctx, false);
        return;
    }
    
    if (ctx->current_pattern == LED_PATTERN_ON) {
        led_driver_set_state(ctx, true);
        return;
    }
    
    absolute_time_t now = get_absolute_time();
    uint64_t elapsed = absolute_time_diff_us(ctx->last_pattern_time, now);
    
    if (elapsed >= ctx->pattern_period_us) {
        ctx->pattern_step++;
        ctx->last_pattern_time = now;
        
        switch (ctx->current_pattern) {
            case LED_PATTERN_BLINK_SLOW:
            case LED_PATTERN_BLINK_FAST:
                led_driver_toggle(ctx);
                break;
                
            case LED_PATTERN_PULSE:
                // Sine wave-like pulsing (simplified)
                led_driver_set_state(ctx, (ctx->pattern_step % 20) < 10);
                break;
                
            case LED_PATTERN_MORSE_SOS:
                // S(3 dots) O(3 dashes) S(3 dots)
                {
                    uint32_t step = ctx->pattern_step % 24;
                    bool state = false;
                    
                    if (step < 6) {  // S - 3 short pulses
                        state = (step % 2) == 0;
                    } else if (step < 18) {  // O - 3 long pulses
                        uint32_t dash_step = (step - 6) % 4;
                        state = (dash_step < 3);
                    } else if (step < 24) {  // S - 3 short pulses
                        uint32_t s2_step = (step - 18) % 2;
                        state = (s2_step == 0);
                    }
                    
                    led_driver_set_state(ctx, state);
                }
                break;
                
            case LED_PATTERN_BURST:
                // Rapid burst of 5 flashes, then pause
                {
                    uint32_t burst_step = ctx->pattern_step % 15;
                    led_driver_set_state(ctx, burst_step < 10 && (burst_step % 2) == 0);
                }
                break;
                
            default:
                break;
        }
    }
}

bool led_driver_set_state(led_driver_ctx_t* ctx, bool state) {
    if (ctx == NULL) {
        return false;
    }
    
    if (ctx->led_state != state) {
        ctx->led_state = state;
        gpio_put(ctx->config.led_pin, state);
        ctx->stats.total_toggles++;
    }
    
    return true;
}

bool led_driver_toggle(led_driver_ctx_t* ctx) {
    if (ctx == NULL) {
        return false;
    }
    
    return led_driver_set_state(ctx, !ctx->led_state);
}

bool led_driver_get_state(led_driver_ctx_t* ctx) {
    return ctx ? ctx->led_state : false;
}

bool led_driver_set_pattern(led_driver_ctx_t* ctx, led_pattern_t pattern) {
    if (ctx == NULL || pattern >= LED_PATTERN_COUNT) {
        return false;
    }
    
    ctx->current_pattern = pattern;
    ctx->pattern_step = 0;
    ctx->pattern_counter = 0;
    ctx->last_pattern_time = get_absolute_time();
    ctx->pattern_period_us = pattern_timings[pattern];
    ctx->stats.pattern_changes++;
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "Pattern changed to %d", pattern);
    return true;
}

led_pattern_t led_driver_get_pattern(led_driver_ctx_t* ctx) {
    return ctx ? ctx->current_pattern : LED_PATTERN_OFF;
}

bool led_driver_test_timer_interrupts(led_driver_ctx_t* ctx, uint32_t count) {
    if (ctx == NULL) {
        return false;
    }
    
    ctx->test_active = true;
    ctx->test_remaining = count;
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "Starting timer interrupt test for %lu interrupts", count);
    
    // Set fast blinking pattern for visual feedback
    led_driver_set_pattern(ctx, LED_PATTERN_BLINK_FAST);
    
    return true;
}

bool led_driver_test_coalescing(led_driver_ctx_t* ctx, interrupt_coalesce_mode_t mode) {
    if (ctx == NULL) {
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "Testing interrupt coalescing mode %d", mode);
    
    // Only reconfigure if using interrupt manager (not SDK timer)
    if (!ctx->using_sdk_timer && ctx->timer_irq_id >= 0) {
        uint32_t time_us = 0;
        uint32_t count = 0;
        
        switch (mode) {
            case INT_COALESCE_TIME:
                time_us = 50000;  // 50ms
                break;
            case INT_COALESCE_COUNT:
                count = 5;        // Every 5 interrupts
                break;
            case INT_COALESCE_HYBRID:
                time_us = 30000;  // 30ms
                count = 3;        // Or every 3 interrupts
                break;
            default:
                return false;
        }
        
        if (!interrupt_configure_coalescing(ctx->timer_irq_id, true, mode, time_us, count)) {
            log_message(LOG_LEVEL_ERROR, "LED Driver", "Failed to configure coalescing");
            return false;
        }
    }
    
    // Start test with burst pattern
    led_driver_set_pattern(ctx, LED_PATTERN_BURST);
    led_driver_test_timer_interrupts(ctx, 100);  // 100 interrupt test
    
    return true;
}

bool led_driver_trigger_burst(led_driver_ctx_t* ctx, uint32_t burst_count) {
    if (ctx == NULL) {
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "LED Driver", "Triggering %lu interrupt burst", burst_count);
    
    // If using interrupt manager, trigger software interrupts
    if (!ctx->using_sdk_timer && ctx->timer_irq_id >= 0) {
        for (uint32_t i = 0; i < burst_count; i++) {
            interrupt_trigger_test(ctx->timer_irq_id);
            sleep_us(1000);  // 1ms between triggers
        }
    } else {
        // If using SDK timer, simulate by calling handler directly
        for (uint32_t i = 0; i < burst_count; i++) {
            led_timer_interrupt_handler(TIMER_IRQ_0, ctx);
            sleep_us(1000);
        }
    }
    
    return true;
}

bool led_driver_get_stats(led_driver_ctx_t* ctx, led_driver_stats_t* stats) {
    if (ctx == NULL || stats == NULL) {
        return false;
    }
    
    *stats = ctx->stats;
    return true;
}

bool led_driver_reset_stats(led_driver_ctx_t* ctx) {
    if (ctx == NULL) {
        return false;
    }
    
    memset(&ctx->stats, 0, sizeof(led_driver_stats_t));
    return true;
}

void led_driver_get_default_config(led_driver_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(led_driver_config_t));
    config->led_pin = PICO_DEFAULT_LED_PIN;
    config->timer_period_us = 100000;  // 100ms = 10Hz
    config->enable_gpio_interrupt = true;
    config->button_pin = DEFAULT_BUTTON_PIN;
}

bool led_driver_deinit(led_driver_ctx_t* ctx) {
    if (ctx == NULL) {
        return false;
    }
    
    // Cancel SDK timer if using it
    if (ctx->using_sdk_timer) {
        cancel_repeating_timer(&ctx->sdk_timer);
    }
    
    // Disable interrupts
    if (ctx->timer_irq_id >= 0) {
        interrupt_set_enabled(ctx->timer_irq_id, false);
        irq_set_enabled(ctx->timer_irq_id, false);
    }
    
    if (ctx->gpio_irq_id >= 0) {
        interrupt_set_enabled(ctx->gpio_irq_id, false);
        irq_set_enabled(ctx->gpio_irq_id, false);
    }
    
    // Turn off LED
    gpio_put(ctx->config.led_pin, false);
    
    // Clear global context
    if (g_led_ctx == ctx) {
        g_led_ctx = NULL;
    }
    if (g_kernel_led_ctx == ctx) {
        g_kernel_led_ctx = NULL;
    }
    
    free(ctx);
    return true;
}

// Kernel integration functions
bool led_driver_kernel_init(void) {
    led_driver_config_t config;
    led_driver_get_default_config(&config);
    
    // Initialize with default settings
    g_kernel_led_ctx = led_driver_init(&config);
    if (g_kernel_led_ctx == NULL) {
        log_message(LOG_LEVEL_ERROR, "LED Init", "Failed to initialize LED driver");
        return false;
    }
    
    // Set a simple blinking pattern to show the system is alive
    led_driver_set_pattern(g_kernel_led_ctx, LED_PATTERN_BLINK_SLOW);
    
    log_message(LOG_LEVEL_INFO, "LED Init", "LED driver initialized successfully");
    return true;
}

void led_driver_kernel_deinit(void) {
    if (g_kernel_led_ctx != NULL) {
        led_driver_deinit(g_kernel_led_ctx);
        g_kernel_led_ctx = NULL;
    }
}