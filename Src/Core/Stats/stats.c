/**
* @file stats.c
* @brief System statistics module implementation
*/

#include "stats.h"
#include "scheduler.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Private data structures
static struct {
    system_stats_t system;
    task_timing_stats_t task_timing[MAX_TASK_STATS];
    buffer_registration_t buffers[MAX_REGISTERED_BUFFERS];
    optimization_state_t active_optimizations;
    uint64_t system_start_time_us;
    uint64_t last_update_time_us;
    bool collection_enabled;
    spin_lock_t* stats_lock;
} stats_data;

// Private function declarations
static void update_system_stats(void);
static void analyze_optimizations(optimization_suggestion_t *suggestions, int max_suggestions, int *count);
static bool is_task_registered(uint32_t task_id);
static int find_task_slot(uint32_t task_id);

bool stats_init(void) {
    memset(&stats_data, 0, sizeof(stats_data));
    
    // Claim a spin lock and get its instance
    uint lock_num = spin_lock_claim_unused(true);
    if (lock_num == -1) {
        return false;
    }
    
    // Get the spin lock instance from the lock number
    stats_data.stats_lock = spin_lock_instance(lock_num);
    
    stats_data.system_start_time_us = time_us_64();
    stats_data.collection_enabled = true;
    
    // Initialize ADC for voltage/temperature monitoring
    adc_init();
    adc_set_temp_sensor_enabled(true);
    
    // Set default optimizations based on current system state
    stats_data.active_optimizations = OPT_NONE;
    
    // Check if multicore is enabled
    scheduler_stats_t sched_stats;
    if (scheduler_get_stats(&sched_stats)) {
        if (sched_stats.core1_switches > 0) {
            stats_data.active_optimizations |= OPT_MULTICORE_ENABLED;
        }
    }
    
    return true;
}

bool stats_get_system(system_stats_t *stats) {
    if (!stats) return false;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    // Update system stats
    update_system_stats();
    
    // Copy to output
    *stats = stats_data.system;
    
    spin_unlock(stats_data.stats_lock, save);
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
    stats_data.system.temperature_c = 27 - (voltage - 0.706f) / 0.001721f;
    
    // Estimate voltage (placeholder - actual implementation would need hardware support)
    stats_data.system.voltage_mv = 3300; // Default 3.3V
    
    // Current measurement would require external hardware
    stats_data.system.current_ma = 0; // Not available
    
    // Calculate CPU usage based on scheduler stats
    scheduler_stats_t sched_stats;
    if (scheduler_get_stats(&sched_stats)) {
        uint64_t period_us = current_time - stats_data.last_update_time_us;
        if (period_us > 0) {
            // Estimate CPU usage based on context switches and timing
            stats_data.system.cpu_usage_percent = 50; // Placeholder
            stats_data.system.core0_usage_percent = 50; // Placeholder
            stats_data.system.core1_usage_percent = (sched_stats.core1_switches > 0) ? 30 : 0;
        }
    }
    
    stats_data.last_update_time_us = current_time;
}

bool stats_get_task_timing(uint32_t task_id, task_timing_stats_t *stats) {
    if (!stats) return false;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    int slot = find_task_slot(task_id);
    if (slot < 0) {
        spin_unlock(stats_data.stats_lock, save);
        return false;
    }
    
    *stats = stats_data.task_timing[slot];
    
    spin_unlock(stats_data.stats_lock, save);
    return true;
}

int stats_get_all_task_timing(task_timing_stats_t *stats, int max_tasks) {
    if (!stats || max_tasks <= 0) return 0;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    int count = 0;
    for (int i = 0; i < MAX_TASK_STATS && count < max_tasks; i++) {
        if (stats_data.task_timing[i].task_id != 0) {
            stats[count++] = stats_data.task_timing[i];
        }
    }
    
    spin_unlock(stats_data.stats_lock, save);
    return count;
}

bool stats_update_task_timing(uint32_t task_id, uint32_t execution_time_us) {
    if (!stats_data.collection_enabled) return false;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    int slot = find_task_slot(task_id);
    if (slot < 0) {
        // Find empty slot
        for (int i = 0; i < MAX_TASK_STATS; i++) {
            if (stats_data.task_timing[i].task_id == 0) {
                slot = i;
                stats_data.task_timing[i].task_id = task_id;
                
                // Get task info
                task_control_block_t tcb;
                if (scheduler_get_task_info(task_id, &tcb)) {
                    strncpy(stats_data.task_timing[i].task_name, tcb.name, TASK_NAME_LEN - 1);
                }
                break;
            }
        }
    }
    
    if (slot < 0) {
        spin_unlock(stats_data.stats_lock, save);
        return false;
    }
    
    task_timing_stats_t *timing = &stats_data.task_timing[slot];
    uint64_t current_time = time_us_64();
    
    // Update execution statistics
    timing->total_executions++;
    
    if (timing->max_execution_us < execution_time_us) {
        timing->max_execution_us = execution_time_us;
    }
    
    // Update average execution time
    timing->avg_execution_us = ((timing->avg_execution_us * (timing->total_executions - 1)) + 
                               execution_time_us) / timing->total_executions;
    
    // Update period statistics
    static uint64_t last_execution_time[MAX_TASK_STATS];
    if (last_execution_time[slot] > 0) {
        uint32_t actual_period = current_time - last_execution_time[slot];
        timing->actual_period_us = actual_period;
        
        if (timing->min_period_us == 0 || timing->min_period_us > actual_period) {
            timing->min_period_us = actual_period;
        }
        
        if (timing->max_period_us < actual_period) {
            timing->max_period_us = actual_period;
        }
        
        // Calculate jitter
        if (timing->desired_period_us > 0) {
            float jitter = (float)abs((int)actual_period - (int)timing->desired_period_us) / 
                          timing->desired_period_us * 100.0f;
            timing->jitter_percent = jitter;
            
            // Check for deadline miss
            if (actual_period > timing->desired_period_us * 1.1f) {
                timing->deadline_misses++;
            }
        }
    }
    
    last_execution_time[slot] = current_time;
    
    spin_unlock(stats_data.stats_lock, save);
    return true;
}

optimization_state_t stats_get_optimizations(void) {
    return stats_data.active_optimizations;
}

bool stats_set_optimization(optimization_state_t opt, bool enabled) {
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    if (enabled) {
        stats_data.active_optimizations |= opt;
    } else {
        stats_data.active_optimizations &= ~opt;
    }
    
    spin_unlock(stats_data.stats_lock, save);
    return true;
}

int stats_get_optimization_suggestions(optimization_suggestion_t *suggestions, int max_suggestions) {
    if (!suggestions || max_suggestions <= 0) return 0;
    
    int count = 0;
    analyze_optimizations(suggestions, max_suggestions, &count);
    
    return count;
}

static void analyze_optimizations(optimization_suggestion_t *suggestions, int max_suggestions, int *count) {
    *count = 0;
    
    // Check if frequency scaling could help
    if (!(stats_data.active_optimizations & OPT_FREQUENCY_SCALING)) {
        if (stats_data.system.cpu_usage_percent > 80) {
            suggestions[(*count)++] = (optimization_suggestion_t){
                .optimization = OPT_FREQUENCY_SCALING,
                .description = "Enable frequency scaling to boost performance",
                .priority = 9,
                .expected_improvement_percent = 15.0f
            };
        }
        if (*count >= max_suggestions) return;
    }
    
    // Check if DMA could help
    if (!(stats_data.active_optimizations & OPT_DMA_ENABLED)) {
        bool has_high_throughput = false;
        for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
            if (stats_data.buffers[i].is_registered && 
                stats_data.buffers[i].swap_count > 100) {
                has_high_throughput = true;
                break;
            }
        }
        
        if (has_high_throughput) {
            suggestions[(*count)++] = (optimization_suggestion_t){
                .optimization = OPT_DMA_ENABLED,
                .description = "Enable DMA for high-throughput buffers",
                .priority = 8,
                .expected_improvement_percent = 20.0f
            };
        }
        if (*count >= max_suggestions) return;
    }
    
    // Check if double buffering could help
    if (!(stats_data.active_optimizations & OPT_DOUBLE_BUFFERING)) {
        int single_buffers = 0;
        for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
            if (stats_data.buffers[i].is_registered && 
                stats_data.buffers[i].buffer_b == NULL) {
                single_buffers++;
            }
        }
        
        if (single_buffers > 0) {
            suggestions[(*count)++] = (optimization_suggestion_t){
                .optimization = OPT_DOUBLE_BUFFERING,
                .description = "Enable double buffering for smoother data flow",
                .priority = 7,
                .expected_improvement_percent = 10.0f
            };
        }
        if (*count >= max_suggestions) return;
    }
}

int stats_register_buffer(const char *name, void *buffer_a, void *buffer_b, 
                         size_t size, volatile void **active_buffer) {
    if (!name || !buffer_a || !active_buffer || size == 0) return -1;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    // Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS; i++) {
        if (!stats_data.buffers[i].is_registered) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        spin_unlock(stats_data.stats_lock, save);
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
    
    spin_unlock(stats_data.stats_lock, save);
    return slot;
}

bool stats_buffer_swapped(int buffer_id) {
    if (buffer_id < 0 || buffer_id >= MAX_REGISTERED_BUFFERS) return false;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    buffer_registration_t *reg = &stats_data.buffers[buffer_id];
    if (!reg->is_registered) {
        spin_unlock(stats_data.stats_lock, save);
        return false;
    }
    
    reg->swap_count++;
    reg->last_swap_time_us = time_us_64();
    
    spin_unlock(stats_data.stats_lock, save);
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

void stats_enable_collection(bool enabled) {
    stats_data.collection_enabled = enabled;
}

void stats_reset(void) {
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
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
    
    spin_unlock(stats_data.stats_lock, save);
}

void stats_reset_task_timing(int task_id) {
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
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
    
    spin_unlock(stats_data.stats_lock, save);
}

bool stats_get_buffer_info(int buffer_id, buffer_registration_t *reg) {
    if (!reg || buffer_id < 0 || buffer_id >= MAX_REGISTERED_BUFFERS) return false;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    if (!stats_data.buffers[buffer_id].is_registered) {
        spin_unlock(stats_data.stats_lock, save);
        return false;
    }
    
    *reg = stats_data.buffers[buffer_id];
    
    spin_unlock(stats_data.stats_lock, save);
    return true;
}

int stats_get_all_buffers(buffer_registration_t *buffers, int max_buffers) {
    if (!buffers || max_buffers <= 0) return 0;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    int count = 0;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS && count < max_buffers; i++) {
        if (stats_data.buffers[i].is_registered) {
            buffers[count++] = stats_data.buffers[i];
        }
    }
    
    spin_unlock(stats_data.stats_lock, save);
    return count;
}

// Add this function to stats.c
int stats_get_all_buffers_with_id(buffer_info_with_id_t *buffer_info, int max_buffers) {
    if (!buffer_info || max_buffers <= 0) return 0;
    
    uint32_t save = spin_lock_blocking(stats_data.stats_lock);
    
    int count = 0;
    for (int i = 0; i < MAX_REGISTERED_BUFFERS && count < max_buffers; i++) {
        if (stats_data.buffers[i].is_registered) {
            buffer_info[count].id = i;
            buffer_info[count].info = stats_data.buffers[i];
            count++;
        }
    }
    
    spin_unlock(stats_data.stats_lock, save);
    return count;
}