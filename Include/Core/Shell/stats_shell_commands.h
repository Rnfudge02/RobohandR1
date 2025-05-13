/**
* @file stats_shell_commands.h
* @brief Shell commands for statistics module
*/

#ifndef _STATS_SHELL_COMMANDS_H_
#define _STATS_SHELL_COMMANDS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_shell.h"

// Command handlers
int cmd_system_stats(int argc, char *argv[]);
int cmd_task_stats(int argc, char *argv[]);
int cmd_optimizations(int argc, char *argv[]);
int cmd_buffers(int argc, char *argv[]);
int cmd_stats_reset(int argc, char *argv[]);

// Register all stats commands with the shell
void register_stats_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* _STATS_SHELL_COMMANDS_H_ */