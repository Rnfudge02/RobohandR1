/**
 * @file log_manager.h
 * @brief Logging system with staged initialization
 */

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

/**
 * @defgroup log_enum Logging Enumerations
 * @{
 */

// Log levels
typedef enum {
    LOG_LEVEL_TRACE = 0,    // Verbose tracing of program execution
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,         // Informational messages
    LOG_LEVEL_WARN,         // Warning messages
    LOG_LEVEL_ERROR,        // Error messages
    LOG_LEVEL_FATAL         // Fatal errors
} log_level_t;

// Log destinations
typedef enum {
    LOG_DEST_CONSOLE = 0x01,  // Output to console (stdio)
    LOG_DEST_SDCARD  = 0x02,  // Output to SD card
    LOG_DEST_FLASH   = 0x04   // Output to flash memory
} log_destination_t;

/** @} */ // end of log_enum group

/**
 * @defgroup log_struct Logging Data Structures
 * @{
 */

// Log message structure
typedef struct {
    absolute_time_t timestamp; // Message timestamp
    log_level_t level;         // Message level
    uint8_t core_id;           // Core that generated the message
    const char* module;        // Module name
    const char* message;       // Message content
    uint32_t seq_num;          // Sequence number
} log_message_t;

// Logging configuration
typedef struct {
    log_level_t console_level;      // Logging level for console output
    log_level_t sdcard_level;       // Logging level for SD card output
    log_level_t flash_level;        // Logging level for flash output
    uint32_t buffer_size;           // Size of the message buffer
    uint32_t max_message_size;      // Maximum size of a single message
    const char* sdcard_filename;    // Filename for SD card logging
    uint32_t flash_offset;          // Offset in flash for storing logs
    uint32_t flash_size;            // Size of flash region for logs
    bool include_timestamp;         // Include timestamp in log messages
    bool include_level;             // Include level in log messages
    bool include_core_id;           // Include core ID in log messages
    bool color_output;              // Use ANSI colors for console output
} log_config_t;

/** @} */ // end of log_struct group

/**
 * @defgroup log_api Logging Application Programming Interface
 * @{
 */

 /**
 * @brief Flush the logs forcefully
 */
void log_flush(void);

/**
 * @brief Retrieve default configuration for logging manager.
 */
void log_get_default_config(log_config_t* config);

/**
 * @brief Initialize the logging manager.
 * 
 * @param config Data structure holding configuration information.
 * @return true if successful.
 * @return false if failed.
 */
bool log_init(const log_config_t* config);

/**
 * @brief Initialize and schedule the logger as a task.
 * 
 * @return true if successful.
 * @return false if failed.
 */
bool log_init_as_task(void);

/**
 * @brief Initialize core logging functions.
 * 
 * @param config Configuration structure to initialize with.
 * @return true if successful.
 * @return false if failed.
 */
bool log_init_core(const log_config_t* config);

/**
 * @brief Initialize spinlocks for logging.
 * @return true if successful.
 * @return false if failed.
 */
bool log_init_spinlocks(void);

/**
 * @brief Checks if the logging manager is initialized.
 * 
 * @param irq_num IRQ number to modify.
 * @param enabled true to enable, false to disable.
 * @return true if initialized.
 * @return false if not initialized.
 */
bool log_is_initialized(void);

/**
 * @brief Checks if the logging manager is fully initialized.
 * 
 * @return true if initialized.
 * @return false if not initialized.
 */
bool log_is_fully_initialized(void);

/**
 * @brief Write a message to the logging manager buffer.
 * 
 * @param level Logging level to display as.
 * @param module String representing the module the logging is occuring from.
 * @param format String consisting of format specifiers.
 * @param ... Variadic arguments
 */
void log_message(log_level_t level, const char* module, const char* format, ...);

/**
 * @brief Process logging messages in buffer.
 */
void log_process(void);

/**
 * @brief scheduler task for launching logging manager.
 * 
 * @param params Optional arguments.
 */
void log_scheduler_task(void* params);

/**
 * @brief Set logging destinations.
 * 
 * @param destinations Set destination to store too.
 */
void log_set_destinations(uint8_t destinations);

/**
 * @brief Set logging level.
 * 
 * @param level Logging level to direct.
 * @param destination Log destination to store to.
 */
void log_set_level(log_level_t level, log_destination_t destination);

/** @} */ // end of log_api group

#endif // LOG_MANAGER_H