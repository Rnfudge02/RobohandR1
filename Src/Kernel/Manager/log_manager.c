/**
* @file log_manager.c
* @brief Implementation of the logging system
* @author Based on Robert Fudge's work
* @date 2025-05-14
*/

#include "log_manager.h"
#include "scheduler.h"
#include "spinlock_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
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
    
    // Safe locking mechanism with fallback
    bool using_spinlocks;
    uint32_t log_lock_num;
    spin_lock_t* log_spin_lock;
    uint32_t console_lock_num;
    spin_lock_t* console_spin_lock;
    mutex_t fallback_mutex;
    
    log_level_t current_levels[3];  // Console, SD card, Flash
    char* temp_buffer;
    uint32_t flash_write_offset;
    bool initialized;
    bool fully_initialized;
} log_state_t;

// Global logging state
static log_state_t log_state;

// Forward declarations for internal functions
static void log_write_console(const log_message_t* message, const char* formatted_message);
static void log_write_sdcard(const char* formatted_message);
static void log_write_flash(const char* formatted_message);
static const char* log_level_to_string(log_level_t level);
static const char* log_level_to_color(log_level_t level);

static inline uint32_t log_acquire_lock(void);
static inline void log_release_lock(uint32_t save);
static inline uint32_t console_acquire_lock(void);
static inline void console_release_lock(uint32_t save);

void log_scheduler_task(void* params);

// Scheduler task ID
static int g_log_task_id = -1;

// Task-based logging initialization
bool log_init_as_task(void) {
    if (!log_state.initialized) {
        // Initialize core logging first
        log_config_t config;
        log_get_default_config(&config);
        if (!log_init_core(&config)) {
            return false;
        }
    }
    
    // Check if task already created
    if (g_log_task_id >= 0) {
        return true;  // Already initialized as task
    }
    
    // Create a dedicated logging task with appropriate priority
    g_log_task_id = scheduler_create_task(
        log_scheduler_task,     // Task function
        NULL,                   // No parameters
        10240,                   // Stack size
        TASK_PRIORITY_HIGH,     // High priority for reliability
        "log_task",             // Task name
        0,                      // Core 0 for logging
        TASK_TYPE_PERSISTENT    // Always running
    );
    
    if (g_log_task_id < 0) {
        printf("ERROR: Failed to create logging task\n");
        return false;
    }
    
    printf("INFO: Logging task created with ID: %d\n", g_log_task_id);
    return true;
}

void log_scheduler_task(void* params) {
    (void)params;  // Unused
    
    // Process pending log messages - this is now called regularly by the scheduler
    if (log_state.initialized && log_state.buffer_count > 0) {
        // Process at most 2 messages per task execution to maintain responsiveness
        for (int i = 0; i < 2 && log_state.buffer_count > 4; i++) {
            // Extract a message from the buffer and process it
            uint32_t save = log_acquire_lock();
            
            // Get message length
            uint32_t msg_len = 0;
            for (int j = 0; j < 4 && log_state.buffer_count > j; j++) {
                msg_len = (msg_len << 8) | log_state.buffer[log_state.buffer_tail];
                log_state.buffer_tail = (log_state.buffer_tail + 1) % log_state.config.buffer_size;
                log_state.buffer_count--;
            }
            
            // Safety check on message length
            if (msg_len == 0 || msg_len > log_state.config.max_message_size) {
                // Invalid length - reset buffer and exit
                log_state.buffer_head = 0;
                log_state.buffer_tail = 0;
                log_state.buffer_count = 0;
                log_release_lock(save);
                break;
            }
            
            // Extract message
            char msg_buffer[DEFAULT_MAX_MESSAGE_SIZE];
            for (size_t j = 0; j < msg_len && j < sizeof(msg_buffer) - 1; j++) {
                msg_buffer[j] = log_state.buffer[log_state.buffer_tail];
                log_state.buffer_tail = (log_state.buffer_tail + 1) % log_state.config.buffer_size;
                log_state.buffer_count--;
            }
            msg_buffer[msg_len < sizeof(msg_buffer) - 1 ? msg_len : sizeof(msg_buffer) - 2] = '\0';
            
            log_release_lock(save);
            
            // Now output to destinations (without holding lock)
            if (log_state.active_destinations & LOG_DEST_SDCARD) {
                log_write_sdcard(msg_buffer);
            }
            
            if (log_state.active_destinations & LOG_DEST_FLASH) {
                log_write_flash(msg_buffer);
            }
        }
    }
}

/**
 * @brief Check if logging system is initialized
 */
bool log_is_initialized(void) {
    return log_state.initialized;
}

/**
 * @brief Check if logging system is fully initialized with spinlocks
 */
bool log_is_fully_initialized(void) {
    return log_state.fully_initialized;
}

/**
 * @brief Initialize logging system core functionality
 */
// In log_manager.c - Initialize with spinlock manager

bool log_init_core(const log_config_t* config) {
    if (log_state.initialized) {
        return true;  // Already initialized
    }
    
    // Initialize mutex (fallback mechanism for early boot only)
    mutex_init(&log_state.fallback_mutex);
    
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
    
    // We start without spinlocks - will be set up later
    log_state.using_spinlocks = false;
    log_state.log_lock_num = UINT32_MAX;
    log_state.console_lock_num = UINT32_MAX;
    
    log_state.initialized = true;
    log_state.fully_initialized = false;
    
    printf("INFO: LogMgr - Logging system core initialized\n");
    
    return true;
}

bool log_init_spinlocks(void) {
    if (!log_state.initialized) {
        return false; // Core not initialized
    }
    
    if (log_state.fully_initialized) {
        return true; // Already fully initialized
    }
    
    // Allocate spinlocks using the spinlock manager
    log_state.log_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_LOGGING, "log_manager");
    if (log_state.log_lock_num == UINT_MAX) {
        printf("ERROR: LogMgr - Failed to allocate spinlock for logging\n");
        return false;
    }
    
    log_state.console_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_LOGGING, "console_output");
    if (log_state.console_lock_num == UINT_MAX) {
        printf("ERROR: LogMgr - Failed to allocate spinlock for console output\n");
        // Free the logging spinlock
        hw_spinlock_free(log_state.log_lock_num);
        log_state.log_lock_num = UINT_MAX;
        return false;
    }
    
    // Now we can use spinlocks
    log_state.using_spinlocks = true;
    log_state.fully_initialized = true;
    
    log_message(LOG_LEVEL_INFO, "LogMgr", "Logging system fully initialized with spinlocks");
    
    return true;
}

/**
 * @brief Legacy initialization function for backward compatibility
 */
bool log_init(const log_config_t* config) {
    if (!log_state.initialized && !log_init_core(config)) {
        return false;
    }
    
    // Try to complete with spinlocks if spinlock manager is available
    if (!log_state.fully_initialized && hw_spinlock_manager_is_core_initialized()) {
        log_init_spinlocks();
        // Note: we return true even if spinlock init fails, as long as core init succeeded
    }
    
    return true;
}

// Now replace all mutex_enter_blocking/mutex_exit calls with log_acquire_lock/log_release_lock
// And all direct printf calls in log_write_console with protected console output

/**
 * @brief Add a log message to the buffer
 */
__attribute__((section(".time_critical")))
void log_message(log_level_t level, const char* module, const char* format, ...) {  // NOSONAR - Simplest implementation
    // Early exit if not initialized or level not sufficient for any output
    if (!log_state.initialized) {
        // Direct fallback still needed for early boot
        va_list args;
        va_start(args, format);
        printf("[%s] [%s] ", log_level_to_string(level), module);
        vprintf(format, args);  // NOSONAR - Easiest implementation
        printf("\n");
        va_end(args);
        return;
    }
    
    // Format the message
    va_list args;
    va_start(args, format);
    vsnprintf(log_state.temp_buffer, log_state.config.max_message_size, format, args);  // NOSONAR - Easiest implementation
    va_end(args);
    
    // Create message and format it
    log_message_t message;
    message.timestamp = get_absolute_time();
    message.level = level;
    message.core_id = (uint8_t) (get_core_num() & 0xFF);
    message.module = module;
    message.message = log_state.temp_buffer;
    
    // Construct the formatted message
    char formatted_message[DEFAULT_MAX_MESSAGE_SIZE];
    size_t offset = 0;
    
    // Add timestamp
    if (log_state.config.include_timestamp) {
        uint32_t ms = to_ms_since_boot(message.timestamp);
        offset += snprintf(formatted_message + offset, 
                         sizeof(formatted_message) - offset,
                         "[%5lu.%03lu] ", 
                         ms / 1000, ms % 1000);
    }
    
    // Add level
    if (log_state.config.include_level) {
        offset += snprintf(formatted_message + offset,
                         sizeof(formatted_message) - offset,
                         "[%s] ", 
                         log_level_to_string(message.level));
    }
    
    // Add core ID
    if (log_state.config.include_core_id) {
        offset += snprintf(formatted_message + offset,
                         sizeof(formatted_message) - offset,
                         "[C%d] ", 
                         message.core_id);
    }
    
    // Add module
    offset += snprintf(formatted_message + offset,
                     sizeof(formatted_message) - offset,
                     "[%s] ", 
                     message.module);
    
    // Add message
    snprintf(formatted_message + offset,
           sizeof(formatted_message) - offset,
           "%s", 
           message.message);
    
    // Console output is still direct for responsiveness
    if ((log_state.active_destinations & LOG_DEST_CONSOLE) && 
        level >= log_state.current_levels[0]) {
        
        // Use spinlock manager for atomic console output
        uint32_t console_save = console_acquire_lock();
        
        if (log_state.config.color_output) {
            printf("%s%s%s\n", 
                   log_level_to_color(message.level), 
                   formatted_message, 
                   ANSI_COLOR_RESET);
        } else {
            printf("%s\n", formatted_message);
        }
        
        // Ensure output is visible immediately
        fflush(stdout);
        
        console_release_lock(console_save);
    }
    
    // Queue for other destinations (processed by task)
    if (((log_state.active_destinations & LOG_DEST_SDCARD) && 
         level >= log_state.current_levels[1]) ||
        ((log_state.active_destinations & LOG_DEST_FLASH) && 
         level >= log_state.current_levels[2])) {
        
        // Acquire lock to add to buffer
        uint32_t save = log_acquire_lock();
        
        // Store in circular buffer
        size_t msg_len = strlen(formatted_message);
        
        // Check if we have space
        if (log_state.buffer_count + msg_len + 4 <= log_state.config.buffer_size) {
            // Store length
            uint8_t len_bytes[4];
            len_bytes[0] = (msg_len >> 24) & 0xFF;
            len_bytes[1] = (msg_len >> 16) & 0xFF;
            len_bytes[2] = (msg_len >> 8) & 0xFF;
            len_bytes[3] = msg_len & 0xFF;
            
            for (int i = 0; i < 4; i++) {
                log_state.buffer[log_state.buffer_head] = len_bytes[i];
                log_state.buffer_head = (log_state.buffer_head + 1) % log_state.config.buffer_size;
                log_state.buffer_count++;
            }
            
            // Store message
            for (size_t i = 0; i < msg_len; i++) {
                log_state.buffer[log_state.buffer_head] = formatted_message[i];
                log_state.buffer_head = (log_state.buffer_head + 1) % log_state.config.buffer_size;
                log_state.buffer_count++;
            }
        } else {
            // Buffer full - note the overflow but don't overwrite
            static uint32_t overflow_count = 0;
            if (++overflow_count % 100 == 1) {
                printf("WARNING: Log buffer overflow (%lu messages dropped)\n", overflow_count);
            }
        }
        
        log_release_lock(save);
        
        // Now that we've queued the message, make sure the task runs soon
        if (g_log_task_id >= 0) {
            scheduler_resume_task(g_log_task_id);  // Hint to scheduler to run log task
        }
    }
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
    uint32_t save = hw_spinlock_acquire(log_state.log_lock_num, scheduler_get_current_task());
    
    
    if (destination & LOG_DEST_CONSOLE) {
        log_state.current_levels[0] = level;
    }
    
    if (destination & LOG_DEST_SDCARD) {
        log_state.current_levels[1] = level;
    }
    
    if (destination & LOG_DEST_FLASH) {
        log_state.current_levels[2] = level;
    }
    
    hw_spinlock_release(log_state.log_lock_num, save);
}

/**
 * @brief Set log output destinations
 */
__attribute__((section(".time_critical")))
void log_set_destinations(uint8_t destinations) {
    uint32_t save = hw_spinlock_acquire(log_state.log_lock_num, scheduler_get_current_task());
    log_state.active_destinations = destinations;
    hw_spinlock_release(log_state.log_lock_num, save);
}

/**
 * @brief Add a log message to the buffer
 */
__attribute__((section(".time_critical")))




/**
 * @brief Process and output pending log messages
 */
__attribute__((section(".time_critical")))
void log_process(void) {
    if (!log_state.initialized) {
        return;
    }
    
    uint32_t save = log_acquire_lock();
    
    // Safety check - don't process if buffer is empty or corrupted
    if (log_state.buffer_count < 4) {
        log_release_lock(save);
        return;
    }
    
    // Process only one message at a time to avoid blocking too long
    uint32_t msg_len = 0;
    
    // Extract message length with proper bounds checking
    for (int j = 0; j < 4 && log_state.buffer_count > j; j++) {
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
        log_release_lock(save);
        return;
    }
    
    // Extract message with bounds checking
    char msg_buffer[log_state.config.max_message_size];
    size_t actual_len = 0;
    for (size_t j = 0; j < msg_len && j < log_state.config.max_message_size - 1 && j < log_state.buffer_count; j++) {
        msg_buffer[j] = log_state.buffer[log_state.buffer_tail];
        log_state.buffer_tail = (log_state.buffer_tail + 1) % log_state.config.buffer_size;
        log_state.buffer_count--;
        actual_len = j + 1; // Track actual bytes written
    }
    
    // Place null terminator at the end of actual content
    msg_buffer[actual_len] = '\0';
    
    // Release lock before writing to outputs to avoid deadlocks
    log_release_lock(save);
    
    // Write to SD card and flash (without holding the lock)
    if (log_state.active_destinations & LOG_DEST_SDCARD) {
        log_write_sdcard(msg_buffer);
    }
    
    if (log_state.active_destinations & LOG_DEST_FLASH) {
        log_write_flash(msg_buffer);
    }
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
    // Acquire console lock for atomic printing
    uint32_t save = console_acquire_lock();
    
    // Keep console output simple to avoid blocking
    if (message && formatted_message) {
        if (log_state.config.color_output) {
            printf("%s%s%s\n", 
                   log_level_to_color(message->level), 
                   formatted_message, 
                   ANSI_COLOR_RESET);
        } else {
            printf("%s\n", formatted_message);
        }
        
        // Force immediate flush to ensure output is visible
        fflush(stdout);
    }
    
    console_release_lock(save);
}

/**
 * @brief Write message to SD card
 */
static void log_write_sdcard(const char* formatted_message) {
    (void) formatted_message;
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
static void log_write_flash(const char* formatted_message) {
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

static inline uint32_t log_acquire_lock(void) {
    if (log_state.using_spinlocks) {
        // Use spinlock manager to acquire lock
        return hw_spinlock_acquire(log_state.log_lock_num, scheduler_get_current_task());
    } else {
        // Fallback for early boot when spinlock manager isn't initialized
        mutex_enter_blocking(&log_state.fallback_mutex);
        return 0;
    }
}

static inline void log_release_lock(uint32_t save) {
    if (log_state.using_spinlocks) {
        // Use spinlock manager to release lock
        hw_spinlock_release(log_state.log_lock_num, save);
    } else {
        // Fallback for early boot
        mutex_exit(&log_state.fallback_mutex);
    }
}

static inline uint32_t console_acquire_lock(void) {
    if (log_state.using_spinlocks) {
        // Use spinlock manager to acquire console lock
        return hw_spinlock_acquire(log_state.console_lock_num, scheduler_get_current_task());
    } else {
        // Fallback for early boot
        mutex_enter_blocking(&log_state.fallback_mutex);
        return 0;
    }
}

static inline void console_release_lock(uint32_t save) {
    if (log_state.using_spinlocks) {
        // Use spinlock manager to release console lock
        hw_spinlock_release(log_state.console_lock_num, save);
    } else {
        // Fallback for early boot
        mutex_exit(&log_state.fallback_mutex);
    }
}