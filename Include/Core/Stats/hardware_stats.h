/**
* @file hardware_stats.h
* @brief Header for RP2350 cache and FPU status detection
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025
* 
* Header file containing API for detecting and benchmarking
* cache and FPU functionality on the RP2350 processor.
*/

#ifndef HARDWARE_STATS_H
#define HARDWARE_STATS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Structure containing cache and FPU statistics
 */
typedef struct {
    bool fpu_enabled;               /**< Whether FPU is enabled */
    bool icache_enabled;            /**< Whether instruction cache is enabled */
    bool dcache_enabled;            /**< Whether data cache is enabled */
    uint32_t fpu_benchmark_time;    /**< FPU benchmark execution time in microseconds */
    uint32_t cache_levels;          /**< Number of cache levels */
    uint32_t icache_line_size;      /**< Instruction cache line size in bytes */
    uint32_t dcache_line_size;      /**< Data cache line size in bytes */
} cache_fpu_stats_t;

/**
 * @brief Initialize the cache and FPU stats module
 * 
 * @return true if initialization successful
 * @return false if initialization failed
 */
bool cache_fpu_stats_init(void);

/**
 * @brief Check if FPU is enabled
 * 
 * @return true if FPU is enabled
 * @return false if FPU is disabled
 */
bool cache_fpu_is_fpu_enabled(void);

/**
 * @brief Check if instruction cache is enabled
 * 
 * @return true if instruction cache is enabled
 * @return false if instruction cache is disabled
 */
bool cache_fpu_is_icache_enabled(void);

/**
 * @brief Check if data cache is enabled
 * 
 * @return true if data cache is enabled
 * @return false if data cache is disabled
 */
bool cache_fpu_is_dcache_enabled(void);

/**
 * @brief Run a benchmark to test FPU performance
 * 
 * @return Benchmark execution time in microseconds
 */
uint32_t cache_fpu_benchmark_fpu(void);

/**
 * @brief Collect all cache and FPU statistics
 * 
 * @param stats Pointer to structure to store statistics
 */
void cache_fpu_get_stats(cache_fpu_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif // CACHE_FPU_STATS_H