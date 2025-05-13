/**
* @file scheduler_shell_commands.h
* @brief Shell commands for controlling the task scheduler
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [Current Date]
* @version 1.0
* 
* This module provides shell commands to control and monitor the
* multi-core task scheduler. It integrates the scheduler functionality
* with the USB shell interface.
* 
* @section commands Available Commands
* - scheduler: Start/stop/status of scheduler
* - task: Create and manage tasks
* - ps: List all tasks
* - stats: Show scheduler statistics
* - trace: Enable/disable tracing
*/

#ifndef SCHEDULER_SHELL_COMMANDS_H
#define SCHEDULER_SHELL_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_shell.h"

/**
 * @defgroup scheduler_commands Scheduler Shell Commands
 * @{
 */

/**
 * @brief Scheduler control command
 * 
 * Controls the scheduler state and displays status information.
 * 
 * Usage: scheduler <start|stop|status>
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, 1 on error
 * 
 * @code
 * scheduler start    //Start the scheduler
 * scheduler stop     //Stop the scheduler
 * scheduler status   //Display scheduler status
 * @endcode
 */
int cmd_scheduler(int argc, char *argv[]);

/**
 * @brief Task management command
 * 
 * Creates test tasks with specified parameters.
 * 
 * Usage: task create <n> <priority> <core>
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, 1 on error
 * 
 * @code
 * task create 1 2 0    //Create task 1, priority 2, core 0
 * task create 2 3 -1   //Create task 2, priority 3, any core
 * @endcode
 */
int cmd_task(int argc, char *argv[]);

/**
 * @brief List all tasks command
 * 
 * Displays a table of all tasks with their current state,
 * priority, core affinity, and execution statistics.
 * 
 * Usage: ps
 * 
 * @param argc Argument count (unused)
 * @param argv Argument array (unused)
 * @return Always returns 0
 */
int cmd_ps(int argc, char *argv[]);

/**
 * @brief Show scheduler statistics
 * 
 * Displays detailed scheduler performance metrics including
 * context switches, runtime, and task counts.
 * 
 * Usage: stats
 * 
 * @param argc Argument count (unused)
 * @param argv Argument array (unused)
 * @return 0 on success, 1 on error
 */
int cmd_stats(int argc, char *argv[]);

/**
 * @brief Control scheduler tracing
 * 
 * Enables or disables verbose debug output from the scheduler.
 * Useful for debugging scheduling issues.
 * 
 * Usage: trace <on|off>
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, 1 on error
 * 
 * @code
 * trace on     //Enable debug output
 * trace off    //Disable debug output
 * @endcode
 */
int cmd_trace(int argc, char *argv[]);

/**
 * @brief Test task function for demonstrations
 * 
 * A sample task that runs for a specified number of iterations,
 * printing progress and demonstrating task switching.
 * 
 * @param params Task number as integer pointer
 * 
 * @note This task is created by the 'task create' command
 */
void test_task(void *params);

/**
 * @brief Register scheduler commands with the shell
 * 
 * Registers all scheduler-related commands with the USB shell.
 * Must be called after shell_init() but before entering main loop.
 * 
 * @pre Shell must be initialized
 * @post All scheduler commands are available in the shell
 * 
 * @code
 * shell_init();
 * register_scheduler_commands();
 * @endcode
 */
void register_scheduler_commands(void);

/** @} */ //end of scheduler_commands

#ifdef __cplusplus
}
#endif

#endif //SCHEDULER_SHELL_COMMANDS_H