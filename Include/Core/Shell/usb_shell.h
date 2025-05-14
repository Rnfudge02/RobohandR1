/**
* @file usb_shell.h
* @brief USB Command Line Shell Interface for Raspberry Pi Pico
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [Current Date]
* @version 1.0
* 
* This module provides a USB CDC-based command line interface with
* command parsing, argument handling, and extensible command registration.
* 
* @section features Features
* - USB CDC serial communication
* - Command parsing with argument support
* - Built-in commands (help, clear, echo)
* - Extensible command registration
* - Command history (optional)
* - Line editing with backspace support
* 
* @section usage Basic Usage
* @code
* //Initialize shell
* shell_init();
* 
* //Register custom commands
* shell_register_command(&my_command);
* 
* //Main loop
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
 * @defgroup shell_constants Shell Configuration Constants
 * @{
 */

/** Maximum command line buffer size */
#define SHELL_BUFFER_SIZE 256

/** Maximum number of arguments per command */
#define SHELL_MAX_ARGS 16

/** Shell prompt string */
#define SHELL_PROMPT "> "

/** @} */

/**
 * @struct shell_command_t
 * @brief Shell command structure
 * 
 * Defines a command with its name, help text, and handler function.
 * Commands are registered with the shell and matched against user input.
 */
typedef struct {
    const char *command;              /**< Command string that user types */
    const char *help;                 /**< Help text shown in help command */
    int (*handler)(int argc, char *argv[]); /**< Command handler function */
} shell_command_t;

/**
 * @struct shell_context_t
 * @brief Shell internal context
 * 
 * Maintains the state of the shell including input buffer,
 * parsed arguments, and configuration options.
 */
typedef struct {
    char buffer[SHELL_BUFFER_SIZE];   /**< Command input buffer */
    uint16_t buffer_pos;              /**< Current position in buffer */
    char *argv[SHELL_MAX_ARGS];       /**< Parsed argument pointers */
    int argc;                         /**< Number of parsed arguments */
    bool echo_enabled;                /**< Whether to echo input characters */
} shell_context_t;

/**
 * @defgroup shell_api Shell API Functions
 * @{
 */

/**
 * @brief Initialize the USB shell
 * 
 * Sets up the USB CDC interface, initializes shell data structures,
 * and registers built-in commands. Waits for USB connection before returning.
 * 
 * @pre USB stdio must be initialized (stdio_init_all())
 * @post Shell is ready to process commands
 * 
 * @code
 * stdio_init_all();
 * shell_init();
 * @endcode
 */
void shell_init(void);

/**
 * @brief Shell task - process input and execute commands
 * 
 * This function should be called repeatedly in the main loop.
 * It handles character input, command parsing, and execution.
 * The function is non-blocking if no input is available.
 * 
 * @note Call this as frequently as possible for responsive shell
 * 
 * @code
 * while (1) {
 *     shell_task();
 *     //Other tasks...
 * }
 * @endcode
 */
void shell_task(void);

/**
 * @brief Register a command with the shell
 * 
 * Adds a command to the shell's command table. The command will be
 * available immediately after registration.
 * 
 * @param cmd Pointer to command structure (must remain valid)
 * @return true if registration successful, false if command table full
 * 
 * @code
 * static const shell_command_t led_cmd = {
 *     "led",                    //Command
 *     "Control LED on/off",     //Help text
 *     cmd_led_handler          //Handler function
 * };
 * 
 * shell_register_command(&led_cmd);
 * @endcode
 */
bool shell_register_command(const shell_command_t *cmd);

/**
 * @defgroup shell_builtins Built-in Command Handlers
 * @{
 */

/**
 * @brief Help command handler
 * 
 * Displays list of all registered commands with their help text.
 * 
 * @param argc Argument count (unused)
 * @param argv Argument array (unused)
 * @return Always returns 0
 */
int cmd_help(int argc, char *argv[]);

/**
 * @brief Clear screen command handler
 * 
 * Clears the terminal screen using ANSI escape codes.
 * 
 * @param argc Argument count (unused)
 * @param argv Argument array (unused)
 * @return Always returns 0
 */
int cmd_clear(int argc, char *argv[]);

/**
 * @brief Echo command handler
 * 
 * Echoes all arguments back to the console.
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return Always returns 0
 * 
 * @code
 * //User types: echo Hello World
 * //Output: Hello World
 * @endcode
 */
int cmd_echo(int argc, char *argv[]);

/** @} */ //end of shell_builtins

/** @} */ //end of shell_api

#ifdef __cplusplus
}
#endif

#endif //USB_SHELL_H