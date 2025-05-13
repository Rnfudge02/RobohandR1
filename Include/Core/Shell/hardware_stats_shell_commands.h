/**
* @file hardware_stats_shell_commands.h
* @brief Shell command interface for cache and FPU status
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025
* 
* Header file for the shell command interface that provides
* command-line interaction with cache and FPU stats functionality.
*/

#ifndef HARDWARE_STATS_SHELL_COMMANDS_H
#define HARDWARE_STATS_SHELL_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_shell.h"

/**
 * @brief Handler function for the 'cachefpu' shell command
 * 
 * This function handles the 'cachefpu' shell command which displays
 * information about cache and FPU status.
 * 
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return 0 on success, non-zero on error
 */
int cmd_cachefpu(int argc, char *argv[]);

/**
 * @brief Register cache and FPU commands with the shell
 * 
 * This function registers the cache and FPU related commands with the shell
 * system. It should be called during system initialization to make the
 * commands available.
 */
void register_cache_fpu_commands(void);

#ifdef __cplusplus
}
#endif

#endif // CACHE_FPU_SHELL_COMMANDS_H