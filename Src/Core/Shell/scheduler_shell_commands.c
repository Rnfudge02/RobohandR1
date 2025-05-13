/**
* @file scheduler_shell_commands_fixed.c
* @brief Fixed shell command implementations for scheduler control
*/

#include "scheduler_shell_commands.h"
#include "scheduler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/time.h"

//External reference to test task
extern void test_task(void *params);

//Scheduler command definitions
static const shell_command_t scheduler_commands[] = {
    {"scheduler", "Control the scheduler (start|stop|status)", cmd_scheduler},
    {"task", "Create a test task (create <n> <priority> <core>)", cmd_task},
    {"ps", "List all tasks", cmd_ps},
    {"stats", "Show scheduler statistics", cmd_stats},
    {"trace", "Enable/disable scheduler tracing (on|off)", cmd_trace},
};

//Scheduler control command
int cmd_scheduler(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: scheduler <start|stop|status>\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "start") == 0) {
        printf("Starting scheduler...\n\r");
        if (scheduler_start()) {
            printf("Scheduler started successfully\n\r");
        } else {
            printf("Failed to start scheduler\n\r");
        }
    } 
    else if (strcmp(argv[1], "stop") == 0) {
        printf("Stopping scheduler...\n\r");
        scheduler_stop();
        printf("Scheduler stopped\n\r");
    } 
    else if (strcmp(argv[1], "status") == 0) {
        scheduler_stats_t stats;
        if (scheduler_get_stats(&stats)) {
            printf("Scheduler Status:\n\r");
            //Check if scheduler is actually running by looking at runtime
            bool running = (stats.total_runtime > 0) || (stats.context_switches > 0);
            printf("  Running: %s\n\r", running ? "Yes" : "No");
            printf("  Context switches: %lu\n\r", stats.context_switches);
            printf("  Tasks created: %lu\n\r", stats.task_creates);
            printf("  Core 0 switches: %lu\n\r", stats.core0_switches);
            printf("  Core 1 switches: %lu\n\r", stats.core1_switches);
            if (running) {
                printf("  Runtime: %llu us\n\r", stats.total_runtime);
            }
        } else {
            printf("Failed to get scheduler status\n\r");
        }
    } 
    else {
        printf("Unknown scheduler command: %s\n\r", argv[1]);
        return 1;
    }
    
    return 0;
}

//Task creation command
int cmd_task(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: task create <n> <priority> <core> [type]\n\r");
        printf("  n: task number\n\r");
        printf("  priority: 0-4 (idle-critical)\n\r");
        printf("  core: 0, 1, or -1 (any)\n\r");
        printf("  type: oneshot or persistent (default: oneshot)\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "create") != 0) {
        printf("Unknown task command: %s\n\r", argv[1]);
        return 1;
    }
    
    int task_num = atoi(argv[2]);
    int priority = atoi(argv[3]);
    int core = atoi(argv[4]);
    
    //Validate parameters
    if (priority < 0 || priority > 4) {
        printf("Invalid priority: %d (must be 0-4)\n\r", priority);
        return 1;
    }
    
    if (core < -1 || core > 1) {
        printf("Invalid core: %d (must be 0, 1, or -1)\n\r", core);
        return 1;
    }
    
    //Convert core value
    uint8_t core_affinity = (core == -1) ? 0xFF : (uint8_t)core;
    
    //Create task name
    char task_name[TASK_NAME_LEN];
    snprintf(task_name, sizeof(task_name), "test_%d", task_num);
    
    //Create the task
    task_type_t task_type = TASK_TYPE_ONESHOT;
    if (argc >= 6) {
        if (strcmp(argv[5], "persistent") == 0) {
            task_type = TASK_TYPE_PERSISTENT;
        }
    }
    
    //Create the task
    int task_id = scheduler_create_task(
        test_task,
        (void *)(intptr_t)task_num,
        0,
        (task_priority_t)priority,
        task_name,
        core_affinity,
        task_type
    );
    
    printf("Created %s task %s (ID: %d) with priority %d on core %s\n\r", task_type == TASK_TYPE_PERSISTENT ? "persistent" : "oneshot",
        task_name, task_id, priority, core == -1 ? "any" : (core == 0 ? "0" : "1"));

    return 0;
}

//List tasks command - fixed version
int cmd_ps(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("Task List:\n\r");
    printf("ID  | Name           | State    | Priority | Core | Run Count\n\r");
    printf("----+----------------+----------+----------+------+----------\n\r");
    
    //Check all possible task IDs (a bit inefficient but works)
    for (int id = 1; id < 100; id++) {
        task_control_block_t tcb;
        if (scheduler_get_task_info(id, &tcb)) {
            const char *state_str;
            switch (tcb.state) {
                case TASK_STATE_INACTIVE:  state_str = "INACTIVE"; break;
                case TASK_STATE_READY:     state_str = "READY"; break;
                case TASK_STATE_RUNNING:   state_str = "RUNNING"; break;
                case TASK_STATE_BLOCKED:   state_str = "BLOCKED"; break;
                case TASK_STATE_SUSPENDED: state_str = "SUSPENDED"; break;
                case TASK_STATE_COMPLETED: state_str = "COMPLETED"; break;  //<-- Added
                default:                   state_str = "UNKNOWN"; break;
            }
            
            printf("%-3lu | %-14s | %-8s | %-8d | %-4s | %lu\n\r",
                   tcb.task_id,
                   tcb.name,
                   state_str,
                   tcb.priority,
                   tcb.core_affinity == 0xFF ? "any" : 
                       (tcb.core_affinity == 0 ? "0" : "1"),
                   tcb.run_count);
        }
    }
    
    return 0;
}

//Show statistics command
int cmd_stats(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    scheduler_stats_t stats;
    
    if (!scheduler_get_stats(&stats)) {
        printf("Failed to get scheduler statistics\n\r");
        return 1;
    }
    
    printf("Scheduler Statistics:\n\r");
    printf("  Total context switches: %lu\n\r", stats.context_switches);
    printf("  Core 0 switches: %lu\n\r", stats.core0_switches);
    printf("  Core 1 switches: %lu\n\r", stats.core1_switches);
    printf("  Tasks created: %lu\n\r", stats.task_creates);
    printf("  Tasks deleted: %lu\n\r", stats.task_deletes);
    printf("  Total runtime: %llu us\n\r", stats.total_runtime);
    
    return 0;
}

//Trace control command
int cmd_trace(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: trace <on|off>\n\r");
        return 1;
    }
    
    if (strcmp(argv[1], "on") == 0) {
        scheduler_enable_tracing(true);
    } else if (strcmp(argv[1], "off") == 0) {
        scheduler_enable_tracing(false);
    } else {
        printf("Invalid option: %s (use 'on' or 'off')\n\r", argv[1]);
        return 1;
    }
    
    return 0;
}

//Register scheduler commands with the shell
void register_scheduler_commands(void) {
    for (int i = 0; i < sizeof(scheduler_commands) / sizeof(scheduler_commands[0]); i++) {
        shell_register_command(&scheduler_commands[i]);
    }
}