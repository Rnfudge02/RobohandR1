/**
* @file cache_fpu_shell_commands.c
* @brief Shell command interface for cache and FPU status
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025
* 
* This file implements shell commands for interacting with the
* cache and FPU stats functionality through a command-line interface.
*/

#include "hardware_stats_shell_commands.h"
#include "hardware_stats.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Print command usage information
 */
static void print_usage(void) {
    printf("Usage: cachefpu [command]\n");
    printf("Commands:\n");
    printf("  status       - Show basic cache and FPU status\n");
    printf("  detail       - Show detailed cache and processor information\n");
    printf("  benchmark    - Run FPU benchmark\n");
    printf("  monitor <n>  - Monitor cache and FPU status for n seconds\n");
    printf("If no command is given, 'status' is the default.\n");
}

/**
 * @brief Display basic cache and FPU status
 * 
 * @return 0 on success
 */
static int cmd_status(void) {
    cache_fpu_stats_t stats;
    cache_fpu_get_stats(&stats);
    
    printf("RP2350 Cache/FPU Status:\n");
    printf("------------------------\n");
    printf("FPU: %s\n", stats.fpu_enabled ? "Enabled" : "Disabled");
    printf("Instruction Cache: %s\n", stats.icache_enabled ? "Enabled" : "Disabled");
    printf("Data Cache: %s\n", stats.dcache_enabled ? "Enabled" : "Disabled");
    
    return 0;
}

/**
 * @brief Display detailed cache and processor information
 * 
 * @return 0 on success
 */
static int cmd_detail(void) {
    cache_fpu_stats_t stats;
    cache_fpu_get_stats(&stats);
    
    printf("RP2350 Detailed Cache/FPU Information:\n");
    printf("-------------------------------------\n");
    printf("FPU: %s\n", stats.fpu_enabled ? "Enabled" : "Disabled");
    printf("Instruction Cache: %s\n", stats.icache_enabled ? "Enabled" : "Disabled");
    printf("Data Cache: %s\n", stats.dcache_enabled ? "Enabled" : "Disabled");
    printf("Cache Levels: %lu\n", stats.cache_levels);
    printf("Instruction Cache Line Size: %lu bytes\n", stats.icache_line_size);
    printf("Data Cache Line Size: %lu bytes\n", stats.dcache_line_size);
    
    // Get processor details from compiler macros
    printf("\nProcessor Information:\n");
    printf("---------------------\n");
    printf("CPU: Cortex-M33\n");
    
    // Print compiler optimization level
    #if defined(__OPTIMIZE_SIZE__)
        printf("Optimization: -Os (size)\n");
    #elif defined(__OPTIMIZE__)
        #if defined(__OPTIMIZE_LEVEL__) && __OPTIMIZE_LEVEL__ == 3
            printf("Optimization: -O3 (speed)\n");
        #elif defined(__OPTIMIZE_LEVEL__) && __OPTIMIZE_LEVEL__ == 2
            printf("Optimization: -O2\n");
        #elif defined(__OPTIMIZE_LEVEL__) && __OPTIMIZE_LEVEL__ == 1
            printf("Optimization: -O1\n");
        #else
            printf("Optimization: Enabled\n");
        #endif
    #else
        printf("Optimization: None\n");
    #endif
    
    // Print FPU ABI
    #if defined(__ARM_FP) && defined(__ARM_PCS_VFP)
        printf("FPU ABI: hard\n");
    #elif defined(__ARM_FP)
        printf("FPU ABI: softfp\n");
    #else
        printf("FPU ABI: soft\n");
    #endif
    
    return 0;
}

/**
 * @brief Run FPU benchmark
 * 
 * @return 0 on success
 */
static int cmd_benchmark(void) {
    printf("Running FPU benchmark...\n");
    
    // Run the benchmark multiple times for better accuracy
    const int runs = 5;
    uint32_t times[runs];
    uint32_t total = 0;
    
    for (int i = 0; i < runs; i++) {
        times[i] = cache_fpu_benchmark_fpu();
        total += times[i];
        printf("Run %d: %lu us\n", i+1, times[i]);
    }
    
    printf("\nAverage execution time: %lu us\n", total / runs);
    
    // Check if FPU is enabled
    if (!cache_fpu_is_fpu_enabled()) {
        printf("\nWARNING: FPU is currently disabled!\n");
        printf("Performance may be significantly improved by enabling the FPU.\n");
    }
    
    return 0;
}

/**
 * @brief Monitor cache and FPU status for a period of time
 * 
 * @param seconds Duration to monitor in seconds
 * @return 0 on success, -1 on error
 */
static int cmd_monitor(int seconds) {
    if (seconds <= 0 || seconds > 60) {
        printf("Invalid duration. Please specify between 1 and 60 seconds.\n");
        return -1;
    }
    
    printf("Monitoring cache and FPU for %d seconds...\n", seconds);
    printf("Press any key to stop.\n\n");
    
    printf("Time(s)  FPU  I-Cache  D-Cache  Benchmark(us)\n");
    printf("-------  ---  -------  -------  ------------\n");
    
    // Clear any pending input
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
        // Discard any character
    }
    
    for (int i = 0; i < seconds; i++) {
        cache_fpu_stats_t stats;
        cache_fpu_get_stats(&stats);
        
        printf("%7d  %3s  %7s  %7s  %12lu\n", 
               i,
               stats.fpu_enabled ? "Yes" : "No",
               stats.icache_enabled ? "Yes" : "No",
               stats.dcache_enabled ? "Yes" : "No",
               stats.fpu_benchmark_time);
        
        // Check for key press to exit early
        for (int j = 0; j < 10; j++) {  // Split into smaller intervals for better responsiveness
            if (getchar_timeout_us(100000) != PICO_ERROR_TIMEOUT) {  // 100ms
                printf("\nMonitoring stopped by user.\n");
                return 0;
            }
        }
    }
    
    printf("\nMonitoring complete.\n");
    return 0;
}

/**
 * @brief Main command handler for cache/FPU shell commands
 * 
 * This function parses the command line arguments and calls the appropriate
 * command handler function.
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, -1 on failure
 */
int cmd_cachefpu(int argc, char *argv[]) {
    // If no command given, show status
    if (argc < 2) {
        return cmd_status();
    }

    // Parse command
    if (strcmp(argv[1], "status") == 0) {
        return cmd_status();
    }
    else if (strcmp(argv[1], "detail") == 0) {
        return cmd_detail();
    }
    else if (strcmp(argv[1], "benchmark") == 0) {
        return cmd_benchmark();
    }
    else if (strcmp(argv[1], "monitor") == 0) {
        int seconds = 10;  // Default duration
        if (argc > 2) {
            seconds = atoi(argv[2]);
        }
        return cmd_monitor(seconds);
    }
    else if (strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return -1;
    }
}

/**
 * @brief Register cache and FPU commands with the shell
 * 
 * This function registers the cache and FPU command handler with the shell system.
 * It should be called during system initialization.
 */
void register_cache_fpu_commands(void) {
    // Register the command with your shell system
    // Create a static shell_command_t struct first
    static const shell_command_t cachefpu_cmd = {
        "cachefpu",
        "Cache and FPU statistics and controls",
        cmd_cachefpu
    };
   
    // Then register it
    shell_register_command(&cachefpu_cmd);
}