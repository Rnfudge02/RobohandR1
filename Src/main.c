/**
* @file main.c
* @brief Main application with USB shell, scheduler
*/

#include "pico/stdlib.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"
#include "hardware/structs/xip_ctrl.h"  // For XIP control
#include "hardware/xip_cache.h"         // For XIP functions

// Define the control bit if not defined already
#ifndef XIP_CTRL_EN
#define XIP_CTRL_EN (1u << 0)  // Typically bit 0 is the enable bit

#endif

#include "hardware_stats.h"
#include "scheduler.h"
#include "sensor_manager.h"
#include "sensor_manager_init.h"
#include "stats.h"
#include "usb_shell.h"

#include "hardware_stats_shell_commands.h"
#include "sensor_manager_shell_commands.h"
#include "scheduler_shell_commands.h"
#include "stats_shell_commands.h"

#include <stdio.h>

void shell_task_func(void *params);
void check_xip(void);
uint32_t flash_calculation(uint32_t i);

__attribute__((section(".data.ram_func")))
int main() {

    // Initialize stdio for USB
    stdio_init_all();
    
    // Wait for USB to stabilize
    sleep_ms(2000);
    
    printf("\n\n=== RobohandR1 System Starting ===\n");
    
    // Initialize the scheduler
    printf("Initializing scheduler...\n");
    if (!scheduler_init()) {
        printf("Failed to initialize scheduler!\n");
        while (1) {
            tight_loop_contents();
        }
    }

    // Initialize the stats module
    printf("Initializing statistics module...\n");
    if (!stats_init()) {
        printf("Failed to initialize stats module!\n");
        while (1) {
            tight_loop_contents();
        }
    }

    // Initialize the stats module
    printf("Initializing hardware statistics module...\n");
    if (!cache_fpu_stats_init()) {
        printf("Failed to initialize hardware stats module!\n");
        while (1) {
            tight_loop_contents();
        }
    }

    if (!sensor_manager_init()) {
        printf("Failed to initialize sensor manager\n");
        printf("Sensors may not be available\n");
        
    } else {
        printf("Sensor manager initialized successfully\n");
    }

    check_xip();

    // Create shell task
    int shell_task_id = scheduler_create_task(
        shell_task_func,
        NULL,
        4096,
        TASK_PRIORITY_HIGH,
        "shell",
        0,
        TASK_TYPE_PERSISTENT
    );

    printf("Shell task creation result: %d\n", shell_task_id);

    // Also check task info
    task_control_block_t tcb;
    if (scheduler_get_task_info(shell_task_id, &tcb)) {
        printf("Shell task state: %d, priority: %d, core: %d\n", 
           tcb.state, tcb.priority, tcb.core_affinity);
    }

    printf("About to start scheduler...\n");
    if (!scheduler_start()) {
        printf("Failed to start scheduler!\n");
        while (1);
    }
    printf("Scheduler started successfully\n");
    
    printf("\n=== System Ready ===\n");
    printf("Type 'help' for available commands\n\n");
    
    // Main loop - just run the shell
    while (true) {
        scheduler_run_pending_tasks();
        tight_loop_contents();
    }
    
    return 0;
}

__attribute__((section(".data.ram_func")))
void shell_task_func(void *params) {
    (void)params;

    static bool initialized = false;

    if (!initialized) {
        // Initialize the shell
        printf("Initializing USB shell...\n");
        shell_init();
        register_scheduler_commands();
        register_stats_commands();
        register_cache_fpu_commands();
        register_sensor_manager_commands();

        printf("> ");

        initialized = true;
    }
    
    shell_task();
    scheduler_yield();
}

void check_xip(void) {
    // Report XIP status before changes
    printf("XIP Cache status (before):\n");
    printf("  XIP Cache: %s\n", (xip_ctrl_hw->ctrl & XIP_CTRL_EN) ? "Enabled" : "Disabled");
    
    // Try to enable the XIP cache if it has a control register
    xip_ctrl_hw->ctrl |= XIP_CTRL_EN;
    __dmb();  // Memory barrier
    
    // Verify XIP cache state
    printf("XIP Cache status (after):\n");
    printf("  XIP Cache: %s\n", (xip_ctrl_hw->ctrl & XIP_CTRL_EN) ? "Enabled" : "Disabled");
    
    // Test XIP cache performance
    printf("Measuring XIP performance...\n");
    uint32_t start, end;
    volatile uint32_t sum = 0;
    
    // First pass
    start = time_us_32();
    for (int i = 0; i < 1000; i++) {
        // Call a function that resides in flash (XIP region)
        sum += flash_calculation(i);  // Simple calculation
    }

    end = time_us_32();
    uint32_t first_time = end - start;
    printf("  First pass: %lu µs\n", first_time);
    
    // Second pass (should be faster if XIP cache is working)
    start = time_us_32();
    for (int i = 0; i < 1000; i++) {
        sum += flash_calculation(i);  // Same calculation
    }

    end = time_us_32();
    uint32_t second_time = end - start;
    printf("  Second pass: %lu µs\n", second_time);
    
    // Calculate speed improvement ratio
    float speedup = (float)first_time / (float)second_time;
    printf("  Speed improvement: %.2fx\n", speedup);
    
    // Use your hardware stats functions
    cache_fpu_stats_t stats;
    cache_fpu_get_stats(&stats);
    
    printf("Cache detection results:\n");
    printf("  I-Cache: %s\n", stats.icache_enabled ? "Enabled" : "Disabled");
    printf("  D-Cache: %s\n", stats.dcache_enabled ? "Enabled" : "Disabled");
    printf("  Cache performance test: %lu µs\n", stats.fpu_benchmark_time);
}

__attribute__((noinline, section(".text")))
uint32_t flash_calculation(uint32_t i) {
    return i * i + i + 1;
}