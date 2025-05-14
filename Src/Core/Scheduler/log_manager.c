/**
* @file log_manager.c
* @brief Implementation of the logging system
* @author Based on Robert Fudge's work
* @date 2025-05-14
*/

#include "log_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/flash.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

// FAT filesystem support (if available)
#ifdef USE_FATFS
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#endif

// Define ANSI color codes for console output
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"

// Default values
#define DEFAULT_BUFFER_SIZE 4096
#define DEFAULT_MAX_MESSAGE_SIZE 256
#define DEFAULT_SDCARD_FILENAME "logs.txt"
#define DEFAULT_FLASH_OFFSET (1024 * 1024)  // 1MB offset
#define DEFAULT_FLASH_SIZE (256 * 1024)     // 256KB size

// Internal configuration and state
typedef struct {
    log_config_t config;
    uint8_t* buffer;
    uint32_t buffer_head;
    uint32_t buffer_tail;
    uint32_t buffer_count;
    uint32_t sequence_counter;
    uint8_t active_destinations;
    mutex_t log_mutex;
    log_level_t current_levels[3];  // Console, SD card, Flash
    char* temp_buffer;
    uint32_t flash_write_offset;
    bool initialized;
} log_state_t;

// Global logging state
static log_state_t log_state;

// Forward declarations for internal functions
static void log_write_console(const log_message_t* message, const char* formatted_message);
static void log_write_sdcard(const log_message_t* message, const char* formatted_message);
static void log_write_flash(const log_message_t* message, const char* formatted_message);
static const char* log_level_to_string(log_level_t level);
static const char* log_level_to_color(log_level_t level);

/**
 * @brief Initialize the logging system
 */
bool log_init(const log_config_t* config) {
    if (log_state.initialized) {
        return true;  // Already initialized
    }
    
    // Initialize mutex
    mutex_init(&log_state.log_mutex);
    
    // Copy configuration
    if (config != NULL) {
        memcpy(&log_state.config, config, sizeof(log_config_t));
    } else {
        // Use default configuration
        log_get_default_config(&log_state.config);
    }
    
    // Allocate message buffer
    log_state.buffer = malloc(log_state.config.buffer_size);
    if (log_state.buffer == NULL) {
        return false;
    }
    
    // Allocate temporary message buffer
    log_state.temp_buffer = malloc(log_state.config.max_message_size);
    if (log_state.temp_buffer == NULL) {
        free(log_state.buffer);
        return false;
    }
    
    // Initialize buffer pointers
    log_state.buffer_head = 0;
    log_state.buffer_tail = 0;
    log_state.buffer_count = 0;
    log_state.sequence_counter = 0;
    
    // Set default levels
    log_state.current_levels[0] = log_state.config.console_level;
    log_state.current_levels[1] = log_state.config.sdcard_level;
    log_state.current_levels[2] = log_state.config.flash_level;
    
    // Set active destinations
    log_state.active_destinations = LOG_DEST_CONSOLE;  // Start with console only
    
    // Initialize flash offset
    log_state.flash_write_offset = log_state.config.flash_offset;
    
    // Initialize SD card if available
#ifdef USE_FATFS
    // SD card initialization would go here
    // If successful, set LOG_DEST_SDCARD in active_destinations
#endif
    
    log_state.initialized = true;
    
    // Log initialization message
    LOG_INFO("LogMgr", "Logging system initialized");
    
    return true;
}

/**
 * @brief Get default logging configuration
 */
__attribute__((section(".time_critical")))
void log_get_default_config(log_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(log_config_t));
    
    config->console_level = LOG_LEVEL_INFO;
    config->sdcard_level = LOG_LEVEL_DEBUG;
    config->flash_level = LOG_LEVEL_ERROR;
    config->buffer_size = DEFAULT_BUFFER_SIZE;
    config->max_message_size = DEFAULT_MAX_MESSAGE_SIZE;
    config->sdcard_filename = DEFAULT_SDCARD_FILENAME;
    config->flash_offset = DEFAULT_FLASH_OFFSET;
    config->flash_size = DEFAULT_FLASH_SIZE;
    config->include_timestamp = true;
    config->include_level = true;
    config->include_core_id = true;
    config->color_output = true;
}

/**
 * @brief Set the global logging level
 */
__attribute__((section(".time_critical")))
void log_set_level(log_level_t level, log_destination_t destination) {
    mutex_enter_blocking(&log_state.log_mutex);
    
    if (destination & LOG_DEST_CONSOLE) {
        log_state.current_levels[0] = level;
    }
    
    if (destination & LOG_DEST_SDCARD) {
        log_state.current_levels[1] = level;
    }
    
    if (destination & LOG_DEST_FLASH) {
        log_state.current_levels[2] = level;
    }
    
    mutex_exit(&log_state.log_mutex);
}

/**
 * @brief Set log output destinations
 */
__attribute__((section(".time_critical")))
void log_set_destinations(uint8_t destinations) {
    mutex_enter_blocking(&log_state.log_mutex);
    log_state.active_destinations = destinations;
    mutex_exit(&log_state.log_mutex);
}

/**
 * @brief Add a log message to the buffer
 */
__attribute__((section(".time_critical")))
void log_message(log_level_t level, const char* module, const char* format, ...) {
    if (!log_state.initialized) {
        return;
    }
    
    // Check if this level would be logged anywhere
    if (level < log_state.current_levels[0] && 
        level < log_state.current_levels[1] && 
        level < log_state.current_levels[2]) {
        return;  // Nothing would log this, so skip
    }
    
    mutex_enter_blocking(&log_state.log_mutex);
    
    // Format the message
    va_list args;
    va_start(args, format);
    vsnprintf(log_state.temp_buffer, log_state.config.max_message_size, format, args);
    va_end(args);
    
    // Create log message
    log_message_t message;
    message.timestamp = get_absolute_time();
    message.level = level;
    message.core_id = get_core_num();
    message.module = module;
    message.message = log_state.temp_buffer;
    message.seq_num = log_state.sequence_counter++;
    
    // Format message for output
    char formatted_buffer[log_state.config.max_message_size];
    size_t offset = 0;
    
    // Add timestamp if enabled
    if (log_state.config.include_timestamp) {
        uint32_t ms = to_ms_since_boot(message.timestamp);
        offset += snprintf(formatted_buffer + offset, 
                          log_state.config.max_message_size - offset,
                          "[%5lu.%03lu] ", 
                          ms / 1000, ms % 1000);
    }
    
    // Add level if enabled
    if (log_state.config.include_level) {
        offset += snprintf(formatted_buffer + offset,
                          log_state.config.max_message_size - offset,
                          "[%s] ", 
                          log_level_to_string(message.level));
    }
    
    // Add core ID if enabled
    if (log_state.config.include_core_id) {
        offset += snprintf(formatted_buffer + offset,
                          log_state.config.max_message_size - offset,
                          "[C%d] ", 
                          message.core_id);
    }
    
    // Add module name
    offset += snprintf(formatted_buffer + offset,
                      log_state.config.max_message_size - offset,
                      "[%s] ", 
                      message.module);
    
    // Add message content
    snprintf(formatted_buffer + offset,
            log_state.config.max_message_size - offset,
            "%s", 
            message.message);
    
    // Write to console immediately (no buffering for console)
    if ((log_state.active_destinations & LOG_DEST_CONSOLE) && 
        level >= log_state.current_levels[0]) {
        log_write_console(&message, formatted_buffer);
    }
    
    // Queue for SD card and flash (these are buffered)
    if (((log_state.active_destinations & LOG_DEST_SDCARD) && 
         level >= log_state.current_levels[1]) ||
        ((log_state.active_destinations & LOG_DEST_FLASH) && 
         level >= log_state.current_levels[2])) {
        
        // Store in circular buffer
        // In a real implementation, you'd store the message and its metadata
        // For simplicity, we're just storing the formatted string
        
        // Check if buffer is full
        if (log_state.buffer_count >= log_state.config.buffer_size) {
            // Buffer full - either overwrite or drop
            // For now, we'll just drop
            mutex_exit(&log_state.log_mutex);
            return;
        }
        
        // Add to buffer
        size_t msg_len = strlen(formatted_buffer);
        if (msg_len > 0) {
            // Store message length first (to make it easier to extract)
            uint8_t len_bytes[4];
            len_bytes[0] = (msg_len >> 24) & 0xFF;
            len_bytes[1] = (msg_len >> 16) & 0xFF;
            len_bytes[2] = (msg_len >> 8) & 0xFF;
            len_bytes[3] = msg_len & 0xFF;
            
            // Store length
            for (int i = 0; i < 4; i++) {
                log_state.buffer[log_state.buffer_head] = len_bytes[i];
                log_state.buffer_head = (log_state.buffer_head + 1) % log_state.config.buffer_size;
                log_state.buffer_count++;
            }
            
            // Store message
            for (size_t i = 0; i < msg_len; i++) {
                log_state.buffer[log_state.buffer_head] = formatted_buffer[i];
                log_state.buffer_head = (log_state.buffer_head + 1) % log_state.config.buffer_size;
                log_state.buffer_count++;
            }
        }
    }
    
    mutex_exit(&log_state.log_mutex);
}

/**
 * @brief Process and output pending log messages
 */
__attribute__((section(".time_critical")))
void log_process(void) {
    if (!log_state.initialized) {
        return;
    }
    
    mutex_enter_blocking(&log_state.log_mutex);
    
    // Process up to 10 messages at a time to avoid blocking too long
    for (int i = 0; i < 10 && log_state.buffer_count > 4; i++) {
        // Extract message length
        uint32_t msg_len = 0;
        for (int j = 0; j < 4; j++) {
            msg_len = (msg_len << 8) | log_state.buffer[log_state.buffer_tail];
            log_state.buffer_tail = (log_state.buffer_tail + 1) % log_state.config.buffer_size;
            log_state.buffer_count--;
        }
        
        // Validate length
        if (msg_len == 0 || msg_len > log_state.config.max_message_size) {
            // Invalid length - something went wrong
            // Log an error and reset buffer
            printf("ERROR: Invalid log message length: %lu\n", msg_len);
            log_state.buffer_head = 0;
            log_state.buffer_tail = 0;
            log_state.buffer_count = 0;
            break;
        }
        
        // Extract message
        char msg_buffer[log_state.config.max_message_size];
        for (size_t j = 0; j < msg_len && j < log_state.config.max_message_size - 1; j++) {
            msg_buffer[j] = log_state.buffer[log_state.buffer_tail];
            log_state.buffer_tail = (log_state.buffer_tail + 1) % log_state.config.buffer_size;
            log_state.buffer_count--;
        }
        msg_buffer[msg_len] = '\0';
        
        // Create a dummy message for the writers
        log_message_t dummy_message;
        dummy_message.timestamp = get_absolute_time();
        dummy_message.level = LOG_LEVEL_INFO;  // Default
        dummy_message.core_id = get_core_num();
        dummy_message.module = "BUFFER";
        dummy_message.message = msg_buffer;
        
        // Write to SD card
        if (log_state.active_destinations & LOG_DEST_SDCARD) {
            log_write_sdcard(&dummy_message, msg_buffer);
        }
        
        // Write to flash
        if (log_state.active_destinations & LOG_DEST_FLASH) {
            log_write_flash(&dummy_message, msg_buffer);
        }
    }
    
    mutex_exit(&log_state.log_mutex);
}

/**
 * @brief Flush all pending log messages
 */
__attribute__((section(".time_critical")))
void log_flush(void) {
    if (!log_state.initialized) {
        return;
    }
    
    // Process all pending messages
    while (log_state.buffer_count > 0) {
        log_process();
    }
}

/**
 * @brief Write message to console
 */
__attribute__((section(".time_critical")))
static void log_write_console(const log_message_t* message, const char* formatted_message) {
    if (log_state.config.color_output) {
        printf("%s%s%s\n", 
               log_level_to_color(message->level), 
               formatted_message, 
               ANSI_COLOR_RESET);
    } else {
        printf("%s\n", formatted_message);
    }
}

/**
 * @brief Write message to SD card
 */
static void log_write_sdcard(const log_message_t* message, const char* formatted_message) {
#ifdef USE_FATFS
    // SD card write implementation would go here
    // For now, just a placeholder
    static bool sd_file_opened = false;
    static FIL file;
    
    if (!sd_file_opened) {
        if (f_open(&file, log_state.config.sdcard_filename, FA_WRITE | FA_OPEN_APPEND | FA_OPEN_ALWAYS) != FR_OK) {
            return;
        }
        sd_file_opened = true;
    }
    
    // Write message
    UINT bw;
    f_write(&file, formatted_message, strlen(formatted_message), &bw);
    f_write(&file, "\n", 1, &bw);
    
    // Sync to SD card periodically
    static uint32_t sync_counter = 0;
    if (++sync_counter % 10 == 0) {
        f_sync(&file);
    }
#endif
}

/**
 * @brief Write message to flash
 */
__attribute__((section(".time_critical")))
static void log_write_flash(const log_message_t* message, const char* formatted_message) {
    // Check if we have enough space in flash
    size_t msg_len = strlen(formatted_message) + 1;  // Include null terminator
    
    // Round up to nearest 4 bytes (flash program size)
    msg_len = (msg_len + 3) & ~3;
    
    // Check if we have enough space
    if (log_state.flash_write_offset + msg_len > log_state.config.flash_offset + log_state.config.flash_size) {
        // Not enough space - wrap around to beginning
        log_state.flash_write_offset = log_state.config.flash_offset;
        
        // Erase the flash sector
        uint32_t sector_offset = log_state.flash_write_offset & ~(FLASH_SECTOR_SIZE - 1);
        flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    }
    
    // Check if we need to erase a sector
    uint32_t current_sector = log_state.flash_write_offset & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t end_sector = (log_state.flash_write_offset + msg_len) & ~(FLASH_SECTOR_SIZE - 1);
    
    if (current_sector != end_sector) {
        // Message crosses sector boundary - erase the next sector
        flash_range_erase(end_sector, FLASH_SECTOR_SIZE);
    }
    
    // Prepare message buffer (must be aligned to 4 bytes for flash program)
    uint8_t flash_buffer[256];  // Max message size
    memset(flash_buffer, 0, sizeof(flash_buffer));
    memcpy(flash_buffer, formatted_message, strlen(formatted_message) + 1);
    
    // Program flash
    uint32_t offset = log_state.flash_write_offset - XIP_BASE;
    flash_range_program(offset, flash_buffer, msg_len);
    
    // Update offset
    log_state.flash_write_offset += msg_len;
}

/**
 * @brief Convert log level to string
 */
__attribute__((section(".time_critical")))
static const char* log_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_TRACE: return "TRACE";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default:              return "UNKN ";
    }
}

/**
 * @brief Convert log level to ANSI color code
 */
__attribute__((section(".time_critical")))
static const char* log_level_to_color(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_TRACE: return ANSI_COLOR_BLUE;
        case LOG_LEVEL_DEBUG: return ANSI_COLOR_CYAN;
        case LOG_LEVEL_INFO:  return ANSI_COLOR_GREEN;
        case LOG_LEVEL_WARN:  return ANSI_COLOR_YELLOW;
        case LOG_LEVEL_ERROR: return ANSI_COLOR_RED;
        case LOG_LEVEL_FATAL: return ANSI_COLOR_MAGENTA;
        default:              return ANSI_COLOR_RESET;
    }
}