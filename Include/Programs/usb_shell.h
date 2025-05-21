/**
* @file usb_shell.h
* @brief USB Command Line Shell Interface for Raspberry Pi Pico 2.
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [2025-05-20]
* @version 1.0
* 
* This module provides a USB CDC-based command line interface with
* command parsing, argument handling, and extensible command registration.
* 
* @section features Features
* - USB CDC serial communication.
* - Command parsing with argument support.
* - Built-in commands. (help, clear, echo)
* - Extensible command registration.
* - Command history. (optional)
* - Line editing with backspace support.
* 
* @section usage Basic Usage
* @code
* // Initialize shell. NOSONAR - Code
* shell_init();
* 
* // Register custom commands. NOSONAR - Code
* shell_register_command(&my_command);
* 
* // Main loop NOSONAR - Code
* while(1) {
*     shell_task();
* }
* @endcode
*/

#ifndef USB_SHELL_H
#define USB_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup shell_constant Shell Configuration Constants
 * @{
 */

/** Maximum command line buffer size. */
#define SHELL_BUFFER_SIZE 256

/** Maximum number of arguments per command. */
#define SHELL_MAX_ARGS 16

/** Shell prompt string. */
#define SHELL_PROMPT "> "

/** @} */ // end of shell_constant group

/**
 * @defgroup shell_struct Shell Configuration Structures
 * @{
 */

 /**
 * @struct shell_context_t
 * @brief Shell internal context.
 * 
 * Maintains the state of the shell including input buffer,
 * parsed arguments, and configuration options.
 */
typedef struct {
    int argc;                         /**< Number of parsed arguments. */
    uint16_t buffer_pos;              /**< Current position in buffer. */
    bool echo_enabled;                /**< Whether to echo input characters. */
    char *argv[SHELL_MAX_ARGS];       /**< Parsed argument pointers. */
    char buffer[SHELL_BUFFER_SIZE];   /**< Command input buffer. */
} shell_context_t;

/**
 * @struct shell_command_t
 * @brief Shell command structure.
 * 
 * Defines a command with its name, help text, and handler function.
 * Commands are registered with the shell and matched against user input.
 */
typedef struct {
    int (*handler)(int argc, char *argv[]);             /**< Command handler function. */
    const char *command;                                /**< Command string that user types. */
    const char *help;                                   /**< Help text shown in help command. */
} shell_command_t;

/** @} */ // end of shell_struct group

/**
 * @defgroup shell_api Shell API Functions
 * @{
 */

/**
 * @brief Initialize the USB shell.
 * 
 * Sets up the USB CDC interface, initializes shell data structures,
 * and registers built-in commands. Waits for USB connection before returning.
 * 
 * @pre USB stdio must be initialized. (stdio_init_all())
 * @post Shell is ready to process commands.
 * 
 * @code
 * stdio_init_all();
 * shell_init();
 * @endcode
 */
void shell_init(void);

/**
 * @brief Register a command with the shell.
 * 
 * Adds a command to the shell's command table. The command will be
 * available immediately after registration.
 * 
 * @param cmd Pointer to command structure. (must remain valid)
 * @return true if registration successful, false if command table full.
 * 
 * @code
 * static const shell_command_t led_cmd = {
 *     "led",                    // Command. NOSONAR - Code
 *     "Control LED on/off",     // Help text. NOSONAR - Code
 *     cmd_led_handler          // Handler function. NOSONAR - Code
 * };
 * 
 * shell_register_command(&led_cmd);
 * @endcode
 */
bool shell_register_command(const shell_command_t *cmd);

/**
 * @brief Shell task - process input and execute commands.
 * 
 * This function should be called repeatedly in the main loop.
 * It handles character input, command parsing, and execution.
 * The function is non-blocking if no input is available.
 * 
 * @note Call this as frequently as possible for responsive shell.
 * 
 * @code
 * while (1) {
 *     shell_task();
 *     // Other tasks... NOSONAR - Code
 * }
 * @endcode
 */
void shell_task(void);

/** @} */ //end of shell_api

/**
 * @defgroup shell_builtins Built-in Command Handlers
 * @{
 */

/**
 * @brief Clear screen command handler
 * 
 * Clears the terminal screen using ANSI escape codes.
 * 
 * @param argc Argument count. (unused)
 * @param argv Argument array. (unused)
 * @return Always returns 0.
 */
int cmd_clear(int argc, char *argv[]);

/**
 * @brief Echo command handler
 * 
 * Echoes all arguments back to the console.
 * 
 * @param argc Argument count.
 * @param argv Argument array.
 * @return Always returns 0.
 * 
 * @code
 * // User types: echo Hello World NOSONAR - Code
 * // Output: Hello World NOSONAR - Code
 * @endcode
 */
int cmd_echo(int argc, char *argv[]);

/**
 * @brief Help command handler.
 * 
 * Displays list of all registered commands with their help text.
 * 
 * @param argc Argument count. (unused)
 * @param argv Argument array. (unused)
 * @return Always returns 0.
 */
int cmd_help(int argc, char *argv[]);

/** @} */ //end of shell_builtins

#ifdef __cplusplus
}
#endif

#endif //USB_SHELL_H