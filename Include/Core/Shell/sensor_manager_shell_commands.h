/**
* @file sensor_manager_shell_commands.h
* @brief Shell commands for sensor manager
* @date 2025-05-13
*/

#ifndef SENSOR_MANAGER_SHELL_COMMANDS_H
#define SENSOR_MANAGER_SHELL_COMMANDS_H

#include "usb_shell.h"

/**
 * @brief Register all sensor manager commands with the shell
 */
void register_sensor_manager_commands(void);

/**
 * @brief Sensor manager command handler
 *
 * @param argc Argument count
 * @param argv Array of argument strings
 * @return 0 on success, non-zero on error
 */
int cmd_sensor(int argc, char *argv[]);

#endif // SENSOR_MANAGER_SHELL_COMMANDS_H