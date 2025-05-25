/**
* @file stats.c
* @brief System statistics module implementation
*/

#include "stats.h"

#include "log_manager.h"
#include "scheduler.h"
#include "spinlock_manager.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"

#include "hardware/structs/scb.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/vreg.h"
#include "usb_shell.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Define cache control mask constants if not defined by SDK
#ifndef SCB_CCR_IC_Msk
#define SCB_CCR_IC_Msk (1 << 17)  // I-Cache enable bit in CCR

#endif

#ifndef SCB_CCR_DC_Msk
#define SCB_CCR_DC_Msk (1 << 16)  // D-Cache enable bit in CCR

#endif

// Private data structures
static struct {
    uint64_t system_start_time_us;
    uint64_t last_update_time_us;
    uint32_t stats_lock_num;
    system_stats_t system;
    
    optimization_state_t active_optimizations;

    task_timing_stats_t task_timing[MAX_TASK_STATS];
    buffer_registration_t buffers[MAX_REGISTERED_BUFFERS];
    
    bool collection_enabled;
    
} stats_data;

// Private function declarations
static void update_system_stats(void);
static void analyze_optimizations(optimization_suggestion_t *suggestions, int max_suggestions, int *count);
static bool is_task_registered(uint32_t task_id);
static int find_task_slot(uint32_t task_id);
static int find_or_create_task_slot(uint32_t task_id);
static void update_period_stats(task_timing_stats_t *timing, uint32_t actual_period);

bool stats_init(void) {
    memset(&stats_data, 0, sizeof(stats_data));
    
    // Get the spin lock instance from the lock number
    stats_data.stats_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_DEBUG, "stats");
    
    stats_data.system_start_time_us = time_us_64();
    stats_data.collection_enabled = true;
    
    // Initialize ADC for voltage/temperature monitoring
    adc_init();
    adc_set_temp_sensor_enabled(true);
    
    // Set default optimizations based on current system state
    stats_data.active_optimizations = OPT_NONE;
    
    // Check if multicore is enabled
    scheduler_stats_t sched_stats;
    if (scheduler_get_stats(&sched_stats)&& sched_stats.core1_switches > 0) {
        stats_data.active_optimizations |= OPT_MULTICORE_ENABLED;
    }
    
    return true;
}

bool stats_get_system(system_stats_t *stats) {
    if (!stats) return false;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    // Update system stats
    update_system_stats();
    
    // Copy to output
    *stats = stats_data.system;
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return true;
}

static void update_system_stats(void) {
    uint64_t current_time = time_us_64();
    
    // Update system frequency
    stats_data.system.system_freq_hz = clock_get_hz(clk_sys);
    
    // Update uptime
    stats_data.system.uptime_us = current_time - stats_data.system_start_time_us;
    
    // Read temperature
    adc_select_input(4); // Temperature sensor is on ADC4
    uint16_t raw = adc_read();
    float voltage = raw * 3.3f / (1 << 12);
    stats_data.system.temperature_c = (uint32_t) (27 - (voltage - 0.706f) / 0.001721f);
    
    // Estimate voltage (placeholder - actual implementation would need hardware support)
    stats_data.system.voltage_mv = 3300; // Default 3.3V
    
    // Current measurement would require external hardware
    stats_data.system.current_ma = 0; // Not available
    
    // Calculate CPU usage based on scheduler stats
    scheduler_stats_t sched_stats;
    if (scheduler_get_stats(&sched_stats)) {
    uint64_t period_us = current_time - stats_data.last_update_time_us;
    if (period_us > 0) {
        // Use context switches to estimate actual usage
        uint32_t total_switches = sched_stats.core0_switches + sched_stats.core1_switches;
        if (total_switches > 0) {
            stats_data.system.cpu_usage_percent = 
                (uint8_t) (((total_switches * 100) / (period_us / 1000)) & 0xFF);  // Normalize to percent
            
            // Calculate per-core usage
            stats_data.system.core0_usage_percent = 
                (uint8_t) (((sched_stats.core0_switches * 100) / total_switches) & 0xFF);
            stats_data.system.core1_usage_percent = 
                (uint8_t) (((sched_stats.core1_switches * 100) / total_switches) & 0xFF);
        }
    }
}
    
    stats_data.last_update_time_us = current_time;
}

bool stats_get_task_timing(uint32_t task_id, task_timing_stats_t *stats) {
    if (!stats) return false;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    int slot = find_task_slot(task_id);
    if (slot < 0) {
        hw_spinlock_release(stats_data.stats_lock_num, save);
        return false;
    }
    
    *stats = stats_data.task_timing[slot];
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return true;
}

int stats_get_all_task_timing(task_timing_stats_t *stats, int max_tasks) {
    if (!stats || max_tasks <= 0) return 0;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    int count = 0;
    
    for (int i = 0; i < MAX_TASK_STATS; i++) {
        // Break if we've filled the output array
        if (count >= max_tasks) {
            break;
        }
    
        // Check for valid task
        if (stats_data.task_timing[i].task_id != 0) {
            // Assign first, then increment separately
            stats[count] = stats_data.task_timing[i];
            count++;
        }
    }
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return count;
}

bool stats_update_task_timing(uint32_t task_id, uint32_t execution_time_us) {
    // Early return if stats collection is disabled
    if (!stats_data.collection_enabled) return false;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    // Find or create a slot for this task
    int slot = find_or_create_task_slot(task_id);
    if (slot < 0) {
        hw_spinlock_release(stats_data.stats_lock_num, save);
        return false;
    }
    
    // Get timing stats struct and current time
    task_timing_stats_t *timing = &stats_data.task_timing[slot];
    uint64_t current_time = time_us_64();
    
    // Update execution count and max
    timing->total_executions++;
    if (timing->max_execution_us < execution_time_us) {
        timing->max_execution_us = execution_time_us;
    }
    
    // Update average execution time
    timing->avg_execution_us = ((timing->avg_execution_us * (timing->total_executions - 1)) + 
                              execution_time_us) / timing->total_executions;
    
    // Track execution times for period calculation
    static uint64_t last_execution_time[MAX_TASK_STATS];
    
    // Calculate period if we have previous execution time
    if (last_execution_time[slot] > 0) {
        uint32_t actual_period = (uint32_t) (current_time - last_execution_time[slot]);
        timing->actual_period_us = actual_period;
        
        // Update period statistics (extracted to helper function)
        update_period_stats(timing, actual_period);
    }
    
    // Remember current time for next period calculation
    last_execution_time[slot] = current_time;
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return true;
}

optimization_state_t stats_get_optimizations(void) {
    return stats_data.active_optimizations;
}

bool stats_set_optimization(optimization_state_t opt, bool enabled) {
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    if (enabled) {
        stats_data.active_optimizations |= opt;
    } else {
        stats_data.active_optimizations &= ~opt;
    }
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return true;
}

int stats_get_optimization_suggestions(optimization_suggestion_t *suggestions, int max_suggestions) {
    if (!suggestions || max_suggestions <= 0) return 0;
    
    int count = 0;
    analyze_optimizations(suggestions, max_suggestions, &count);
    
    return count;
}

// Add suggestion and check if we've reached the maximum
static inline bool add_suggestion(
    optimization_suggestion_t *suggestions,
    int *count,
    int max_suggestions,
    optimization_state_t opt_type,
    const char *description,
    int priority,
    float improvement
) {
    if (*count >= max_suggestions) {
        return false;
    }
    
    suggestions[*count] = (optimization_suggestion_t){
        .optimization = opt_type,
        .description = description,
        .priority = (uint8_t) (priority & 0xFF),
        .expected_improvement_percent = improvement
    };
    
    (*count)++;
    return true;
}

// Check if we have high-throughput buffers
static bool has_high_throughput_buffers(void) {
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (stats_data.buffers[i].is_registered && 
            stats_data.buffers[i].swap_count > 100) {
            return true;
        }
    }
    return false;
}

// Count single-buffered items
static int count_single_buffers(void) {
    int count = 0;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (stats_data.buffers[i].is_registered && 
            stats_data.buffers[i].buffer_b == NULL) {
            count++;
        }
    }
    return count;
}

static void analyze_optimizations(
    optimization_suggestion_t *suggestions, 
    int max_suggestions, 
    int *count
) {
    *count = 0;
    
    // Check frequency scaling opportunity
    if ((!(stats_data.active_optimizations & OPT_FREQUENCY_SCALING) && 
        stats_data.system.cpu_usage_percent > 80) && (!add_suggestion(suggestions, count, max_suggestions,
            OPT_FREQUENCY_SCALING,
            "Enable frequency scaling to boost performance",
            9, 15.0f))) {
        return;  // Reached max suggestions
    }
    
    // Check DMA opportunity
    if ((!(stats_data.active_optimizations & OPT_DMA_ENABLED) && 
        has_high_throughput_buffers()) && (!add_suggestion(suggestions, count, max_suggestions,
            OPT_DMA_ENABLED,
            "Enable DMA for high-throughput buffers",
            8, 20.0f))) {
        return;  // Reached max suggestions
    }
    
    // Check double buffering opportunity
    if ((!(stats_data.active_optimizations & OPT_DOUBLE_BUFFERING) && 
        count_single_buffers() > 0) && (!add_suggestion(suggestions, count, max_suggestions,
            OPT_DOUBLE_BUFFERING,
            "Enable double buffering for smoother data flow",
            7, 10.0f))) {
        return;  // Reached max suggestions
    }
}

int stats_register_buffer(const char *name, void *buffer_a, void *buffer_b, 
                         size_t size, volatile void **active_buffer) {
    if (!name || !buffer_a || !active_buffer || size == 0) return -1;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    // Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (!stats_data.buffers[i].is_registered) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        hw_spinlock_release(stats_data.stats_lock_num, save);
        return -1;
    }
    
    buffer_registration_t *reg = &stats_data.buffers[slot];
    reg->name = name;
    reg->buffer_a = buffer_a;
    reg->buffer_b = buffer_b;
    reg->buffer_size = size;
    reg->active_buffer = active_buffer;
    reg->swap_count = 0;
    reg->last_swap_time_us = time_us_64();
    reg->is_registered = true;
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return slot;
}

bool stats_buffer_swapped(int buffer_id) {
    if (buffer_id < 0 || buffer_id >= MAX_REGISTERED_BUFFERS) return false;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    buffer_registration_t *reg = &stats_data.buffers[buffer_id];
    if (!reg->is_registered) {
        hw_spinlock_release(stats_data.stats_lock_num, save);
        return false;
    }
    
    reg->swap_count++;
    reg->last_swap_time_us = time_us_64();
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return true;
}

const char* stats_optimization_to_string(optimization_state_t opt) {
    switch (opt) {
        case OPT_FREQUENCY_SCALING: return "Frequency Scaling";
        case OPT_VOLTAGE_SCALING: return "Voltage Scaling";
        case OPT_DMA_ENABLED: return "DMA Enabled";
        case OPT_CACHE_ENABLED: return "Cache Enabled";
        case OPT_MULTICORE_ENABLED: return "Multicore Enabled";
        case OPT_INTERRUPT_COALESCING: return "Interrupt Coalescing";
        case OPT_POWER_GATING: return "Power Gating";
        case OPT_DOUBLE_BUFFERING: return "Double Buffering";
        default: return "Unknown";
    }
}

static int find_task_slot(uint32_t task_id) {
    for (int i = 0; i < MAX_TASK_STATS; i++) {
        if (stats_data.task_timing[i].task_id == task_id) {
            return i;
        }
    }
    return -1;
}

// Helper function to find or create a task slot
static int find_or_create_task_slot(uint32_t task_id) {
    int slot = find_task_slot(task_id);
    if (slot >= 0) return slot;
    
    // Find empty slot
    for (int i = 0; i < MAX_TASK_STATS; i++) {
        if (stats_data.task_timing[i].task_id == 0) {
            // Initialize the new slot
            stats_data.task_timing[i].task_id = task_id;
            
            // Get task info
            task_control_block_t tcb;
            if (scheduler_get_task_info(task_id, &tcb)) {
                strncpy(stats_data.task_timing[i].task_name, tcb.name, TASK_NAME_LEN - 1);
            }
            
            return i;
        }
    }
    
    return -1; // No slot available
}

// Helper function to update period statistics
static void update_period_stats(task_timing_stats_t *timing, uint32_t actual_period) {
    // Update min/max period
    if (timing->min_period_us == 0 || timing->min_period_us > actual_period) {
        timing->min_period_us = actual_period;
    }
    
    if (timing->max_period_us < actual_period) {
        timing->max_period_us = actual_period;
    }
    
    // Only calculate jitter if desired period is set
    if (timing->desired_period_us == 0) return;
    
    // Calculate jitter
    float jitter = (float) abs((int) actual_period - (int) timing->desired_period_us) / 
                  (float) timing->desired_period_us * 100.0f;
    timing->jitter_percent = jitter;
    
    // Check for deadline miss (10% tolerance)
    if ((float) actual_period > ((float) timing->desired_period_us * 1.1f)) {
        timing->deadline_misses++;
    }
}

void stats_enable_collection(bool enabled) {
    stats_data.collection_enabled = enabled;
}

void stats_reset(void) {
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    // Reset system stats
    memset(&stats_data.system, 0, sizeof(system_stats_t));
    stats_data.system_start_time_us = time_us_64();
    
    // Reset task timing
    memset(stats_data.task_timing, 0, sizeof(stats_data.task_timing));
    
    // Keep buffer registrations but reset counts
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (stats_data.buffers[i].is_registered) {
            stats_data.buffers[i].swap_count = 0;
            stats_data.buffers[i].last_swap_time_us = time_us_64();
        }
    }
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
}

void stats_reset_task_timing(int task_id) {
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    if (task_id < 0) {
        // Reset all
        memset(stats_data.task_timing, 0, sizeof(stats_data.task_timing));
    } else {
        // Reset specific task
        int slot = find_task_slot(task_id);
        if (slot >= 0) {
            uint32_t id = stats_data.task_timing[slot].task_id;
            char name[TASK_NAME_LEN];
            strncpy(name, stats_data.task_timing[slot].task_name, TASK_NAME_LEN - 1);
            
            memset(&stats_data.task_timing[slot], 0, sizeof(task_timing_stats_t));
            
            stats_data.task_timing[slot].task_id = id;
            strncpy(stats_data.task_timing[slot].task_name, name, TASK_NAME_LEN - 1);
        }
    }
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
}

bool stats_get_buffer_info(int buffer_id, buffer_registration_t *reg) {
    if (!reg || buffer_id < 0 || buffer_id >= MAX_REGISTERED_BUFFERS) return false;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    if (!stats_data.buffers[buffer_id].is_registered) {
        hw_spinlock_release(stats_data.stats_lock_num, save);
        return false;
    }
    
    *reg = stats_data.buffers[buffer_id];
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return true;
}

int stats_get_all_buffers(buffer_registration_t *buffers, int max_buffers) {
    if (!buffers || max_buffers <= 0) return 0;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    int count = 0;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (count >= max_buffers) {
            break;
        }

        if (stats_data.buffers[i].is_registered) {
            buffers[count++] = stats_data.buffers[i];
        }
    }
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return count;
}

// Add this function to stats.c
int stats_get_all_buffers_with_id(buffer_info_with_id_t *buffer_info, int max_buffers) {
    if (!buffer_info || max_buffers <= 0) return 0;
    
    uint32_t save = hw_spinlock_acquire(stats_data.stats_lock_num, scheduler_get_current_task());
    
    int count = 0;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (count >= max_buffers) {
            break;
        }
        
        if (stats_data.buffers[i].is_registered) {
            buffer_info[count].id = i;
            buffer_info[count].info = stats_data.buffers[i];
            count++;
        }
    }
    
    hw_spinlock_release(stats_data.stats_lock_num, save);
    return count;
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
bool stats_is_fpu_enabled(void) {
    // Run two quick benchmarks - one with integer math, one with float
    uint32_t int_time;
    uint32_t float_time;
    volatile int int_result = 1;                // NOSONAR - Required to prevent compiler optimization in benchmark
    volatile float float_result = 1.0f;         // NOSONAR - Required to prevent compiler optimization in benchmark
    
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
bool stats_is_cache_enabled(void) {
    // Create a large array in RAM
    #define TEST_SIZE 4096
    static volatile uint8_t test_array[TEST_SIZE];
    uint32_t uncached_time;
    uint32_t cached_time;
    volatile uint32_t sum = 0;              // NOSONAR - Required to prevent compiler optimization in benchmark
    
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
    return (cached_time < uncached_time * 0.7);
}

/**
 * @brief Check if instruction cache is enabled on RP2350
 * 
 * @return true if instruction cache is enabled
 * @return false if instruction cache is disabled
 */
bool stats_is_icache_enabled(void) {
    return (scb_hw->ccr & SCB_CCR_IC_Msk) != 0;
}

/**
 * @brief Check if data cache is enabled on RP2350
 * 
 * @return true if data cache is enabled
 * @return false if data cache is disabled
 */
bool stats_is_dcache_enabled(void) {
    return (scb_hw->ccr & SCB_CCR_DC_Msk) != 0;
}

/**
 * @brief Run a benchmark to test FPU performance
 * 
 * @return Benchmark execution time in microseconds
 */
uint32_t stats_benchmark_fpu(void) {
    uint32_t start_time;
    uint32_t end_time;
    volatile float result = 1.0f;               // NOSONAR - Required to prevent compiler optimization in benchmark
    
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
        log_message(LOG_LEVEL_INFO, "HW Info", "Unexpected result: %f", result);
    }
    
    return end_time - start_time;
}

/**
 * @brief Collect all cache and FPU statistics
 * 
 * @param stats Pointer to structure to store statistics
 */
void stats_get_stats(hwstats_stats_t* stats) {
    // Check cache and FPU status
    stats->fpu_enabled = stats_is_fpu_enabled();
    stats->icache_enabled = stats_is_icache_enabled();
    stats->dcache_enabled = stats_is_dcache_enabled();
    
    // Run FPU benchmark
    stats->fpu_benchmark_time = stats_benchmark_fpu();
    
    // Get cache information
    stats->cache_levels = 1;
    stats->icache_line_size = 32;
    stats->dcache_line_size = 32;
}

// Stats command definitions
static const shell_command_t stats_commands[] = {
    {cmd_system_stats, "sys_stats", "Show system statistics"},
    {cmd_task_stats, "task_stats", "Show task timing statistics"},
    {cmd_hwstats, "hw_stats", "Hardware and Firmware Information."},
    {cmd_optimizations, "opt", "Show/manage optimizations"},
    {cmd_buffers, "buffers", "Show registered buffers"},
    {cmd_stats_reset, "statreset", "Reset statistics"}
};

int cmd_system_stats(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    system_stats_t stats;
    if (!stats_get_system(&stats)) {
        printf("Failed to get system statistics\n\r");
        return 1;
    }
    
    printf("System Statistics:\n\r");
    printf("------------------\n\r");
    printf("System Frequency: %lu Hz\n\r", stats.system_freq_hz);
    printf("Voltage: %lu mV\n\r", stats.voltage_mv);
    printf("Current: %lu mA\n\r", stats.current_ma);
    printf("Temperature: %lu C\n\r", stats.temperature_c);
    printf("Uptime: %llu us\n\r", stats.uptime_us);
    printf("CPU Usage: %u%%\n\r", stats.cpu_usage_percent);
    printf("Core 0 Usage: %u%%\n\r", stats.core0_usage_percent);  
    printf("Core 1 Usage: %u%%\n\r", stats.core1_usage_percent);
    
    return 0;
}

int cmd_task_stats(int argc, char *argv[]) {
    task_timing_stats_t stats[MAX_TASK_STATS];
    int count = stats_get_all_task_timing(stats, MAX_TASK_STATS);
    
    if (argc > 1 && strcmp(argv[1], "reset") == 0) {
        int task_id = -1;
        if (argc > 2) {
            task_id = atoi(argv[2]);
        }
        stats_reset_task_timing(task_id);
        printf("Task timing statistics reset\n\r");
        return 0;
    }
    
    printf("Task Timing Statistics:\n\r");
    printf("----------------------\n\r");
    printf("ID  | Name           | Desired(us) | Actual(us) | Jitter(%%) | Misses | Execs\n\r");
    printf("----+----------------+-------------+------------+-----------+--------+------\n\r");
    
    for (int i = 0; i < count; i++) {
        printf("%-3lu | %-14s | %-11lu | %-10lu | %-9.1f | %-6lu | %lu\n\r",
               stats[i].task_id,
               stats[i].task_name,
               stats[i].desired_period_us,
               stats[i].actual_period_us,
               stats[i].jitter_percent,
               stats[i].deadline_misses,
               stats[i].total_executions);
    }
    
    if (count == 0) {
        printf("No task timing data available\n\r");
    }
    
    return 0;
}

int cmd_optimizations(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "suggest") == 0) {
        optimization_suggestion_t suggestions[10];
        int count = stats_get_optimization_suggestions(suggestions, 10);
        
        printf("Optimization Suggestions:\n\r");
        printf("------------------------\n\r");
        
        if (count == 0) {
            printf("No optimization suggestions at this time\n\r");
        }
        
        for (int i = 0; i < count; i++) {
            printf("%d. %s\n\r", i+1, suggestions[i].description);
            printf("   Priority: %u/10\n\r", suggestions[i].priority);
            printf("   Expected Improvement: %.1f%%\n\r", suggestions[i].expected_improvement_percent);
            printf("\n\r");
        }
       
       return 0;
    }
   
    optimization_state_t opts = stats_get_optimizations();
   
    printf("Active Optimizations:\n\r");
    printf("--------------------\n\r");
   
    for (int i = 0; i < 8; i++) {
        optimization_state_t opt = 1 << i;
        bool active = (opts & opt) != 0;
        printf("%s: %s\n\r", stats_optimization_to_string(opt), active ? "Enabled" : "Disabled");
    }
   
    return 0;
}

int cmd_buffers(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    buffer_info_with_id_t buffer_info[MAX_REGISTERED_BUFFERS];
    int count = stats_get_all_buffers_with_id(buffer_info, MAX_REGISTERED_BUFFERS);
    
    printf("Registered Buffers:\n\r");
    printf("------------------\n\r");
    printf("ID | Name           | Size    | Swaps | Last Swap\n\r");
    printf("---+----------------+---------+-------+----------\n\r");
    
    for (int i = 0; i < count; i++) {
        printf("%-2d | %-14s | %-7d | %-5lu | %llu us\n\r",
               buffer_info[i].id,
               buffer_info[i].info.name,
               buffer_info[i].info.buffer_size,
               buffer_info[i].info.swap_count,
               buffer_info[i].info.last_swap_time_us);
    }
    
    if (count == 0) {
        printf("No buffers registered\n\r");
    }
    
    return 0;
}

int cmd_stats_reset(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "all") == 0) {
        stats_reset();
        printf("All statistics reset\n\r");
    } else if (argc > 1 && strcmp(argv[1], "tasks") == 0) {
        stats_reset_task_timing(-1);
        printf("Task timing statistics reset\n\r");
    } else {
        printf("Usage: statreset <all|tasks>\n\r");
        return 1;
    }
   
    return 0;
}

/**
 * @brief Print command usage information
 */
static void stats_print_usage(void) {
    log_message(LOG_LEVEL_INFO, "HW Stats", "Usage: hw_stats [command]");
    log_message(LOG_LEVEL_INFO, "HW Stats", "Commands:");
    log_message(LOG_LEVEL_INFO, "HW Stats", "  status       - Show basic cache and FPU status.");
    log_message(LOG_LEVEL_INFO, "HW Stats", "  detail       - Show detailed cache and processor information.");
    log_message(LOG_LEVEL_INFO, "HW Stats", "  benchmark    - Run FPU benchmark.");
    log_message(LOG_LEVEL_INFO, "HW Stats", "  monitor <n>  - Monitor cache and FPU status for n seconds.");
    log_message(LOG_LEVEL_INFO, "HW Stats", "If no command is given, 'status' is the default.");
}

/**
 * @brief Display basic cache and FPU status
 * 
 * @return 0 on success
 */
static int stats_status(void) {
    hwstats_stats_t stats;
    stats_get_stats(&stats);
    
    log_message(LOG_LEVEL_INFO, "HW Info", "RP2350 Cache/FPU Status:");
    log_message(LOG_LEVEL_INFO, "HW Info", "------------------------");
    log_message(LOG_LEVEL_INFO, "HW Info", "FPU: %s", stats.fpu_enabled ? "Enabled" : "Disabled");
    log_message(LOG_LEVEL_INFO, "HW Info", "Instruction Cache: %s", stats.icache_enabled ? "Enabled" : "Disabled");
    log_message(LOG_LEVEL_INFO, "HW Info", "Data Cache: %s", stats.dcache_enabled ? "Enabled" : "Disabled");
    
    return 0;
}

/**
 * @brief Display detailed cache and processor information
 * 
 * @return 0 on success
 */
static int stats_detail(void) {
    hwstats_stats_t stats;
    stats_get_stats(&stats);
    
    log_message(LOG_LEVEL_INFO, "HW Stats", "RP2350 Detailed Cache/FPU Information:");
    log_message(LOG_LEVEL_INFO, "HW Stats", "-------------------------------------");
    log_message(LOG_LEVEL_INFO, "HW Stats", "FPU: %s.", stats.fpu_enabled ? "Enabled" : "Disabled");
    log_message(LOG_LEVEL_INFO, "HW Stats", "Instruction Cache: %s.", stats.icache_enabled ? "Enabled" : "Disabled");
    log_message(LOG_LEVEL_INFO, "HW Stats", "Data Cache: %s.", stats.dcache_enabled ? "Enabled" : "Disabled");
    log_message(LOG_LEVEL_INFO, "HW Stats", "Cache Levels: %lu.", stats.cache_levels);
    log_message(LOG_LEVEL_INFO, "HW Stats", "Instruction Cache Line Size: %lu bytes.", stats.icache_line_size);
    log_message(LOG_LEVEL_INFO, "HW Stats", "Data Cache Line Size: %lu bytes.", stats.dcache_line_size);
    
    // Get processor details from compiler macros
    log_message(LOG_LEVEL_INFO, "HW Stats", "Processor Information:");
    log_message(LOG_LEVEL_INFO, "HW Stats", "---------------------");
    log_message(LOG_LEVEL_INFO, "HW Stats", "CPU: Cortex-M33.");
    
    // Print compiler optimization level
    #if defined(__OPTIMIZE_SIZE__)
        log_message(LOG_LEVEL_INFO, "HW Info", "Optimization: -Os (size)");
    #elif defined(__OPTIMIZE__)
        #if defined(__OPTIMIZE_LEVEL__) && __OPTIMIZE_LEVEL__ == 3
            log_message(LOG_LEVEL_INFO, "HW Info", "Optimization: -O3 (speed)");
        #elif defined(__OPTIMIZE_LEVEL__) && __OPTIMIZE_LEVEL__ == 2
            log_message(LOG_LEVEL_INFO, "HW Info", "Optimization: -O2");
        #elif defined(__OPTIMIZE_LEVEL__) && __OPTIMIZE_LEVEL__ == 1
            log_message(LOG_LEVEL_INFO, "HW Info", "Optimization: -O1");
        #else
            log_message(LOG_LEVEL_INFO, "HW Info", "Optimization: Enabled");
        #endif
    #else
        log_message(LOG_LEVEL_INFO, "HW Info", "Optimization: None");
    #endif
    
    // Print FPU ABI
    #if defined(__ARM_FP) && defined(__ARM_PCS_VFP)
        log_message(LOG_LEVEL_INFO, "HW Info", "FPU ABI: hard.");
    #elif defined(__ARM_FP)
        log_message(LOG_LEVEL_INFO, "HW Info", "FPU ABI: softfp");
    #else
        log_message(LOG_LEVEL_INFO, "HW Info", "FPU ABI: soft");
    #endif
    
    return 0;
}

/**
 * @brief Run FPU benchmark
 * 
 * @return 0 on success
 */
static int stats_benchmark(void) {
    log_message(LOG_LEVEL_INFO, "HW Stats", "Running FPU benchmark...");
    
    // Run the benchmark multiple times for better accuracy
    const int runs = 5;
    uint32_t times[runs];
    uint32_t total = 0;
    
    for (int i = 0; i < runs; i++) {
        times[i] = stats_benchmark_fpu();
        total += times[i];
        log_message(LOG_LEVEL_DEBUG, "HW Stats", "Run %d: %lu us.", i+1, times[i]);
    }
    
    log_message(LOG_LEVEL_INFO, "HW Stats", "Average execution time: %lu us.", total / runs);
    
    // Check if FPU is enabled
    if (!stats_is_fpu_enabled()) {
        log_message(LOG_LEVEL_WARN, "HW Stats", "FPU is currently disabled! Performance may be significantly improved by enabling the FPU.");
    }
    
    return 0;
}

/**
 * @brief Monitor cache and FPU status for a period of time
 * 
 * @param seconds Duration to monitor in seconds
 * @return 0 on success, -1 on error
 */
static int stats_monitor(int seconds) {
    if (seconds <= 0 || seconds > 60) {
        log_message(LOG_LEVEL_ERROR, "HW Stats", "Invalid duration. Please specify between 1 and 60 seconds.");
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "HW Stats", "Monitoring cache and FPU for %d seconds...", seconds);
    log_message(LOG_LEVEL_INFO, "HW Stats", "Press any key to stop.");
    
    log_message(LOG_LEVEL_INFO, "HW Stats", "Time(s)  FPU  I-Cache  D-Cache  Benchmark(us)");
    log_message(LOG_LEVEL_INFO, "HW Stats", "-------  ---  -------  -------  ------------");
    
    // Clear any pending input
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
        // Discard any character
    }
    
    for (int i = 0; i < seconds; i++) {
        hwstats_stats_t stats;
        stats_get_stats(&stats);
        
        log_message(LOG_LEVEL_DEBUG, "Enabled", "SEC: %7d FPU: %3s ICACHE: %3s DCACHE: %3s BENCH_TIME: %12lu.", 
            i,
            stats.fpu_enabled ? "Yes" : "No",
            stats.icache_enabled ? "Yes" : "No",
            stats.dcache_enabled ? "Yes" : "No",
            stats.fpu_benchmark_time);
        
        // Check for key press to exit early
        for (int j = 0; j < 10; j++) {  // Split into smaller intervals for better responsiveness
            if (getchar_timeout_us(100000) != PICO_ERROR_TIMEOUT) {  // 100ms
                log_message(LOG_LEVEL_WARN, "HW Stats", "Monitoring stopped by user.");
                return 0;
            }
        }
    }
    
    log_message(LOG_LEVEL_INFO, "HW Stats", "Monitoring complete.");
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
int cmd_hwstats(int argc, char *argv[]) {
    // If no command given, show status
    if (argc < 2) {
        return stats_status();
    }

    // Parse command
    if (strcmp(argv[1], "status") == 0) {
        return stats_status();
    }

    else if (strcmp(argv[1], "detail") == 0) {
        return stats_detail();
    }

    else if (strcmp(argv[1], "benchmark") == 0) {
        return stats_benchmark();
    }

    else if (strcmp(argv[1], "monitor") == 0) {
        int seconds = 10;  // Default duration
        if (argc > 2) {
            seconds = atoi(argv[2]);
        }
        return stats_monitor(seconds);
    }

    else if (strcmp(argv[1], "help") == 0) {
        stats_print_usage();
        return 0;
    }

    else {
        log_message(LOG_LEVEL_INFO, "HW Stats", "Unknown command: %s.", argv[1]);
        stats_print_usage();
        return -1;
    }
}

void register_stats_commands(void) {
    for (int i = 0; i < sizeof(stats_commands) / sizeof(stats_commands[0]); i++) {
        shell_register_command(&stats_commands[i]);
    }
}