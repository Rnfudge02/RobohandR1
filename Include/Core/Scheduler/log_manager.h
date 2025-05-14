/**
* @file log_manager.h
* @brief Robust logging system with multi-destination support
* @author Based on Robert Fudge's work
* @date 2025-05-14
*/

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log severity levels
 */
typedef enum {
    LOG_LEVEL_TRACE = 0,  // Most verbose - detailed tracing
    LOG_LEVEL_DEBUG,      // Debug information
    LOG_LEVEL_INFO,       // General information
    LOG_LEVEL_WARN,       // Warning conditions
    LOG_LEVEL_ERROR,      // Error conditions
    LOG_LEVEL_FATAL,      // Fatal errors
    LOG_LEVEL_NONE        // No logging
} log_level_t;

/**
 * @brief Log output destinations
 */
typedef enum {
    LOG_DEST_CONSOLE = 0x01,  // Output to console (USB/UART)
    LOG_DEST_SDCARD  = 0x02,  // Output to SD card
    LOG_DEST_FLASH   = 0x04,  // Output to flash memory
    LOG_DEST_ALL     = 0xFF   // Output to all available destinations
} log_destination_t;

/**
 * @brief Log message structure
 */
typedef struct {
    absolute_time_t timestamp;  // Message timestamp
    log_level_t level;          // Message severity level
    uint8_t core_id;            // Core ID (0 or 1)
    const char* module;         // Source module name
    const char* message;        // Log message
    uint32_t seq_num;           // Sequence number for ordering
} log_message_t;

/**
 * @brief Logger configuration
 */
typedef struct {
    log_level_t console_level;      // Minimum level for console output
    log_level_t sdcard_level;       // Minimum level for SD card output
    log_level_t flash_level;        // Minimum level for flash output
    uint32_t buffer_size;           // Size of internal message buffer
    uint32_t max_message_size;      // Maximum size of a log message
    const char* sdcard_filename;    // Filename for SD card logging
    uint32_t flash_offset;          // Starting offset in flash for logging
    uint32_t flash_size;            // Size of flash region for logging
    bool include_timestamp;         // Include timestamp in messages
    bool include_level;             // Include level in messages
    bool include_core_id;           // Include core ID in messages
    bool color_output;              // Use ANSI colors in console output
} log_config_t;

/**
 * @brief Initialize the logging system
 * 
 * @param config Logging configuration
 * @return true if initialization successful
 * @return false if initialization failed
 */
bool log_init(const log_config_t* config);

/**
 * @brief Get default logging configuration
 * 
 * @param config Pointer to config structure to fill
 */
void log_get_default_config(log_config_t* config);

/**
 * @brief Set the global logging level
 * 
 * @param level Minimum level to log
 * @param destination Destination(s) to apply level to
 */
void log_set_level(log_level_t level, log_destination_t destination);

/**
 * @brief Add a log message to the buffer
 * 
 * @param level Log severity level
 * @param module Source module name
 * @param format Format string (printf style)
 * @param ... Variable arguments
 */
void log_message(log_level_t level, const char* module, const char* format, ...);

/**
 * @brief Process and output pending log messages
 * 
 * @note This should be called regularly to flush the log buffer
 */
void log_process(void);

/**
 * @brief Flush all pending log messages
 * 
 * @note This ensures all buffered messages are written to their destinations
 */
void log_flush(void);

/**
 * @brief Set log output destinations
 * 
 * @param destinations Bitmask of destinations (LOG_DEST_*)
 */
void log_set_destinations(uint8_t destinations);

/**
 * @brief Convenience macros for logging
 */
#define LOG_TRACE(module, ...) log_message(LOG_LEVEL_TRACE, module, __VA_ARGS__)
#define LOG_DEBUG(module, ...) log_message(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define LOG_INFO(module, ...)  log_message(LOG_LEVEL_INFO, module, __VA_ARGS__)
#define LOG_WARN(module, ...)  log_message(LOG_LEVEL_WARN, module, __VA_ARGS__)
#define LOG_ERROR(module, ...) log_message(LOG_LEVEL_ERROR, module, __VA_ARGS__)
#define LOG_FATAL(module, ...) log_message(LOG_LEVEL_FATAL, module, __VA_ARGS__)

/**
 * @brief Get module name from filename
 */
#define LOG_MODULE_NAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : \
                         (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__))

#ifdef __cplusplus
}
#endif

#endif // LOG_MANAGER_H