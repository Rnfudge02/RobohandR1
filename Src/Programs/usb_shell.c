/**
* @file usb_shell.c
* @brief USB Shell implementation for Raspberry Pi Pico 2W
* @author [Robert Fudge (rnfudge@mun.ca)]
* @date [Current Date]
* 
* This file implements the USB shell functionality, including command
* parsing, execution, and built-in command handlers.
*/

#include "log_manager.h"
#include "usb_shell.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/**
 * @defgroup Shell_Private Private Shell Definitions
 * @{
 */

/** Maximum number of commands that can be registered */
#define MAX_COMMANDS 32

/** @} */

/**
 * @defgroup Shell_Private_Data Private Shell Data
 * @{
 */

/** Global shell context instance */
static shell_context_t shell_ctx;

/** Command table storing pointers to all registered commands */
static const shell_command_t *command_table[MAX_COMMANDS];

/** Current number of registered commands */
static uint8_t command_count = 0;

/** Built-in command definitions */
static const shell_command_t builtin_commands[] = {
    {cmd_help, "help", "Show available commands"},
    {cmd_clear, "clear", "Clear the screen"},
    {cmd_echo, "echo", "Echo arguments back to console"},
};

/** @} */

/**
 * @defgroup Shell_Private_Functions Private Shell Functions
 * @{
 */

/**
 * @brief Process a complete command line
 * 
 * Called when user presses Enter. Parses the command buffer and
 * executes the matching command if found.
 */
static void shell_process_command(void);

/**
 * @brief Parse the command buffer into arguments
 * 
 * Tokenizes the input buffer into separate arguments, handling
 * whitespace separation. Updates shell_ctx.argc and shell_ctx.argv.
 */
static void shell_parse_buffer(void);

/**
 * @brief Print the shell prompt
 * 
 * Outputs the shell prompt string to the console.
 */
static void shell_print_prompt(void);

/** @} */

void shell_init(void) {
    
    //Clear shell context
    memset(&shell_ctx, 0, sizeof(shell_ctx));
    shell_ctx.echo_enabled = true;
    
    //Register built-in commands
    for (int i = 0; i < sizeof(builtin_commands) / sizeof(builtin_commands[0]); i++) {
        shell_register_command(&builtin_commands[i]);
    }
    
    //Wait for USB connection
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    //Give time for USB to initialize
    sleep_ms(500);
    
    //Flush any pending log messages
    if (log_is_initialized()) {
        log_flush();
    }
    
    //Print welcome message
    printf("\n\rRaspberry Pi Pico 2W USB Shell\n\r");
    printf("Type 'help' for available commands\n\r");
}

void shell_task(void) {
    int c = getchar_timeout_us(0);
    
    if (c == PICO_ERROR_TIMEOUT) {
        return;
    }
    
    //Handle special characters
    switch (c) {
        case '\r':
        case '\n':
            if (shell_ctx.echo_enabled) {
                printf("\n\r");
            }
            shell_process_command();
            shell_print_prompt();
            break;
            
        case '\b':
        case 0x7F: //Delete key
            if (shell_ctx.buffer_pos > 0) {
                shell_ctx.buffer_pos--;
                shell_ctx.buffer[shell_ctx.buffer_pos] = '\0';
                if (shell_ctx.echo_enabled) {
                    printf("\b \b");
                }
            }
            break;
            
        case '\t':
            //Tab key - could implement command completion here
            break;
            
        default:
            if (shell_ctx.buffer_pos < SHELL_BUFFER_SIZE - 1) {
                shell_ctx.buffer[shell_ctx.buffer_pos++] = (char) (c & 0xFF);
                shell_ctx.buffer[shell_ctx.buffer_pos] = '\0';
                if (shell_ctx.echo_enabled && isprint(c)) {
                    putchar(c);
                }
            }
            break;
    }
}

bool shell_register_command(const shell_command_t *cmd) {
    if (command_count >= MAX_COMMANDS || cmd == NULL) {
        return false;
    }
    
    command_table[command_count++] = cmd;
    return true;
}

static void shell_process_command(void) {
    if (shell_ctx.buffer_pos == 0) {
        return;
    }
    
    //Parse the command buffer
    shell_parse_buffer();
    
    if (shell_ctx.argc == 0) {
        shell_ctx.buffer_pos = 0;
        shell_ctx.buffer[0] = '\0';
        return;
    }
    
    //Find and execute command
    bool found = false;
    for (int i = 0; i < command_count; i++) {
        if (strcmp(shell_ctx.argv[0], command_table[i]->command) == 0) {
            command_table[i]->handler(shell_ctx.argc, shell_ctx.argv);
            found = true;
            break;
        }
    }
    
    if (!found) {
        printf("Unknown command: %s\n\r", shell_ctx.argv[0]);
    }
    
    //Clear buffer
    shell_ctx.buffer_pos = 0;
    shell_ctx.buffer[0] = '\0';
}

static void shell_parse_buffer(void) {
    char *ptr = shell_ctx.buffer;
    shell_ctx.argc = 0;
    
    while (*ptr && shell_ctx.argc < SHELL_MAX_ARGS) {
        //Skip whitespace
        while (*ptr && isspace(*ptr)) {
            ptr++;
        }
        
        if (*ptr == '\0') {
            break;
        }
        
        //Mark start of argument
        shell_ctx.argv[shell_ctx.argc++] = ptr;
        
        //Find end of argument
        while (*ptr && !isspace(*ptr)) {
            ptr++;
        }
        
        if (*ptr) {
            *ptr++ = '\0';
        }
    }
}

static void shell_print_prompt(void) {
    printf(SHELL_PROMPT);
}

//Built-in command implementations
int cmd_help(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    printf("Available commands:\n\r");
    for (int i = 0; i < command_count; i++) {
        printf("  %-12s %s\n\r", command_table[i]->command, command_table[i]->help);
    }
    return 0;
}

int cmd_clear(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    printf("\033[2J\033[H");  //ANSI escape codes to clear screen
    return 0;
}

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            printf(" ");
        }
    }
    printf("\n\r");
    return 0;
}