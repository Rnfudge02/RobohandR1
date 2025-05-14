/**
* @file hardware_stats.c
* @brief Implementation for RP2350 cache and FPU status detection
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025
* 
* This module provides functions to detect and benchmark 
* cache and FPU functionality on the RP2350 processor.
*/

#include "hardware_stats.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "hardware/structs/scb.h"

// Define cache control mask constants if not defined by SDK
#ifndef SCB_CCR_IC_Msk
#define SCB_CCR_IC_Msk (1 << 17)  // I-Cache enable bit in CCR

#endif

#ifndef SCB_CCR_DC_Msk
#define SCB_CCR_DC_Msk (1 << 16)  // D-Cache enable bit in CCR

#endif

// Flag for module initialization status
static bool initialized = false;

/**
 * @brief Initialize the cache and FPU stats module
 * 
 * @return true if initialization successful
 * @return false if initialization failed
 */
bool cache_fpu_stats_init(void) {
    // Nothing specific to initialize, just set the flag
    initialized = true;
    return true;
}

/**
 * @brief Check if FPU is enabled using runtime benchmarking
 * 
 * Since direct register access is causing issues on RP2350,
 * we'll use a benchmark approach to determine if the FPU is enabled.
 * 
 * @return true if FPU appears to be enabled based on performance
 * @return false if FPU seems disabled
 */
bool cache_fpu_is_fpu_enabled(void) {
    // Run two quick benchmarks - one with integer math, one with float
    uint32_t int_time, float_time;
    volatile int int_result = 1;
    volatile float float_result = 1.0f;
    
    // Integer benchmark
    uint32_t start_time = time_us_32();
    for(int i = 0; i < 1000; i++) {
        int_result *= 2;
        int_result /= 2;
    }
    int_time = time_us_32() - start_time;
    
    // Float benchmark  
    start_time = time_us_32();
    for(int i = 0; i < 1000; i++) {
        float_result *= 2.0f;
        float_result /= 2.0f;
    }
    float_time = time_us_32() - start_time;
    
    // If float operations are significantly faster than expected compared to int,
    // the FPU is likely enabled
    // Use compiler flag as a secondary check
    #if defined(PICO_FPU_ENABLED) && PICO_FPU_ENABLED
        return (float_time < int_time * 2);
    #else
        return false;
    #endif
}

/**
 * @brief Test if cache is enabled by performance measurement
 * 
 * @return true if cache appears to be enabled based on access patterns
 * @return false if cache seems disabled
 */
bool cache_is_cache_enabled(void) {
    // Create a large array in RAM
    #define TEST_SIZE 4096
    static volatile uint8_t test_array[TEST_SIZE];
    uint32_t uncached_time, cached_time;
    volatile uint32_t sum = 0;
    
    // Initialize array
    for (int i = 0; i < TEST_SIZE; i++) {
        test_array[i] = i & 0xFF;
    }
    
    // First pass - uncached access
    uint32_t start_time = time_us_32();
    for (int i = 0; i < TEST_SIZE; i++) {
        sum += test_array[i];
    }
    uncached_time = time_us_32() - start_time;
    
    // Second pass - should be cached if cache is enabled
    start_time = time_us_32();
    for (int i = 0; i < TEST_SIZE; i++) {
        sum += test_array[i];
    }
    cached_time = time_us_32() - start_time;
    
    // If second pass is significantly faster, cache is likely enabled
    // Use compiler flag as a secondary check
    #if defined(PICO_CACHE_ENABLED) && PICO_CACHE_ENABLED
        return (cached_time < uncached_time * 0.7);
    #else
        return false;
    #endif
}

/**
 * @brief Check if instruction cache is enabled on RP2350
 * 
 * @return true if instruction cache is enabled
 * @return false if instruction cache is disabled
 */
bool cache_fpu_is_icache_enabled(void) {
    return (scb_hw->ccr & SCB_CCR_IC_Msk) != 0;
}

/**
 * @brief Check if data cache is enabled on RP2350
 * 
 * @return true if data cache is enabled
 * @return false if data cache is disabled
 */
bool cache_fpu_is_dcache_enabled(void) {
    return (scb_hw->ccr & SCB_CCR_DC_Msk) != 0;
}

/**
 * @brief Run a benchmark to test FPU performance
 * 
 * @return Benchmark execution time in microseconds
 */
uint32_t cache_fpu_benchmark_fpu(void) {
    uint32_t start_time, end_time;
    volatile float result = 1.0f;
    
    // Get start time
    start_time = time_us_32();
    
    // Do some floating point operations
    for(int i = 0; i < 10000; i++) {
        result *= 1.000001f;
        result /= 1.000001f;
    }
    
    // Get end time
    end_time = time_us_32();
    
    // Prevent optimization from removing the calculation
    if (result != 1.0f) {
        printf("Unexpected result: %f\n", result);
    }
    
    return end_time - start_time;
}

/**
 * @brief Get estimated number of cache levels
 * 
 * @return Number of cache levels (estimated)
 */
static uint32_t get_cache_levels(void) {
    // RP2350 typically has one level of cache
    return 1;
}

/**
 * @brief Get estimated cache line sizes
 * 
 * @param icache_line_size Pointer to store instruction cache line size
 * @param dcache_line_size Pointer to store data cache line size
 */
static void get_cache_line_sizes(uint32_t* icache_line_size, uint32_t* dcache_line_size) {
    // Common cache line sizes for ARM cores
    *icache_line_size = 32;  // 32 bytes is typical
    *dcache_line_size = 32;  // 32 bytes is typical
}

/**
 * @brief Collect all cache and FPU statistics
 * 
 * @param stats Pointer to structure to store statistics
 */
void cache_fpu_get_stats(cache_fpu_stats_t* stats) {
    if (!initialized) {
        cache_fpu_stats_init();
    }
    
    // Check cache and FPU status
    stats->fpu_enabled = cache_fpu_is_fpu_enabled();
    stats->icache_enabled = cache_fpu_is_icache_enabled();
    stats->dcache_enabled = cache_fpu_is_dcache_enabled();
    
    // Run FPU benchmark
    stats->fpu_benchmark_time = cache_fpu_benchmark_fpu();
    
    // Get cache information
    stats->cache_levels = get_cache_levels();
    get_cache_line_sizes(&stats->icache_line_size, &stats->dcache_line_size);
}