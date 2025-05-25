/**
* @file servo_manager.c
* @brief Servo manager implementation for RTOS-based servo control
* @date 2025-05-14
*/

#include "log_manager.h"
#include "scheduler.h"
#include "servo_manager.h"
#include "servo_controller.h"
#include "spinlock_manager.h"
#include "usb_shell.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Servo entry structure
 */
typedef struct {
    servo_controller_t controller;  // Servo controller handle
    uint id;                       // Servo ID
    bool is_active;                // Whether the servo is active
} servo_entry_t;

/**
 * @brief Servo manager structure
 */
struct servo_manager_s {
    servo_entry_t servos[SERVO_MANAGER_MAX_SERVOS];  // Array of servo entries
    uint32_t task_period_ms;                       // Task period in milliseconds
    uint32_t last_execution_time;                  // Time of last task execution
    uint32_t access_lock_num;                      // Lock for thread-safe access
    uint32_t lock_owner;                           // ID of task that acquired the lock (0 = none)
    uint32_t lock_save;                            // Saved state for unlocking
    servo_movement_callback_t callback;            // Movement callback function
    void* callback_data;                           // User data for callback
    bool is_running;                               // Whether the manager is running
    bool enable_all_on_start;                      // Whether to enable all servos on start
};

// Global servo manager instance
static servo_manager_t g_servo_manager = NULL;

// Servo manager task ID from scheduler
static int g_servo_task_id = -1;

// Spinlock for init/deinit operations
static uint32_t g_servo_lock_num;


// Private function declarations
static void servo_manager_scheduler_task(void *param);

servo_manager_t servo_manager_create(const servo_manager_config_t* config) {
    if (config == NULL) {
        return NULL;
    }
    
    // Allocate manager structure
    servo_manager_t manager = (servo_manager_t)malloc(sizeof(struct servo_manager_s));
    if (manager == NULL) {
        return NULL;
    }
    
    // Initialize the manager
    memset(manager, 0, sizeof(struct servo_manager_s));
    manager->task_period_ms = config->task_period_ms;
    manager->is_running = false;
    manager->enable_all_on_start = config->enable_all_on_start;
    

    
    manager->access_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SERVO, "servo_manager");
    manager->lock_owner = 0;
    manager->lock_save = 0;
    
    return manager;
}

void servo_manager_get_default_config(servo_manager_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(servo_manager_config_t));
    config->task_period_ms = 20;  // Default to 20ms (50Hz)
    config->enable_all_on_start = true;
}

int servo_manager_add_servo(servo_manager_t manager, servo_controller_t controller, uint id) {
    if (manager == NULL || controller == NULL) {
        return -1;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // If ID is 0, auto-assign the next available ID
    if (id == 0) {
        id = 1;  // Start from 1
        
        // Find the highest ID currently in use
        for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
            if (manager->servos[i].controller != NULL && manager->servos[i].id >= id) {
                id = manager->servos[i].id + 1;
            }
        }
    }
    
    // Check if ID already exists
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // ID already in use
            servo_manager_unlock(manager);
            return -1;
        }
    }
    
    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        // No empty slots available
        servo_manager_unlock(manager);
        return -1;
    }
    
    // Add servo to the manager
    manager->servos[slot].controller = controller;
    manager->servos[slot].id = id;
    manager->servos[slot].is_active = false;
    
    servo_manager_unlock(manager);
    return id;
}

bool servo_manager_remove_servo(servo_manager_t manager, uint id) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    int slot = -1;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        // Servo not found
        servo_manager_unlock(manager);
        return false;
    }
    
    // If servo is active, disable it first
    if (manager->servos[slot].is_active) {
        servo_controller_disable(manager->servos[slot].controller);
    }
    
    // Clear the slot
    manager->servos[slot].controller = NULL;
    manager->servos[slot].id = 0;
    manager->servos[slot].is_active = false;
    
    servo_manager_unlock(manager);
    return true;
}

bool servo_manager_start(servo_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Enable all servos if configured
    if (manager->enable_all_on_start) {
        for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
            if ((manager->servos[i].controller != NULL) && (servo_controller_enable(manager->servos[i].controller))) {
                manager->servos[i].is_active = true;
            }
        }
    }
    
    // Mark the manager as running
    manager->is_running = true;
    manager->last_execution_time = to_ms_since_boot(get_absolute_time());
    
    servo_manager_unlock(manager);
    return true;
}

bool servo_manager_stop(servo_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Disable all servos
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if ((manager->servos[i].controller != NULL && manager->servos[i].is_active) && (servo_controller_disable(manager->servos[i].controller))) {
            manager->servos[i].is_active = false;
        }
    }
    
    // Mark the manager as not running
    manager->is_running = false;
    
    servo_manager_unlock(manager);
    return true;
}

bool servo_manager_enable_servo(servo_manager_t manager, uint id) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Enable the servo
            if (servo_controller_enable(manager->servos[i].controller)) {
                manager->servos[i].is_active = true;
                found = true;
            }
            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_disable_servo(servo_manager_t manager, uint id) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Disable the servo
            if (servo_controller_disable(manager->servos[i].controller)) {
                manager->servos[i].is_active = false;
                found = true;
            }
            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_set_position(servo_manager_t manager, uint id, float position) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;

    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Set position
            if (servo_controller_set_position(manager->servos[i].controller, position)) {
                found = true;
                
                
            }

            // Call movement callback if registered
            if ((manager->callback != NULL)  && found) {
                manager->callback(id, position, manager->callback_data);
            }

            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_set_position_percent(servo_manager_t manager, uint id, float percentage) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    float position = 0.0f;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Set position percentage
            if (servo_controller_set_position_percent(manager->servos[i].controller, percentage)) {
                found = true;
                
                // Get the actual position for the callback
                position = servo_controller_get_position(manager->servos[i].controller);
                
                
            }
            // Call movement callback if registered
            if ((manager->callback != NULL) && found) {
                manager->callback(id, position, manager->callback_data);
            }

            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_set_pulse(servo_manager_t manager, uint id, uint pulse_us) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    float position = 0.0f;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Set pulse width
            if (servo_controller_set_pulse(manager->servos[i].controller, pulse_us)) {
                found = true;
                
                // Get the actual position for the callback
                position = servo_controller_get_position(manager->servos[i].controller);
            }

            // Call movement callback if registered
            if ((manager->callback != NULL) && found) {
                manager->callback(id, position, manager->callback_data);
            }

            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_set_speed(servo_manager_t manager, uint id, float speed) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Set speed
            if (servo_controller_set_speed(manager->servos[i].controller, speed)) {
                found = true;
            }

            // Call movement callback if registered
            if ((manager->callback != NULL) && found) {
                manager->callback(id, speed, manager->callback_data);
            }

            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_configure_sweep(servo_manager_t manager, uint id,
                                 float min_pos, float max_pos, 
                                 float speed_deg_per_sec) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Configure sweep
            if (servo_controller_configure_sweep(manager->servos[i].controller, 
                                               min_pos, max_pos, speed_deg_per_sec)) {
                found = true;
            }
            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_set_mode(servo_manager_t manager, uint id, servo_mode_t mode) {
    if (manager == NULL || id == 0) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Set mode
            if (servo_controller_set_mode(manager->servos[i].controller, mode)) {
                found = true;
                
                // Update active state based on mode
                manager->servos[i].is_active = (mode != SERVO_MODE_DISABLED);
            }
            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_get_position(servo_manager_t manager, uint id, float* position) {
    if (manager == NULL || id == 0 || position == NULL) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    bool found = false;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            // Get position
            *position = servo_controller_get_position(manager->servos[i].controller);
            found = true;
            break;
        }
    }
    
    servo_manager_unlock(manager);
    return found;
}

bool servo_manager_register_callback(servo_manager_t manager, 
                                   servo_movement_callback_t callback,
                                   void* user_data) {
    if (manager == NULL || callback == NULL) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    manager->callback = callback;
    manager->callback_data = user_data;
    
    servo_manager_unlock(manager);
    return true;
}

void servo_manager_task(void* param) {
    servo_manager_t manager = (servo_manager_t)param;
    
    if (manager == NULL || !manager->is_running) {
        return;
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Check if it's time to execute the task
    if ((current_time - manager->last_execution_time) >= manager->task_period_ms) {
        // Acquire the lock
        if (!servo_manager_lock(manager)) {
            return;
        }
        
        // Execute task for each active servo
        for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
            if (manager->servos[i].controller != NULL && manager->servos[i].is_active) {
                // Execute servo controller task
                servo_controller_task(manager->servos[i].controller);
            }
        }
        
        // Update last execution time
        manager->last_execution_time = current_time;
        
        // Release the lock
        servo_manager_unlock(manager);
    }
}

servo_controller_t servo_manager_get_controller(servo_manager_t manager, uint id) {
    if (manager == NULL || id == 0) {
        return NULL;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Find the servo
    servo_controller_t controller = NULL;
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL && manager->servos[i].id == id) {
            controller = manager->servos[i].controller;
            break;
        }
    }
    
    servo_manager_unlock(manager);
    return controller;
}

bool servo_manager_destroy(servo_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    // Lock access
    servo_manager_lock(manager);
    
    // Stop all servos
    for (int i = 0; i < SERVO_MANAGER_MAX_SERVOS; i++) {
        if (manager->servos[i].controller != NULL) {
            if (manager->servos[i].is_active) {
                servo_controller_disable(manager->servos[i].controller);
            }
            // Note: We don't destroy the controllers here, as they may be used elsewhere
            manager->servos[i].controller = NULL;
        }
    }
    
    // Release lock
    if (manager->lock_owner != 0) {
        hw_spinlock_release(manager->access_lock_num, manager->lock_save);
    }
    
    // Free the manager structure
    free(manager);
    
    return true;
}

bool servo_manager_lock(servo_manager_t manager) {
    if (manager == NULL || manager->access_lock_num == NULL) {
        return false;
    }
    
    // Get current task ID for ownership tracking
    int task_id = scheduler_get_current_task();
    
    // Check if we already own the lock
    if (manager->lock_owner == task_id && task_id != 0) {
        // Already locked by us, just return true
        return true;
    }
    
    // Store owner and save value
    manager->lock_owner = task_id;
    manager->lock_save = hw_spinlock_acquire(manager->access_lock_num, scheduler_get_current_task());
    
    return true;
}

void servo_manager_unlock(servo_manager_t manager) {
    if (manager == NULL || manager->access_lock_num == NULL) {
        return;
    }
    
    // Only unlock if we're the owner
    int task_id = scheduler_get_current_task();
    if (manager->lock_owner == task_id || task_id == 0) {
        // Release the lock
        hw_spinlock_release(manager->access_lock_num, manager->lock_save);
        manager->lock_owner = 0;
    }
}

servo_manager_t servo_manager_get_instance(void) {
    return g_servo_manager;
}

/**
 * @brief Servo manager task function for the scheduler
 * 
 * This function is registered with the scheduler and called periodically.
 * 
 * @param param Parameters (unused)
 */
static void servo_manager_scheduler_task(void *param) {
    (void)param; // Unused parameter
    
    // Call the servo manager task function
    servo_manager_t manager = servo_manager_get_instance();
    if (manager != NULL) {
        // Use the task function directly without additional locking
        // (the task function handles locking internally)
        servo_manager_task(manager);
    }
}

//Init/Task functions

bool servo_manager_init(void) {
    // Initialize spinlock
    g_servo_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SERVO, "servo_manager_init");
    if (g_servo_lock_num == UINT_MAX) {
        log_message(LOG_LEVEL_ERROR, "Servo Manager Init", "Failed to claim spinlock for servo manager.");
        return false;
    }
    
    // Acquire lock to prevent race conditions during initialization
    uint32_t save = hw_spinlock_acquire(g_servo_lock_num, scheduler_get_current_task());
    
    // Check if already initialized
    if (g_servo_manager != NULL) {
        hw_spinlock_release(g_servo_lock_num, save);
        return true;  // Already initialized
    }
    
    // Create servo manager with default configuration
    servo_manager_config_t sm_config;
    servo_manager_get_default_config(&sm_config);
    sm_config.task_period_ms = 20;  // 50Hz update rate (20ms)
    
    g_servo_manager = servo_manager_create(&sm_config);
    if (g_servo_manager == NULL) {
        log_message(LOG_LEVEL_ERROR, "Servo Manager Init", "Failed to create servo manager.");
        hw_spinlock_release(g_servo_lock_num, save);
        return false;
    }
    
    // Create scheduler task
    // Use a dedicated task for servo management with high priority
    // to ensure consistent timing
    g_servo_task_id = scheduler_create_task(
        servo_manager_scheduler_task,   // Task function
        NULL,                           // No parameters needed
        2048,                           // Stack size
        TASK_PRIORITY_HIGH,             // High priority for consistent timing
        "servo_mgr",                    // Task name
        0,                              // Core 1 for servo tasks
        TASK_TYPE_PERSISTENT            // Runs indefinitely
    );
    
    if (g_servo_task_id < 0) {
        log_message(LOG_LEVEL_ERROR, "Servo Manager Init", "Failed to create servo manager task.");
        servo_manager_destroy(g_servo_manager);
        g_servo_manager = NULL;
        hw_spinlock_release(g_servo_lock_num, save);
        return false;
    }
    
    // Start the servo manager
    if (!servo_manager_start(g_servo_manager)) {
        log_message(LOG_LEVEL_ERROR, "Servo Manager Init", "Failed to start servo manager.");
        servo_manager_destroy(g_servo_manager);
        g_servo_manager = NULL;
        hw_spinlock_release(g_servo_lock_num, save);
        return false;
    }
    
    // Log success message if logging is available
    log_message(LOG_LEVEL_INFO, "Servo Manager Init", "Servo manager initialized successfully.");
    
    // Release lock
    hw_spinlock_release(g_servo_lock_num, save);
    
    return true;
}

bool servo_manager_deinit(void) {
    // Acquire lock
    uint32_t save = hw_spinlock_acquire(g_servo_lock_num, scheduler_get_current_task());
    
    if (g_servo_manager == NULL) {
        hw_spinlock_release(g_servo_lock_num, save);
        return true;  // Already deinitialized
    }
    
    // Remove scheduler task if it exists
    if (g_servo_task_id >= 0) {
        scheduler_delete_task(g_servo_task_id);
        g_servo_task_id = -1;
    }
    
    // Destroy servo manager
    servo_manager_destroy(g_servo_manager);
    g_servo_manager = NULL;
    
    // Release lock
    hw_spinlock_release(g_servo_lock_num, save);
    
    return true;
}

bool servo_manager_ensure_task(void) {
    // If task ID is valid, task already exists
    if (g_servo_task_id >= 0) {
        return true;
    }
    
    // Acquire lock
    uint32_t save = hw_spinlock_acquire(g_servo_lock_num, scheduler_get_current_task());
    
    // Check again after acquiring lock
    if (g_servo_task_id >= 0) {
        hw_spinlock_release(g_servo_lock_num, save);
        return true;
    }
    
    // Create scheduler task
    g_servo_task_id = scheduler_create_task(
        servo_manager_scheduler_task,   // Task function
        NULL,                           // No parameters needed
        2048,                           // Stack size
        TASK_PRIORITY_HIGH,             // High priority for consistent timing
        "servo_mgr",                    // Task name
        1,                              // Core 0 for servo tasks
        TASK_TYPE_PERSISTENT            // Runs indefinitely
    );
    
    bool success = (g_servo_task_id >= 0);
    
    if (success) {
        log_message(LOG_LEVEL_INFO, "Servo Manager Init", "Servo manager task created with ID: %d.", g_servo_task_id);
    } else {
        log_message(LOG_LEVEL_ERROR, "Servo Manager Init", "Failed to create servo manager task.");
    }
    
    // Release lock
    hw_spinlock_release(g_servo_lock_num, save);
    
    return success;
}

/**
 * @brief Print servo command usage information
 */
static void print_servo_help(void) {
    printf("Usage: servo <command> [args...]\n");
    printf("Commands:\n");
    printf("  create <pin> [min_pulse] [max_pulse] - Create a new servo on specified GPIO pin\n");
    printf("  list       - List all registered servos\n");
    printf("  position <id> <degrees> - Set servo position in degrees\n");
    printf("  percent <id> <percent>  - Set servo position as percentage (0-100)\n");
    printf("  pulse <id> <us>         - Set servo pulse width in microseconds\n");
    printf("  speed <id> <percent>    - Set continuous rotation servo speed (-100 to 100)\n");
    printf("  sweep <id> <min> <max> <speed> - Configure automatic sweep mode\n");
    printf("  mode <id> <mode>        - Set servo mode (0=disabled, 1=position, 2=speed, 3=sweep)\n");
    printf("  enable <id>             - Enable servo output\n");
    printf("  disable <id>            - Disable servo output\n");
    printf("  center <id>             - Center the servo\n");
}

/**
 * @brief Get or initialize the servo manager
 * 
 * @return servo_manager_t Manager instance or NULL if failed
 */
static servo_manager_t ensure_servo_manager(void) {
    servo_manager_t manager = servo_manager_get_instance();
    if (manager == NULL) {
        printf("Servo manager not initialized. Initializing now...\n");
        if (!servo_manager_init()) {
            printf("Failed to initialize servo manager\n");
            return NULL;
        }
        manager = servo_manager_get_instance();
        if (manager == NULL) {
            printf("Failed to get servo manager instance\n");
            return NULL;
        }
    }
    return manager;
}

/**
 * @brief Convert servo mode to string representation
 */
static const char* servo_mode_to_string(servo_mode_t mode) {
    switch (mode) {
        case SERVO_MODE_DISABLED: return "Disabled";
        case SERVO_MODE_POSITION: return "Position";
        case SERVO_MODE_SPEED:    return "Speed";
        case SERVO_MODE_SWEEP:    return "Sweep";
        default:                  return "Unknown";
    }
}

/**
 * @brief Handle servo create command
 */
static int handle_servo_create(servo_manager_t manager, int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: servo create <pin> [min_pulse] [max_pulse]\n");
        return 1;
    }

    uint gpio_pin = atoi(argv[2]);
    
    // Create servo with default configuration
    servo_config_t config;
    servo_controller_get_default_config(&config);
    config.gpio_pin = gpio_pin;
    
    // Apply custom pulse range if specified
    if (argc >= 4) {
        config.min_pulse_us = atoi(argv[3]);
    }
    if (argc >= 5) {
        config.max_pulse_us = atoi(argv[4]);
    }
    
    servo_controller_t controller = servo_controller_create(&config);
    if (controller == NULL) {
        printf("Failed to create servo controller for GPIO %u\n", gpio_pin);
        return 1;
    }
    
    // Add servo to manager with auto-assigned ID
    int id = servo_manager_add_servo(manager, controller, 0);
    if (id < 0) {
        printf("Failed to add servo to manager\n");
        servo_controller_destroy(controller);
        return 1;
    }
    
    printf("Created servo on GPIO %u with ID %d\n", gpio_pin, id);
    printf("Pulse range: %u to %u microseconds\n", config.min_pulse_us, config.max_pulse_us);
    printf("Angle range: %.1f to %.1f degrees\n", config.min_angle_deg, config.max_angle_deg);
    
    // Enable the servo by default
    if (servo_manager_enable_servo(manager, id)) {
        printf("Servo enabled and centered\n");
        servo_manager_set_position(manager, id, 0.0f);  // Center position
    }
    
    return 0;
}

/**
 * @brief Handle servo list command
 */
static int handle_servo_list(servo_manager_t manager) {
    printf("Registered servos:\n");
    printf("ID | GPIO | Position | Pulse (us) | Mode\n");
    printf("---+------+----------+-----------+--------\n");
    
    // For simplicity, we'll check IDs from 1 to 32
    for (uint id = 1; id <= 32; id++) {
        servo_controller_t controller = servo_manager_get_controller(manager, id);
        if (controller != NULL) {
            float position;
            if (servo_manager_get_position(manager, id, &position)) {
                uint pulse = servo_controller_get_pulse(controller);
                servo_mode_t mode = servo_controller_get_mode(controller);
                
                printf("%2u | %4u | %8.1f | %9u | %s\n", 
                    id, 
                    servo_controller_get_gpio_pin(controller),
                    position, 
                    pulse,
                    servo_mode_to_string(mode));
            }
        }
    }
    
    return 0;
}

/**
 * @brief Handle servo position command
 */
static int handle_servo_position(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: servo position <id> <degrees>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    float position = (float) atof(argv[3]);
    
    if (!servo_manager_set_position(manager, id, position)) {
        printf("Failed to set position for servo %u\n", id);
        return 1;
    }
    
    printf("Set servo %u position to %.1f degrees\n", id, position);
    return 0;
}

/**
 * @brief Handle servo percent command
 */
static int handle_servo_percent(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: servo percent <id> <percent>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    float percent = (float) atof(argv[3]);
    
    if (!servo_manager_set_position_percent(manager, id, percent)) {
        printf("Failed to set position for servo %u\n", id);
        return 1;
    }
    
    // Get actual position for display
    float position;
    servo_manager_get_position(manager, id, &position);
    
    printf("Set servo %u position to %.1f%% (%.1f degrees)\n", id, percent, position);
    return 0;
}

/**
 * @brief Handle servo pulse command
 */
static int handle_servo_pulse(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: servo pulse <id> <us>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    uint pulse = atoi(argv[3]);
    
    if (!servo_manager_set_pulse(manager, id, pulse)) {
        printf("Failed to set pulse for servo %u\n", id);
        return 1;
    }
    
    // Get actual position for display
    float position = 0.0f;
    servo_manager_get_position(manager, id, &position);
    
    printf("Set servo %u pulse to %u us (%.1f degrees)\n", id, pulse, position);
    return 0;
}

/**
 * @brief Handle servo speed command
 */
static int handle_servo_speed(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: servo speed <id> <percent>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    float speed = (float) atof(argv[3]);
    
    if (!servo_manager_set_speed(manager, id, speed)) {
        printf("Failed to set speed for servo %u\n", id);
        return 1;
    }
    
    printf("Set servo %u speed to %.1f%%\n", id, speed);
    return 0;
}

/**
 * @brief Handle servo sweep command
 */
static int handle_servo_sweep(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: servo sweep <id> <min> <max> <speed>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    float min_pos = (float) atof(argv[3]);
    float max_pos = (float) atof(argv[4]);
    float speed = (float) atof(argv[5]);
    
    if (!servo_manager_configure_sweep(manager, id, min_pos, max_pos, speed)) {
        printf("Failed to configure sweep for servo %u\n", id);
        return 1;
    }
    
    printf("Configured sweep for servo %u: %.1f to %.1f degrees at %.1f deg/s\n",
           id, min_pos, max_pos, speed);
    return 0;
}

/**
 * @brief Handle servo mode command
 */
static int handle_servo_mode(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: servo mode <id> <mode>\n");
        printf("Modes: 0=Disabled, 1=Position, 2=Speed, 3=Sweep\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    int mode_val = atoi(argv[3]);
    
    if (mode_val < 0 || mode_val > 3) {
        printf("Invalid mode value: %d\n", mode_val);
        return 1;
    }
    
    servo_mode_t mode = (servo_mode_t)mode_val;
    
    if (!servo_manager_set_mode(manager, id, mode)) {
        printf("Failed to set mode for servo %u\n", id);
        return 1;
    }
    
    printf("Set servo %u mode to %s\n", id, servo_mode_to_string(mode));
    return 0;
}

/**
 * @brief Handle servo enable command
 */
static int handle_servo_enable(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: servo enable <id>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    
    if (!servo_manager_enable_servo(manager, id)) {
        printf("Failed to enable servo %u\n", id);
        return 1;
    }
    
    printf("Enabled servo %u\n", id);
    return 0;
}

/**
 * @brief Handle servo disable command
 */
static int handle_servo_disable(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: servo disable <id>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    
    if (!servo_manager_disable_servo(manager, id)) {
        printf("Failed to disable servo %u\n", id);
        return 1;
    }
    
    printf("Disabled servo %u\n", id);
    return 0;
}

/**
 * @brief Handle servo center command
 */
static int handle_servo_center(servo_manager_t manager, int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: servo center <id>\n");
        return 1;
    }
    
    uint id = atoi(argv[2]);
    
    if (!servo_manager_set_position(manager, id, 0.0f)) {
        printf("Failed to center servo %u\n", id);
        return 1;
    }
    
    printf("Centered servo %u\n", id);
    return 0;
}

/**
 * @brief Main servo command handler
 */
int cmd_servo(int argc, char *argv[]) {
    if (argc < 2) {
        print_servo_help();
        return 1;
    }

    // Get or initialize the servo manager
    servo_manager_t manager = ensure_servo_manager();
    if (manager == NULL) {
        return 1;
    }

    // Dispatch to the appropriate command handler
    if (strcmp(argv[1], "create") == 0) {
        return handle_servo_create(manager, argc, argv);
    }
    else if (strcmp(argv[1], "list") == 0) {
        return handle_servo_list(manager);
    }
    else if (strcmp(argv[1], "position") == 0) {
        return handle_servo_position(manager, argc, argv);
    }
    else if (strcmp(argv[1], "percent") == 0) {
        return handle_servo_percent(manager, argc, argv);
    }
    else if (strcmp(argv[1], "pulse") == 0) {
        return handle_servo_pulse(manager, argc, argv);
    }
    else if (strcmp(argv[1], "speed") == 0) {
        return handle_servo_speed(manager, argc, argv);
    }
    else if (strcmp(argv[1], "sweep") == 0) {
        return handle_servo_sweep(manager, argc, argv);
    }
    else if (strcmp(argv[1], "mode") == 0) {
        return handle_servo_mode(manager, argc, argv);
    }
    else if (strcmp(argv[1], "enable") == 0) {
        return handle_servo_enable(manager, argc, argv);
    }
    else if (strcmp(argv[1], "disable") == 0) {
        return handle_servo_disable(manager, argc, argv);
    }
    else if (strcmp(argv[1], "center") == 0) {
        return handle_servo_center(manager, argc, argv);
    }
    else {
        printf("Unknown servo command: %s\n", argv[1]);
        print_servo_help();
        return 1;
    }
}

// Register servo commands with the shell
void register_servo_manager_commands(void) {
    static const shell_command_t servo_command = {
        cmd_servo,
        "servo",
        "Servo management commands",
    };
    
    shell_register_command(&servo_command);
}