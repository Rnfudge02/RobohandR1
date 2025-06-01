

/**
 * @file test_interrupt_integration.c
 * @brief Test application demonstrating interrupt manager and LED driver integration
 * @date 2025-05-25
 */

#include "kernel_init.h"
#include "interrupt_manager.h"
#include "led_driver.h"
#include "log_manager.h"
#include "usb_shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test interrupt handler
static void test_interrupt_handler(uint32_t irq_num, void* context) {
    static uint32_t call_count = 0;
    call_count++;
    
    // Log every 10th interrupt to avoid flooding
    if (call_count % 10 == 0) {
        log_message(LOG_LEVEL_INFO, "Test IRQ", "Test interrupt %lu triggered (count: %lu)", 
                   irq_num, call_count);
    }
    
    // Context might contain a counter or other test data
    if (context) {
        uint32_t* counter = (uint32_t*)context;
        (*counter)++;
    }
}

// Test application commands
static int cmd_test_interrupts(int argc, char *argv[]);
static int cmd_test_led_interrupts(int argc, char *argv[]);
static int cmd_test_coalescing(int argc, char *argv[]);

/**
 * @brief Test basic interrupt registration and triggering
 */
static int cmd_test_interrupts(int argc, char *argv[]) {
    printf("Testing basic interrupt functionality...\n");
    
    // Register a test interrupt handler
    static uint32_t test_counter = 0;
    uint32_t test_irq = 10; // Use IRQ 10 for testing
    
    if (!interrupt_register(test_irq, test_interrupt_handler, &test_counter, 2)) {
        printf("Error: Failed to register test interrupt\n");
        return 1;
    }
    
    printf("Registered test interrupt on IRQ %lu\n", test_irq);
    
    // Enable the interrupt
    if (!interrupt_set_enabled(test_irq, true)) {
        printf("Error: Failed to enable test interrupt\n");
        return 1;
    }
    
    printf("Enabled test interrupt\n");
    
    // Get initial statistics
    interrupt_stats_t stats_before;
    if (!interrupt_get_stats(&stats_before)) {
        printf("Error: Failed to get initial statistics\n");
        return 1;
    }
    
    // Trigger some test interrupts
    uint32_t trigger_count = 20;
    if (argc > 1) {
        trigger_count = atoi(argv[1]);
        if (trigger_count > 100) trigger_count = 100; // Safety limit
    }
    
    printf("Triggering %lu test interrupts...\n", trigger_count);
    
    for (uint32_t i = 0; i < trigger_count; i++) {
        interrupt_trigger_test(test_irq);
        sleep_us(1000); // 1ms delay between triggers
    }
    
    // Allow time for processing
    sleep_ms(100);
    
    // Get final statistics
    interrupt_stats_t stats_after;
    if (!interrupt_get_stats(&stats_after)) {
        printf("Error: Failed to get final statistics\n");
        return 1;
    }
    
    // Report results
    printf("\nTest Results:\n");
    printf("  Triggers sent: %lu\n", trigger_count);
    printf("  Handler counter: %lu\n", test_counter);
    printf("  Total interrupts before: %lu\n", stats_before.total_interrupts);
    printf("  Total interrupts after: %lu\n", stats_after.total_interrupts);
    printf("  Interrupts processed: %lu\n", 
           stats_after.total_interrupts - stats_before.total_interrupts);
    
    return 0;
}

/**
 * @brief Test LED driver interrupt integration
 */
static int cmd_test_led_interrupts(int argc, char *argv[]) {
    printf("Testing LED driver interrupt integration...\n");
    
    // Get LED driver statistics before test
    extern led_driver_ctx_t* g_kernel_led_ctx; // Access global context
    
    led_driver_stats_t led_stats_before = {0};
    if (g_kernel_led_ctx) {
        led_driver_get_stats(g_kernel_led_ctx, &led_stats_before);
    }
    
    // Test different LED patterns to exercise timer interrupts
    printf("Testing LED patterns with interrupt-driven timing...\n");
    
    if (g_kernel_led_ctx) {
        // Test different patterns
        led_pattern_t patterns[] = {
            LED_PATTERN_BLINK_FAST,
            LED_PATTERN_PULSE,
            LED_PATTERN_MORSE_SOS,
            LED_PATTERN_BURST
        };
        
        const char* pattern_names[] = {
            "Blink Fast",
            "Pulse", 
            "Morse SOS",
            "Burst"
        };
        
        for (int i = 0; i < 4; i++) {
            printf("  Setting pattern: %s\n", pattern_names[i]);
            led_driver_set_pattern(g_kernel_led_ctx, patterns[i]);
            sleep_ms(2000); // Run each pattern for 2 seconds
        }
        
        // Return to slow blink
        led_driver_set_pattern(g_kernel_led_ctx, LED_PATTERN_BLINK_SLOW);
        
        // Get final statistics
        led_driver_stats_t led_stats_after = {0};
        led_driver_get_stats(g_kernel_led_ctx, &led_stats_after);
        
        printf("\nLED Driver Statistics:\n");
        printf("  Timer interrupts: %lu -> %lu (+%lu)\n",
               led_stats_before.timer_interrupts,
               led_stats_after.timer_interrupts,
               led_stats_after.timer_interrupts - led_stats_before.timer_interrupts);
        printf("  GPIO interrupts: %lu -> %lu (+%lu)\n",
               led_stats_before.gpio_interrupts,
               led_stats_after.gpio_interrupts,
               led_stats_after.gpio_interrupts - led_stats_before.gpio_interrupts);
        printf("  Total toggles: %lu -> %lu (+%lu)\n",
               led_stats_before.total_toggles,
               led_stats_after.total_toggles,
               led_stats_after.total_toggles - led_stats_before.total_toggles);
        
        // Test interrupt burst
        printf("\nTesting interrupt burst...\n");
        led_driver_trigger_burst(g_kernel_led_ctx, 10);
        
    } else {
        printf("Error: LED driver not initialized\n");
        return 1;
    }
    
    return 0;
}

/**
 * @brief Test interrupt coalescing functionality
 */
static int cmd_test_coalescing(int argc, char *argv[]) {
    printf("Testing interrupt coalescing functionality...\n");
    
    uint32_t test_irq = 11; // Use IRQ 11 for coalescing test
    static uint32_t coalesce_counter = 0;
    
    // Register test interrupt
    if (!interrupt_register(test_irq, test_interrupt_handler, &coalesce_counter, 1)) {
        printf("Error: Failed to register coalescing test interrupt\n");
        return 1;
    }
    
    // Enable the interrupt
    if (!interrupt_set_enabled(test_irq, true)) {
        printf("Error: Failed to enable coalescing test interrupt\n");
        return 1;
    }
    
    // Test different coalescing modes
    interrupt_coalesce_mode_t modes[] = {
        INT_COALESCE_TIME,
        INT_COALESCE_COUNT,
        INT_COALESCE_HYBRID
    };
    
    const char* mode_names[] = {
        "Time-based",
        "Count-based", 
        "Hybrid"
    };
    
    for (int mode_idx = 0; mode_idx < 3; mode_idx++) {
        printf("\nTesting %s coalescing...\n", mode_names[mode_idx]);
        
        // Configure coalescing based on mode
        uint32_t time_us = 0;
        uint32_t count = 0;
        
        switch (modes[mode_idx]) {
            case INT_COALESCE_TIME:
                time_us = 50000; // 50ms
                break;
            case INT_COALESCE_COUNT:
                count = 5;
                break;
            case INT_COALESCE_HYBRID:
                time_us = 30000; // 30ms
                count = 3;
                break;
            default:
                break;
        }
        
        if (!interrupt_configure_coalescing(test_irq, true, modes[mode_idx], time_us, count)) {
            printf("Error: Failed to configure %s coalescing\n", mode_names[mode_idx]);
            continue;
        }
        
        // Get initial stats
        interrupt_stats_t stats_before;
        interrupt_get_stats(&stats_before);
        uint32_t counter_before = coalesce_counter;
        
        // Trigger rapid interrupts
        printf("  Triggering 20 rapid interrupts...\n");
        for (int i = 0; i < 20; i++) {
            interrupt_trigger_test(test_irq);
            sleep_us(5000); // 5ms between triggers
        }
        
        // Allow time for coalescing and processing
        sleep_ms(200);
        
        // Get final stats
        interrupt_stats_t stats_after;
        interrupt_get_stats(&stats_after);
        uint32_t counter_after = coalesce_counter;
        
        printf("  Results:\n");
        printf("    Handler calls: %lu\n", counter_after - counter_before);
        printf("    Coalesced events: %lu\n", 
               stats_after.coalesced_events - stats_before.coalesced_events);
        printf("    Coalesce triggers: %lu\n",
               stats_after.coalesce_triggers - stats_before.coalesce_triggers);
    }
    
    // Disable coalescing
    interrupt_configure_coalescing(test_irq, false, INT_COALESCE_NONE, 0, 0);
    
    return 0;
}

/**
 * @brief Register test commands with the shell
 */
void register_test_commands(void) {
    static const shell_command_t test_commands[] = {
        {cmd_test_interrupts, "test_int", "Test basic interrupt functionality [count]"},
        {cmd_test_led_interrupts, "test_led", "Test LED driver interrupt integration"},
        {cmd_test_coalescing, "test_coal", "Test interrupt coalescing functionality"}
    };
    
    for (int i = 0; i < 3; i++) {
        shell_register_command(&test_commands[i]);
    }
    
    log_message(LOG_LEVEL_INFO, "Test App", "Test commands registered");
}

/**
 * @brief Application-specific initialization
 * 
 * This function is called by the kernel during initialization
 * if the application defines it.
 */
void kernel_register_commands(void) {
    // Register our test commands
    register_test_commands();
    
    log_message(LOG_LEVEL_INFO, "Test App", "Application initialized");
    
    // Run a simple startup test
    printf("\n=== Interrupt Manager Integration Test ===\n");
    printf("Available test commands:\n");
    printf("  test_int [count]  - Test basic interrupt functionality\n");
    printf("  test_led          - Test LED driver integration\n"); 
    printf("  test_coal         - Test interrupt coalescing\n");
    printf("  interrupt stats   - Show interrupt statistics\n");
    printf("  interrupt list    - List registered interrupts\n");
    printf("==========================================\n\n");
}