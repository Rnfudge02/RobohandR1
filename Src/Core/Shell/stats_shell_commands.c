/**
* @file stats_shell_commands.c
* @brief Shell command implementations for statistics module
*/

#include "stats_shell_commands.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Stats command definitions
static const shell_command_t stats_commands[] = {
    {"sysstats", "Show system statistics", cmd_system_stats},
    {"taskstats", "Show task timing statistics", cmd_task_stats},
    {"opt", "Show/manage optimizations", cmd_optimizations},
    {"buffers", "Show registered buffers", cmd_buffers},
    {"statreset", "Reset statistics", cmd_stats_reset},
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
    printf("ID  | Name           | Desired(us) | Actual(us) | Jitter(%) | Misses | Execs\n\r");
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
        printf("%-2d | %-14s | %-7lu | %-5lu | %llu us\n\r",
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

void register_stats_commands(void) {
    for (int i = 0; i < sizeof(stats_commands) / sizeof(stats_commands[0]); i++) {
        shell_register_command(&stats_commands[i]);
    }
}