/**
* @file stats.h
* @brief System statistics module for tracking performance metrics.
*/

#ifndef _STATS_H_
#define _STATS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "scheduler.h"

/**
 * @defgroup stats_constant Stats Configuration Constants
 * @{
 */

// Maximum number of registered buffers.
#define MAX_REGISTERED_BUFFERS 16

// Maximum number of tasks to track.
#define MAX_TASK_STATS 32

/** @} */ // end of shell_constant group

/**
 * @defgroup stats_enum Stats Enumerations
 * @{
 */

/**
 * @brief System optimization states.
 */
typedef enum {
    OPT_NONE                    = 0x00,
    OPT_FREQUENCY_SCALING       = 0x01,
    OPT_VOLTAGE_SCALING         = 0x02,
    OPT_DMA_ENABLED            = 0x04,
    OPT_CACHE_ENABLED          = 0x08,
    OPT_MULTICORE_ENABLED      = 0x10,
    OPT_INTERRUPT_COALESCING   = 0x20,
    OPT_POWER_GATING           = 0x40,
    OPT_DOUBLE_BUFFERING       = 0x80
} optimization_state_t;

/** @} */ // end of shell_enum group

/**
 * @brief Buffer registration structure for double buffering.
 */
typedef struct {
    const char *name;               // Buffer name.
    void *buffer_a;                 // First buffer.
    void *buffer_b;                 // Second buffer.
    size_t buffer_size;             // Size of each buffer.
    volatile void **active_buffer;  // Pointer to currently active buffer.
    uint32_t swap_count;            // Number of buffer swaps.
    uint64_t last_swap_time_us;     // Time of last swap.
    bool is_registered;             // Registration status.
} buffer_registration_t;

/**
 * @defgroup stats_struct Stats Structures
 * @{
 */

/**
 * @brief Get all registered buffers with their IDs.
 * @param buffer_info Array to store buffer info with ID.
 * @param max_buffers Maximum number of buffers to retrieve.
 * @return Number of buffers retrieved.
 */
typedef struct {
    int id;
    buffer_registration_t info;
} buffer_info_with_id_t;

/**
 * @brief Structure containing cache and FPU statistics.
 */
typedef struct {
    bool fpu_enabled;               /**< Whether FPU is enabled. */
    bool icache_enabled;            /**< Whether instruction cache is enabled. */
    bool dcache_enabled;            /**< Whether data cache is enabled. */
    uint32_t fpu_benchmark_time;    /**< FPU benchmark execution time in microseconds. */
    uint32_t cache_levels;          /**< Number of cache levels. */
    uint32_t icache_line_size;      /**< Instruction cache line size in bytes. */
    uint32_t dcache_line_size;      /**< Data cache line size in bytes. */
} hwstats_stats_t;

/**
 * @brief Optimization suggestion based on stats.
 */
typedef struct {
    optimization_state_t optimization;
    const char *description;
    uint8_t priority;  // 0-10, higher is more important.
    float expected_improvement_percent;
} optimization_suggestion_t;

/**
 * @brief System operating statistics.
 */
typedef struct {
    uint64_t uptime_us;             // System uptime in microseconds.
    uint32_t system_freq_hz;        // Current system frequency.
    uint32_t voltage_mv;            // Current voltage in millivolts.
    uint32_t current_ma;            // Current draw in milliamps. (if available)
    uint32_t temperature_c;         // Temperature in Celsius. (if available)
    uint32_t free_heap_bytes;       // Available heap memory.
    uint32_t used_heap_bytes;       // Used heap memory.
    uint8_t cpu_usage_percent;      // Overall CPU usage percentage.
    uint8_t core0_usage_percent;    // Core 0 usage percentage.
    uint8_t core1_usage_percent;    // Core 1 usage percentage.
} system_stats_t;

/**
 * @brief Task timing statistics.
 */
typedef struct {
    uint32_t task_id;
    char task_name[TASK_NAME_LEN];
    uint32_t desired_period_us;     // Desired execution period.
    uint32_t actual_period_us;      // Actual measured period.
    uint32_t min_period_us;         // Minimum observed period.
    uint32_t max_period_us;         // Maximum observed period.
    uint32_t avg_execution_us;      // Average execution time.
    uint32_t max_execution_us;      // Maximum execution time.
    uint32_t deadline_misses;       // Number of deadline misses.
    uint32_t total_executions;      // Total number of executions.
    float jitter_percent;           // Period jitter percentage.
} task_timing_stats_t;

/** @} */ // end of shell_struct group

/**
 * @defgroup stats_api Stats Application Programming Interface
 * @{
 */

/**
 * @brief Run a benchmark to test FPU performance.
 * 
 * @return Benchmark execution time in microseconds.
 */
uint32_t stats_benchmark_fpu(void);

/**
 * @brief Notify the stats module of a buffer swap.
 * @param buffer_id Buffer registration ID.
 * @return true on success, false on failure.
 */
bool stats_buffer_swapped(int buffer_id);

/**
 * @brief Enable/disable automatic statistics collection.
 * @param enabled true to enable, false to disable.
 */
void stats_enable_collection(bool enabled);

/**
 * @brief Get all registered buffers.
 * @param buffers Array to store buffer info.
 * @param max_buffers Maximum number of buffers to retrieve.
 * @return Number of buffers retrieved.
 */
int stats_get_all_buffers(buffer_registration_t *buffers, int max_buffers);

/**
 * @brief Get all buffers with specified ID.
 *
 * @param buffer_info Pointer to program buffer information.
 * @param max_buffers maximum number of buffers to read.
 * @return Number of bytes retrieved.
 */
int stats_get_all_buffers_with_id(buffer_info_with_id_t *buffer_info, int max_buffers);

/**
 * @brief Get all task timing statistics.
 * @param stats Array to store timing stats.
 * @param max_tasks Maximum number of tasks to retrieve.
 * @return Number of tasks retrieved.
 */
int stats_get_all_task_timing(task_timing_stats_t *stats, int max_tasks);

/**
 * @brief Get buffer statistics.
 * @param buffer_id Buffer registration ID.
 * @param reg Output structure for buffer info.
 * @return true on success, false on failure.
 */
bool stats_get_buffer_info(int buffer_id, buffer_registration_t *reg);

/**
 * @brief Collect all cache and FPU statistics.
 * 
 * @param stats Pointer to structure to store statistics.
 */
void stats_get_hwstats(hwstats_stats_t* stats);

/**
 * @brief Get current optimization state.
 * @return Bitmask of active optimizations.
 */
optimization_state_t stats_get_optimizations(void);

/**
 * @brief Get optimization suggestions based on current stats.
 * @param suggestions Array to store suggestions.
 * @param max_suggestions Maximum number of suggestions to retrieve.
 * @return Number of suggestions provided.
 */
int stats_get_optimization_suggestions(optimization_suggestion_t *suggestions, int max_suggestions);

/**
 * @brief Get current system statistics.
 * @param stats Output structure for system stats.
 * @return true on success, false on failure.
 */
bool stats_get_system(system_stats_t *stats);

/**
 * @brief Get task timing statistics.
 * @param task_id ID of the task to query.
 * @param stats Output structure for task stats.
 * @return true on success, false on failure.
 */
bool stats_get_task_timing(uint32_t task_id, task_timing_stats_t *stats);

/**
 * @brief Initialize the statistics module.
 * @return true on success, false on failure.
 */
bool stats_init(void);

/**
 * @brief Check if data cache is enabled.
 * 
 * @return true if data cache is enabled.
 * @return false if data cache is disabled.
 */
bool stats_is_dcache_enabled(void);

/**
 * @brief Check if FPU is enabled.
 * 
 * @return true if FPU is enabled.
 * @return false if FPU is disabled.
 */
bool stats_is_fpu_enabled(void);

/**
 * @brief Check if instruction cache is enabled.
 * 
 * @return true if instruction cache is enabled.
 * @return false if instruction cache is disabled.
 */
bool stats_is_icache_enabled(void);

/**
 * @brief Get a string representation of optimization state.
 * @param opt Optimization state.
 * @return String description.
 */
const char* stats_optimization_to_string(optimization_state_t opt);

/**
 * @brief Set optimization state.
 * @param opt Optimization to enable/disable.
 * @param enabled true to enable, false to disable.
 * @return true on success, false on failure.
 */
bool stats_set_optimization(optimization_state_t opt, bool enabled);

/**
 * @brief Register a double buffer for monitoring.
 * @param name Buffer name.
 * @param buffer_a First buffer.
 * @param buffer_b Second buffer.
 * @param size Size of each buffer.
 * @param active_buffer Pointer to the active buffer pointer.
 * @return Buffer registration ID on success, -1 on failure.
 */
int stats_register_buffer(const char *name, void *buffer_a, void *buffer_b, 
    size_t size, volatile void **active_buffer);

/**
 * @brief Reset all statistics.
 */
void stats_reset(void);

/**
 * @brief Reset task timing statistics.
 * @param task_id Task ID (or -1 for all tasks).
 */
void stats_reset_task_timing(int task_id);

/**
 * @brief Update task timing statistics.
 * @param task_id Task ID.
 * @param execution_time_us Execution time in microseconds.
 * @return true on success, false on failure.
 */
bool stats_update_task_timing(uint32_t task_id, uint32_t execution_time_us);

/** @} */ // end of stats_api group

/**
 * @defgroup stats_cmd Stats Command Handlers
 * @{
 */

/**
 * @brief Handler function for the 'stats_reset' shell command.
 * 
 * This function handles the 'stats_reset' shell command which allows
 * the user to manually reset statistics.
 * 
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_buffers(int argc, char *argv[]);

/**
 * @brief Handler function for the 'hw_stats' shell command.
 * 
 * This function handles the 'hw_stats' shell command which displays
 * information about cache and FPU status.
 * 
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_hwstats(int argc, char *argv[]);

/**
 * @brief Handler function for the 'optimizations' shell command.
 * 
 * This function handles the 'optimizations' shell command which displays
 * information about optimizations currently available in the build.
 * 
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_optimizations(int argc, char *argv[]);

/**
 * @brief Handler function for the 'stats_reset' shell command.
 * 
 * This function handles the 'stats_reset' shell command which allows
 * the user to manually reset statistics.
 * 
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_stats_reset(int argc, char *argv[]);

/**
 * @brief Handler function for the 'system_stats' shell command.
 * 
 * This function handles the 'system_stats' shell command which displays
 * information about the system.
 * 
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_system_stats(int argc, char *argv[]);

/**
 * @brief Handler function for the 'task_stats' shell command.
 * 
 * This function handles the 'task_stats' shell command which can
 * be used to view task performance statistics.
 * 
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_task_stats(int argc, char *argv[]);

/**
 * @brief Command registration for the stats module.
 * 
 * This function registers all stats commands within the stats
 * module for shell use.
 */
void register_stats_commands(void);

/** @} */ // end of stats_cmd group

#ifdef __cplusplus
}
#endif

#endif /* _STATS_H_ */