/**
* @file sensor_manager.c
* @brief Sensor manager implementation for RTOS-based sensor integration
* @date 2025-05-13
*/

#include "bmm350_adapter.h"

#include "scheduler.h"

#include "i2c_driver.h"
#include "i2c_sensor_adapter.h"

#include "log_manager.h"
#include "sensor_manager.h"
#include "spinlock_manager.h"

#include "usb_shell.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/sync.h"


#include <limits.h>
#include <stdlib.h>
#include <stdio.h>



/**
 * @brief Sensor entry structure
 */
typedef struct {
    i2c_sensor_adapter_t adapter;      // Sensor adapter handle
    bool is_active;                   // Whether the sensor is active
} sensor_entry_t;

/**
 * @brief Sensor manager structure
 */
struct sensor_manager_s {
    i2c_driver_ctx_t* i2c_ctx;                            // I2C driver context
    sensor_entry_t sensors[SENSOR_MANAGER_MAX_SENSORS];   // Array of sensor entries
    uint32_t task_period_ms;                             // Task period in milliseconds
    uint32_t last_execution_time;                       // Time of last task execution
    uint32_t access_lock_num;                              // Lock for thread-safe access
    uint32_t lock_owner;                               // ID of task that acquired the lock (0 = none)
    uint32_t lock_save;                                // Saved state for unlocking
    sensor_manager_callback_t callback;                 // Data callback function
    void* callback_data;                                // User data for callback
    bool is_running;                                   // Whether the manager is running
};

// Global sensor manager instance (only accessible from this file)
static sensor_manager_t g_global_sensor_manager = NULL;

// Global I2C driver instance
static i2c_driver_ctx_t* g_i2c_driver = NULL;

// Sensor manager task ID from scheduler
static int g_sensor_task_id = -1;

// Spinlock for init/deinit operations
static uint32_t g_sensor_lock_num;

static void sensor_manager_scheduler_task(void *param);
static bool setup_default_sensors(sensor_manager_t manager);
static i2c_sensor_adapter_t setup_bmm350_sensor(sensor_manager_t manager);

// Forward declaration of internal callback function
static void sensor_manager_internal_callback(sensor_type_t type, const sensor_data_t* data, void* user_data);
static bool execute_sensor_task_safe(sensor_manager_t manager, i2c_sensor_adapter_t adapter);
static bool sensor_manager_atomic_operation(sensor_manager_t manager, bool (*operation)(sensor_manager_t, void*), void* param);

int cmd_sensor(int argc, char *argv[]);
int cmd_stats_reset(int argc, char *argv[]);

sensor_manager_t sensor_manager_create(const sensor_manager_config_t* config) {
    if (config == NULL || config->i2c_ctx == NULL) {
        return NULL;
    }
    
    // Allocate manager structure
    sensor_manager_t manager = (sensor_manager_t)malloc(sizeof(struct sensor_manager_s));
    if (manager == NULL) {
        return NULL;
    }
    
    // Initialize the manager
    memset(manager, 0, sizeof(struct sensor_manager_s));
    manager->i2c_ctx = config->i2c_ctx;
    manager->task_period_ms = config->task_period_ms;
    manager->is_running = false;
    
    manager->access_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SENSOR, "sensor_manager");
    manager->lock_owner = 0;
    manager->lock_save = 0;
    
    return manager;
}

void sensor_manager_get_default_config(sensor_manager_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(sensor_manager_config_t));
    config->task_period_ms = 10;  // Default to 10ms (100Hz)
}

bool sensor_manager_add_sensor(sensor_manager_t manager, i2c_sensor_adapter_t adapter) {
    if (manager == NULL || adapter == NULL) {
        return false;
    }
    
    // Get sensor type
    sensor_type_t type = i2c_sensor_adapter_get_type(adapter);
    if (type == SENSOR_TYPE_UNKNOWN) {
        return false;
    }
    
    // Find an empty slot or check if the sensor already exists
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        // Check if slot is empty
        if (manager->sensors[i].adapter == NULL) {
            // Found an empty slot, use it
            manager->sensors[i].adapter = adapter;
            manager->sensors[i].is_active = false;
            
            // Register the internal callback
            i2c_sensor_adapter_register_callback(adapter, sensor_manager_internal_callback, manager);
            
            return true;
        }
        
        // Check if sensor of this type already exists
        if (i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Sensor already exists, replace it
            i2c_sensor_adapter_destroy(manager->sensors[i].adapter);
            manager->sensors[i].adapter = adapter;
            manager->sensors[i].is_active = false;
            
            // Register the internal callback
            i2c_sensor_adapter_register_callback(adapter, sensor_manager_internal_callback, manager);
            
            return true;
        }
    }
    
    // No empty slots available
    return false;
}

bool sensor_manager_remove_sensor(sensor_manager_t manager, sensor_type_t type) {
    if (manager == NULL || type == SENSOR_TYPE_UNKNOWN) {
        return false;
    }
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, remove it
            i2c_sensor_adapter_destroy(manager->sensors[i].adapter);
            manager->sensors[i].adapter = NULL;
            manager->sensors[i].is_active = false;
            
            return true;
        }
    }
    
    // Sensor not found
    return false;
}

bool sensor_manager_start_all(sensor_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    bool all_success = true;
    
    // Start all sensors
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL) {
            if (i2c_sensor_adapter_start(manager->sensors[i].adapter)) {
                manager->sensors[i].is_active = true;
            } else {
                all_success = false;
            }
        }
    }
    
    // Mark the manager as running
    manager->is_running = true;
    manager->last_execution_time = to_ms_since_boot(get_absolute_time());
    
    return all_success;
}

bool sensor_manager_stop_all(sensor_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    bool all_success = true;
    
    // Stop all sensors
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL) {
            if (i2c_sensor_adapter_stop(manager->sensors[i].adapter)) {
                manager->sensors[i].is_active = false;
            } else {
                all_success = false;
            }
        }
    }
    
    // Mark the manager as not running
    manager->is_running = false;
    
    return all_success;
}

// In sensor_manager.c - Modify sensor_manager_start_sensor
bool sensor_manager_start_sensor(sensor_manager_t manager, sensor_type_t type) {
    if (manager == NULL) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager", "Sensor manager is NULL.");
        return false;
    }
    
    if (type == SENSOR_TYPE_UNKNOWN) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager", "Unknown sensor type.");
        return false;
    }
    
    // Acquire lock
    if (!sensor_manager_lock(manager)) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager", "Failed to acquire sensor manager lock.");
        return false;
    }
    
    bool result = false;
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, start it
            log_message(LOG_LEVEL_INFO, "Sensor Manager", "Starting sensor of type %d.", type);
            if (i2c_sensor_adapter_start(manager->sensors[i].adapter)) {
                manager->sensors[i].is_active = true;
                log_message(LOG_LEVEL_INFO, "Sensor Manager", "Sensor started successfully.");
                result = true;
            } else {
                log_message(LOG_LEVEL_ERROR, "Sensor Manager", "Failed to start sensor adapter.");
            }
            break;
        }
    }
    
    // Release lock
    sensor_manager_unlock(manager);
    
    if (!result) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager", "Sensor of type %d not found or failed to start.", type);
    }
    
    return result;
}

bool sensor_manager_stop_sensor(sensor_manager_t manager, sensor_type_t type) {
    if (manager == NULL || type == SENSOR_TYPE_UNKNOWN) {
        return false;
    }
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, stop it
            if (i2c_sensor_adapter_stop(manager->sensors[i].adapter)) {
                manager->sensors[i].is_active = false;
                return true;
            } else {
                return false;
            }
        }
    }
    
    // Sensor not found
    return false;
}

bool sensor_manager_set_power_mode(sensor_manager_t manager, sensor_type_t type, sensor_power_mode_t mode) {
    if (manager == NULL || type == SENSOR_TYPE_UNKNOWN) {
        return false;
    }
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, set power mode
            return i2c_sensor_adapter_set_power_mode(manager->sensors[i].adapter, mode);
        }
    }
    
    // Sensor not found
    return false;
}

bool sensor_manager_set_rate(sensor_manager_t manager, sensor_type_t type, sensor_rate_t rate) {
    if (manager == NULL || type == SENSOR_TYPE_UNKNOWN) {
        return false;
    }
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, set rate
            return i2c_sensor_adapter_set_rate(manager->sensors[i].adapter, rate);
        }
    }
    
    // Sensor not found
    return false;
}

bool sensor_manager_register_callback(
    sensor_manager_t manager,
    sensor_manager_callback_t callback,
    void* user_data
) {
    if (manager == NULL || callback == NULL) {
        return false;
    }
    
    manager->callback = callback;
    manager->callback_data = user_data;
    
    return true;
}

// Helper function to check if a sensor slot is valid
static bool is_sensor_valid(const sensor_manager_t manager, int index) {
    return manager->sensors[index].adapter != NULL;
}

// Helper function to get sensor type safely
static sensor_type_t get_sensor_type(const sensor_manager_t manager, int index) {
    return i2c_sensor_adapter_get_type(manager->sensors[index].adapter);
}

// Helper function to attempt data retrieval from a sensor
static bool try_get_sensor_data(const sensor_manager_t manager, int index, sensor_data_t* data) {
    bool result = i2c_sensor_adapter_get_data(manager->sensors[index].adapter, data);
    if (!result) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager", "Failed to get data from sensor adapter");
    }
    return result;
}

bool sensor_manager_get_data(sensor_manager_t manager, sensor_type_t type, sensor_data_t* data) {
    // Early parameter validation
    if (manager == NULL || type == SENSOR_TYPE_UNKNOWN || data == NULL) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager", "get_data failed: Invalid parameters.");
        return false;
    }

    log_message(LOG_LEVEL_DEBUG, "Sensor Manager", "Requested data for sensor type: %d", type);

    // Single loop to find sensor and track if any sensors exist
    int sensor_count = 0;
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (!is_sensor_valid(manager, i)) {
            continue;  // Skip invalid sensors early
        }

        sensor_count++;
        sensor_type_t sensor_type = get_sensor_type(manager, i);
        log_message(LOG_LEVEL_DEBUG, "Sensor Manager", "Found sensor at index %d with type %d", i, sensor_type);

        // Check if this is the sensor we're looking for
        if (sensor_type != type) {
            continue;  // Not the right type, keep looking
        }

        // Found matching sensor - get data and return
        log_message(LOG_LEVEL_DEBUG, "Sensor Manager", "Found matching sensor at index %d, getting data", i);
        return try_get_sensor_data(manager, i, data);
    }

    // Handle cases where we didn't find the sensor
    if (sensor_count == 0) {
        log_message(LOG_LEVEL_WARN, "Sensor Manager", "No sensors registered with manager.");
        return false;
    }

    log_message(LOG_LEVEL_WARN, "Sensor Manager", "No sensor of type %d found among %d registered sensors", type, sensor_count);
    return false;
}

void sensor_manager_task(void* param) {
    sensor_manager_t manager = (sensor_manager_t)param;
    
    if (manager == NULL || !manager->is_running) {
        return;
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Check if it's time to execute the task
    if ((current_time - manager->last_execution_time) >= manager->task_period_ms) {
        // Execute task for each active sensor
        // Note: We don't need another lock here because the scheduler task
        // already acquired the lock before calling this function
        for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
            if (manager->sensors[i].adapter != NULL && manager->sensors[i].is_active) {
                // Execute sensor task
                i2c_sensor_adapter_task_execute(manager->sensors[i].adapter);
            }
        }
        
        // Update last execution time
        manager->last_execution_time = current_time;
    }
}

bool sensor_manager_destroy(sensor_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    // Stop all sensors
    sensor_manager_stop_all(manager);
    
    // Destroy all sensors
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL) {
            i2c_sensor_adapter_destroy(manager->sensors[i].adapter);
            manager->sensors[i].adapter = NULL;
        }
    }
    
    // Release the access lock if it was allocated
    if (manager->access_lock_num != NULL) {
        // Ensure no one else has the lock
        if (manager->lock_owner != 0) {
            hw_spinlock_release(manager->access_lock_num, manager->lock_save);
        }
        
        // Release the lock number back to the system
        // Note: The SDK doesn't have a direct way to release a spin lock instance
        // Best practice is to set it to NULL to prevent further use
        manager->access_lock_num = 0;
    }
    
    // Free the manager structure
    free(manager);
    
    return true;
}

/**
 * @brief Get the status of all sensors with proper locking
 */
int sensor_manager_get_all_statuses(
    sensor_manager_t manager,
    sensor_type_t* types,
    bool* statuses,
    int max_sensors
) {
    if (manager == NULL || types == NULL || statuses == NULL || max_sensors <= 0) {
        return 0;
    }
    
    // Acquire lock
    if (!sensor_manager_lock(manager)) {
        return 0;
    }
    
    int count = 0;
    
    // Loop through all sensors
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (count >= max_sensors) {
            break;
        }

        if (manager->sensors[i].adapter != NULL) {
            types[count] = i2c_sensor_adapter_get_type(manager->sensors[i].adapter);
            statuses[count] = manager->sensors[i].is_active;
            count++;
        }
    }
    
    // Release lock
    sensor_manager_unlock(manager);
    
    return count;
}

// Internal callback function for sensor data
static void sensor_manager_internal_callback(sensor_type_t type, const sensor_data_t* data, void* user_data) {
    sensor_manager_t manager = (sensor_manager_t) user_data;
    
    if (manager == NULL || data == NULL) {
        return;
    }
    
    // Forward the data to the registered callback if any
    if (manager->callback != NULL) {
        manager->callback(type, data, manager->callback_data);
    }
}

// Implement locking functions:
bool sensor_manager_lock(sensor_manager_t manager) {
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

void sensor_manager_unlock(sensor_manager_t manager) {
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

/**
 * @brief Thread-safe wrapper for sensor operations
 * 
 * This function acquires the lock, performs an operation, and releases the lock.
 * 
 * @param manager Sensor manager instance
 * @param operation Function to perform while lock is held
 * @param param Parameter to pass to the operation function
 * @return Return value from the operation function
 */
static bool sensor_manager_atomic_operation(
    sensor_manager_t manager,
    bool (*operation)(sensor_manager_t, void*),
    void* param
) {
    if (manager == NULL || operation == NULL) {
        return false;
    }
    
    // Acquire lock
    if (!sensor_manager_lock(manager)) {
        return false;
    }
    
    // Perform operation
    bool result = operation(manager, param);
    
    // Release lock
    sensor_manager_unlock(manager);
    
    return result;
}

/**
 * @brief Thread-safe wrapper for executing sensor tasks
 * 
 * This function ensures that sensor tasks are executed atomically.
 * 
 * @param manager Sensor manager instance
 * @param adapter Sensor adapter to execute
 * @return true if executed successfully
 */
static bool execute_sensor_task_safe(sensor_manager_t manager, i2c_sensor_adapter_t adapter) {
    if (manager == NULL || adapter == NULL) {
        return false;
    }
    
    // Execute task with lock held
    i2c_sensor_adapter_task_execute(adapter);
    
    return true;
}

/**
 * @brief Get the I2C driver context from the sensor manager
 * 
 * This function is used for debugging purposes to allow direct access
 * to the I2C bus.
 * 
 * @param manager Sensor manager handle
 * @return I2C driver context or NULL if not available
 */
i2c_driver_ctx_t* sensor_manager_get_i2c_context(sensor_manager_t manager) {
    if (manager == NULL) {
        return NULL;
    }
    
    return manager->i2c_ctx;
}

/**
 * @brief Sensor manager task function for the scheduler
 * 
 * This function is registered with the scheduler and called periodically.
 * 
 * @param param Parameters (unused)
 */

static void sensor_manager_scheduler_task(void *param) {
    (void)param; // Unused parameter
    
    // Call the sensor manager task function
    sensor_manager_t manager = sensor_manager_get_instance();
    if ((manager != NULL) && (sensor_manager_lock(manager))) {
        // Execute sensor manager task
        sensor_manager_task(manager);
            
        // Release the lock
        sensor_manager_unlock(manager);
    }
}

bool sensor_manager_init(void) {
    // Initialize spinlock
    g_sensor_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SENSOR, "sensor_manager_init");
    if (g_sensor_lock_num == UINT_MAX) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager Init", "Failed to claim spinlock for sensor manager init.");
        return false;
    }
    
    // Acquire lock to prevent race conditions during initialization
    uint32_t save = hw_spinlock_acquire(g_sensor_lock_num, scheduler_get_current_task());
    
    // Check if already initialized
    if (g_global_sensor_manager != NULL) {
        hw_spinlock_release(g_sensor_lock_num, save);
        return true;  // Already initialized
    }
    
    // Initialize I2C driver
    i2c_driver_config_t i2c_config;
    i2c_driver_get_default_config(&i2c_config);
    
    // Use I2C0 on default pins (GPIO 16, 17)
    i2c_config.i2c_inst = i2c0;
    i2c_config.sda_pin = 16;
    i2c_config.scl_pin = 17;
    i2c_config.clock_freq = 400000;  // 400 kHz
    i2c_config.use_dma = false;       // Enable DMA for better performance
    
    g_i2c_driver = i2c_driver_init(&i2c_config);
    if (g_i2c_driver == NULL) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager Init", "Failed to initialize I2C driver.");
        hw_spinlock_release(g_sensor_lock_num, save);
        return false;
    }
    
    // Initialize sensor manager
    sensor_manager_config_t sm_config;
    sensor_manager_get_default_config(&sm_config);
    sm_config.i2c_ctx = g_i2c_driver;
    sm_config.task_period_ms = 10;  // 10ms (100Hz)
    
    g_global_sensor_manager = sensor_manager_create(&sm_config);
    if (g_global_sensor_manager == NULL) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager Init", "Failed to create sensor manager.");
        i2c_driver_deinit(g_i2c_driver);
        g_i2c_driver = NULL;
        hw_spinlock_release(g_sensor_lock_num, save);
        return false;
    }
    
    // Set up sensors (placeholder for your specific sensors)
    bool sensor_setup_success = setup_default_sensors(g_global_sensor_manager);
    if (!sensor_setup_success) {
        // Non-fatal warning
        log_message(LOG_LEVEL_WARN, "Sensor Manager Init","Some sensors failed to initialize.");
    }
    
    // Create scheduler task
    // Use a dedicated task for sensor management with high priority
    // to ensure consistent sampling
    g_sensor_task_id = scheduler_create_task(
        sensor_manager_scheduler_task,    // Task function
        NULL,                             // No parameters needed
        2048,                             // Stack size (adjust as needed)
        TASK_PRIORITY_HIGH,               // High priority
        "sensor_mgr",                     // Task name
        0,                                // Core 0 for sensor tasks
        TASK_TYPE_PERSISTENT              // Runs indefinitely
    );
    
    if (g_sensor_task_id < 0) {
        log_message(LOG_LEVEL_ERROR, "Sensor Manager Init", "Failed to create sensor manager task.");
        sensor_manager_destroy(g_global_sensor_manager);
        g_global_sensor_manager = NULL;
        i2c_driver_deinit(g_i2c_driver);
        g_i2c_driver = NULL;
        hw_spinlock_release(g_sensor_lock_num, save);
        return false;
    }
    
    // Release lock
    hw_spinlock_release(g_sensor_lock_num, save);
    
    log_message(LOG_LEVEL_INFO, "Sensor Manager Init","Sensor manager initialized successfully.");
    return true;
}

/**
 * @brief Set up default sensors for the application
 * 
 * @param manager Sensor manager instance
 * @return true if at least one sensor was set up successfully
 */
static bool setup_default_sensors(sensor_manager_t manager) {
    if (manager == NULL) {
        return false;
    }
    
    bool any_success = false;
    
    // Set up BMM350 magnetometer
    const i2c_sensor_adapter_t mag_adapter = setup_bmm350_sensor(manager);     // NOSONAR is const?
    if (mag_adapter != NULL) {
        any_success = true;
    }
    
    // Add more sensors as needed
    
    return any_success;
}

/**
 * @brief Set up BMM350 magnetometer sensor
 * 
 * @param manager Sensor manager instance
 * @return Sensor adapter handle or NULL if failed
 */
static i2c_sensor_adapter_t setup_bmm350_sensor(sensor_manager_t manager) {
    // Create BMM350 adapter
    log_message(LOG_LEVEL_INFO, "BMM350 Setup", "Initializing BMM350 adapter...");
    bmm350_task_tcb_t* bmm350_tcb = bmm350_adapter_init(g_i2c_driver);
    if (bmm350_tcb == NULL) {
        log_message(LOG_LEVEL_ERROR, "BMM350 Adapter", "Failed to initialize BMM350 adapter.");
        return NULL;
    }
    log_message(LOG_LEVEL_INFO, "BMM350 Setup", "BMM350 adapter initialized successfully.");
    
    // Create sensor adapter
    i2c_sensor_config_t config = {
        .type = SENSOR_TYPE_MAGNETOMETER,
        .mode = SENSOR_POWER_NORMAL,
        .rate = SENSOR_RATE_NORMAL,
        .int_enabled = true,
        .device_addr = BMM350_I2C_ADSEL_SET_LOW  // Default address
    };
    
    log_message(LOG_LEVEL_INFO, "BMM350 Setup", "Creating sensor adapter with type MAGNETOMETER...");
    i2c_sensor_adapter_t adapter = i2c_sensor_adapter_create(
        g_i2c_driver,
        &config,
        bmm350_adapter_task,
        bmm350_tcb
    );
    
    if (adapter == NULL) {
        log_message(LOG_LEVEL_ERROR, "BMM350 Adapter", "Failed to create sensor adapter for BMM350.");
        bmm350_adapter_deinit(bmm350_tcb);
        return NULL;
    }
    
    // Link the adapter back to the BMM350 TCB so it can update data
    bmm350_tcb->sensor_adapter = adapter;
    
    log_message(LOG_LEVEL_INFO, "BMM350 Setup", "Sensor adapter created successfully.");
    
    // Add to sensor manager
    log_message(LOG_LEVEL_INFO, "BMM350 Setup", "Adding sensor to manager...");
    if (!sensor_manager_add_sensor(manager, adapter)) {
        log_message(LOG_LEVEL_ERROR, "BMM350 Adapter", "Failed to add BMM350 sensor to manager.");
        i2c_sensor_adapter_destroy(adapter);
        return NULL;
    }
    log_message(LOG_LEVEL_INFO, "BMM350 Setup", "BMM350 sensor added to manager successfully.");
    
    return adapter;
}


sensor_manager_t sensor_manager_get_instance(void) {
    return g_global_sensor_manager;
}

/**
 * @brief Deinitialize the sensor manager
 * 
 * @return true if deinitialized successfully
 */
bool sensor_manager_deinit(void) {
    // Acquire lock
    uint32_t save = hw_spinlock_acquire(g_sensor_lock_num, scheduler_get_current_task());
    
    if (g_global_sensor_manager == NULL) {
        hw_spinlock_release(g_sensor_lock_num, save);
        return true;  // Already deinitialized
    }
    
    // Remove scheduler task if it exists
    if (g_sensor_task_id >= 0) {
        scheduler_delete_task(g_sensor_task_id);
        g_sensor_task_id = -1;
    }
    
    // Destroy sensor manager
    sensor_manager_destroy(g_global_sensor_manager);
    g_global_sensor_manager = NULL;
    
    // Deinitialize I2C driver
    if (g_i2c_driver != NULL) {
        i2c_driver_deinit(g_i2c_driver);
        g_i2c_driver = NULL;
    }
    
    // Release lock
    hw_spinlock_release(g_sensor_lock_num, save);
    
    return true;
}

static int sensor_i2c_scan(i2c_driver_ctx_t* i2c_ctx);
static int sensor_i2c_read(i2c_driver_ctx_t* i2c_ctx, uint8_t dev_addr, uint8_t reg_addr, uint8_t len);
static int sensor_i2c_write(i2c_driver_ctx_t* i2c_ctx, uint8_t dev_addr, uint8_t reg_addr, const uint8_t* data, uint8_t len);

// Shell command handler for sensor operations
static void print_sensor_usage(void) {
    printf("Usage: sensor <command> [args...]\n");
    printf("Commands:\n");
    printf("  info      - Show sensor info\n");
    printf("  start     - Start all sensors\n");
    printf("  stop      - Stop all sensors\n");
    printf("  start <type> - Start specific sensor\n");
    printf("  stop <type>  - Stop specific sensor\n");
    printf("  read <type>  - Read data from sensor\n");
    printf("  mode <type> <mode> - Set sensor power mode\n");
    printf("  rate <type> <rate> - Set sensor rate\n");
    printf("I2C debugging:\n");
    printf("  scan      - Scan I2C bus for devices\n");
    printf("  i2c_read <dev_addr> <reg_addr> [len=1] - Read register(s)\n");
    printf("  i2c_write <dev_addr> <reg_addr> <value1> [value2...] - Write register(s)\n");
    printf("  dump <dev_addr> <start_reg> <end_reg> - Dump register range\n");
    printf("where <type> is: mag, accel, gyro, temp, press, hum\n");
    printf("and <mode> is: off, low, normal, high\n");
    printf("and <rate> is: off, low, normal, high, vhigh\n");
}

// Helper function to parse sensor type from command line
static sensor_type_t parse_sensor_type(const char* type_str) {
    if (strcmp(type_str, "mag") == 0) {
        return SENSOR_TYPE_MAGNETOMETER;
    } else if (strcmp(type_str, "accel") == 0) {
        return SENSOR_TYPE_ACCELEROMETER;
    } else if (strcmp(type_str, "gyro") == 0) {
        return SENSOR_TYPE_GYROSCOPE;
    } else if (strcmp(type_str, "temp") == 0) {
        return SENSOR_TYPE_TEMPERATURE;
    } else if (strcmp(type_str, "press") == 0) {
        return SENSOR_TYPE_PRESSURE;
    } else if (strcmp(type_str, "hum") == 0) {
        return SENSOR_TYPE_HUMIDITY;
    }
    
    return SENSOR_TYPE_UNKNOWN;
}

// Helper function to convert sensor type to string
static const char* sensor_type_to_string(sensor_type_t type) {
    switch (type) {
        case SENSOR_TYPE_ACCELEROMETER: return "Accelerometer";
        case SENSOR_TYPE_GYROSCOPE:     return "Gyroscope";
        case SENSOR_TYPE_MAGNETOMETER:  return "Magnetometer";
        case SENSOR_TYPE_PRESSURE:      return "Pressure";
        case SENSOR_TYPE_TEMPERATURE:   return "Temperature";
        case SENSOR_TYPE_HUMIDITY:      return "Humidity";
        case SENSOR_TYPE_LIGHT:         return "Light";
        case SENSOR_TYPE_PROXIMITY:     return "Proximity";
        case SENSOR_TYPE_IMU:           return "IMU";
        case SENSOR_TYPE_ENV:           return "Environmental";
        default:                        return "Unknown";
    }
}

// Command handler for 'info' command
static int handle_sensor_info(sensor_manager_t manager) {
    printf("Sensor Manager Status:\n");
    
    // Get scheduler task info to display task ID
    int task_id = -1;
    for (int i = 0; i < 100; i++) {
        task_control_block_t tcb;
        if ((scheduler_get_task_info(i, &tcb)) && (strcmp(tcb.name, "sensor_mgr") == 0)) {
            task_id = i;
            break;
        }
    }
    
    printf("Task ID: %d\n", task_id);
    
    // Check if sensors are available
    printf("Registered sensors:\n");
    sensor_type_t types[SENSOR_MANAGER_MAX_SENSORS];
    bool statuses[SENSOR_MANAGER_MAX_SENSORS];
    int count = sensor_manager_get_all_statuses(manager, types, statuses, SENSOR_MANAGER_MAX_SENSORS);
    
    if (count == 0) {
        printf("  No sensors found\n");
    } else {
        for (int i = 0; i < count; i++) {
            printf("  - %s: %s\n", 
                sensor_type_to_string(types[i]), 
                statuses[i] ? "Running" : "Stopped");
        }
    }
    
    return 0;
}

// Command handler for 'scan' command
static int handle_sensor_scan(sensor_manager_t manager) {
    i2c_driver_ctx_t* i2c_ctx = sensor_manager_get_i2c_context(manager);
    
    if (i2c_ctx == NULL) {
        printf("Could not access I2C driver context\n");
        return 1;
    }
    
    return sensor_i2c_scan(i2c_ctx);
}

// Command handler for 'i2c_read' command
static int handle_sensor_i2c_read(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: sensor i2c_read <dev_addr> <reg_addr> [len=1]\n");
        return 1;
    }
    
    i2c_driver_ctx_t* i2c_ctx = sensor_manager_get_i2c_context(manager);
    
    if (i2c_ctx == NULL) {
        printf("Could not access I2C driver context\n");
        return 1;
    }
    
    uint8_t dev_addr = (uint8_t)strtol(argv[2], NULL, 0);
    uint8_t reg_addr = (uint8_t)strtol(argv[3], NULL, 0);
    uint8_t len = 1;
    
    if (argc > 4) {
        len = (uint8_t)strtol(argv[4], NULL, 0);
    }
    
    return sensor_i2c_read(i2c_ctx, dev_addr, reg_addr, len);
}

// Command handler for 'i2c_write' command
static int handle_sensor_i2c_write(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc < 5) {
        printf("Usage: sensor i2c_write <dev_addr> <reg_addr> <value1> [value2...]\n");
        return 1;
    }
    
    i2c_driver_ctx_t* i2c_ctx = sensor_manager_get_i2c_context(manager);
    
    if (i2c_ctx == NULL) {
        printf("Could not access I2C driver context\n");
        return 1;
    }
    
    uint8_t dev_addr = (uint8_t)strtol(argv[2], NULL, 0);
    uint8_t reg_addr = (uint8_t)strtol(argv[3], NULL, 0);
    
    uint8_t data[32]; // Maximum 32 bytes
    uint8_t len = 0;
    
    for (int i = 4; i < argc; i++) {
        if (len >= sizeof(data)) {
            break;
        }
        data[len++] = (uint8_t)strtol(argv[i], NULL, 0);
    }
    
    return sensor_i2c_write(i2c_ctx, dev_addr, reg_addr, data, len);
}

// Command handler for 'dump' command
static int handle_sensor_dump(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc < 5) {
        printf("Usage: sensor dump <dev_addr> <start_reg> <end_reg>\n");
        return 1;
    }
    
    i2c_driver_ctx_t* i2c_ctx = sensor_manager_get_i2c_context(manager);
    
    if (i2c_ctx == NULL) {
        printf("Could not access I2C driver context\n");
        return 1;
    }
    
    uint8_t dev_addr = (uint8_t)strtol(argv[2], NULL, 0);
    uint8_t start_reg = (uint8_t)strtol(argv[3], NULL, 0);
    uint8_t end_reg = (uint8_t)strtol(argv[4], NULL, 0);
    
    printf("Register dump for device 0x%02X (registers 0x%02X to 0x%02X):\n", 
           dev_addr, start_reg, end_reg);
    printf("Reg | Value (hex) | Value (dec)\n");
    printf("----+------------+------------\n");
    
    for (uint8_t reg = start_reg; reg <= end_reg; reg++) {
        uint8_t value;
        bool success = i2c_driver_read_bytes(i2c_ctx, dev_addr, reg, &value, 1);
        
        if (success) {
            printf("0x%02X | 0x%02X       | %3d\n", reg, value, value);
        } else {
            printf("0x%02X | -- (read failed) --\n", reg);
        }
    }
    
    return 0;
}

// Command handler for 'start' command
static int handle_sensor_start(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc > 2) {
        // Start specific sensor
        sensor_type_t type = parse_sensor_type(argv[2]);
        if (type == SENSOR_TYPE_UNKNOWN) {
            printf("Unknown sensor type: %s\n", argv[2]);
            return 1;
        }
        
        if (sensor_manager_start_sensor(manager, type)) {
            printf("Started sensor: %s\n", argv[2]);
        } else {
            printf("Failed to start sensor: %s\n", argv[2]);
            return 1;
        }
    } else {
        // Start all sensors
        if (sensor_manager_start_all(manager)) {
            printf("Started all sensors\n");
        } else {
            printf("Failed to start all sensors\n");
            return 1;
        }
    }
    
    return 0;
}

// Command handler for 'stop' command
static int handle_sensor_stop(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc > 2) {
        // Stop specific sensor
        sensor_type_t type = parse_sensor_type(argv[2]);
        if (type == SENSOR_TYPE_UNKNOWN) {
            printf("Unknown sensor type: %s\n", argv[2]);
            return 1;
        }
        
        if (sensor_manager_stop_sensor(manager, type)) {
            printf("Stopped sensor: %s\n", argv[2]);
        } else {
            printf("Failed to stop sensor: %s\n", argv[2]);
            return 1;
        }
    } else {
        // Stop all sensors
        if (sensor_manager_stop_all(manager)) {
            printf("Stopped all sensors\n");
        } else {
            printf("Failed to stop all sensors\n");
            return 1;
        }
    }
    
    return 0;
}

// Command handler for 'read' command
static int handle_sensor_read(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc < 3) {
        printf("Specify sensor type to read\n");
        return 1;
    }
    
    sensor_type_t type = parse_sensor_type(argv[2]);
    if (type == SENSOR_TYPE_UNKNOWN) {
        printf("Unknown sensor type: %s\n", argv[2]);
        return 1;
    }
    
    sensor_data_t data;
    if (sensor_manager_get_data(manager, type, &data)) {
        // Print formatted sensor data
        switch(type) {
            case SENSOR_TYPE_MAGNETOMETER:
                printf("MAG: X: %.2f, Y: %.2f, Z: %.2f\n", 
                    data.xyz.x, data.xyz.y, data.xyz.z);
                break;
            
            case SENSOR_TYPE_ACCELEROMETER:
                printf("ACCEL: X: %.2f, Y: %.2f, Z: %.2f\n", 
                    data.xyz.x, data.xyz.y, data.xyz.z);
                break;
                
            case SENSOR_TYPE_GYROSCOPE:
                printf("GYRO: X: %.2f, Y: %.2f, Z: %.2f\n", 
                    data.xyz.x, data.xyz.y, data.xyz.z);
                break;
                
            case SENSOR_TYPE_TEMPERATURE:
                printf("TEMP: %.2f °C\n", data.scalar.value);
                break;
                
            case SENSOR_TYPE_PRESSURE:
                printf("PRESS: %.2f hPa\n", data.scalar.value);
                break;
                
            case SENSOR_TYPE_HUMIDITY:
                printf("HUM: %.2f %%\n", data.scalar.value);
                break;
                
            default:
                break;
        }
    } else {
        printf("No data available from sensor: %s\n", argv[2]);
        return 1;
    }
    
    return 0;
}

// Parse power mode from string
static sensor_power_mode_t parse_power_mode(const char* mode_str) {
    if (strcmp(mode_str, "off") == 0) {
        return SENSOR_POWER_OFF;
    } else if (strcmp(mode_str, "low") == 0) {
        return SENSOR_POWER_LOW;
    } else if (strcmp(mode_str, "normal") == 0) {
        return SENSOR_POWER_NORMAL;
    } else if (strcmp(mode_str, "high") == 0) {
        return SENSOR_POWER_HIGH;
    }
    
    return SENSOR_POWER_NORMAL; // Default
}

// Command handler for 'mode' command
static int handle_sensor_mode(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc < 4) {
        printf("Specify sensor type and mode\n");
        return 1;
    }
    
    sensor_type_t type = parse_sensor_type(argv[2]);
    if (type == SENSOR_TYPE_UNKNOWN) {
        printf("Unknown sensor type: %s\n", argv[2]);
        return 1;
    }
    
    sensor_power_mode_t mode = parse_power_mode(argv[3]);
    if (mode == SENSOR_POWER_NORMAL && strcmp(argv[3], "normal") != 0) {
        printf("Unknown power mode: %s\n", argv[3]);
        return 1;
    }
    
    if (sensor_manager_set_power_mode(manager, type, mode)) {
        printf("Set power mode for %s to %s\n", argv[2], argv[3]);
    } else {
        printf("Failed to set power mode\n");
        return 1;
    }
    
    return 0;
}

// Parse sensor rate from string
static sensor_rate_t parse_sensor_rate(const char* rate_str) {
    if (strcmp(rate_str, "off") == 0) {
        return SENSOR_RATE_OFF;
    } else if (strcmp(rate_str, "low") == 0) {
        return SENSOR_RATE_LOW;
    } else if (strcmp(rate_str, "normal") == 0) {
        return SENSOR_RATE_NORMAL;
    } else if (strcmp(rate_str, "high") == 0) {
        return SENSOR_RATE_HIGH;
    } else if (strcmp(rate_str, "vhigh") == 0) {
        return SENSOR_RATE_VERY_HIGH;
    }
    
    return SENSOR_RATE_NORMAL; // Default
}

// Command handler for 'rate' command
static int handle_sensor_rate(sensor_manager_t manager, int argc, char* argv[]) {
    if (argc < 4) {
        printf("Specify sensor type and rate\n");
        return 1;
    }
    
    sensor_type_t type = parse_sensor_type(argv[2]);
    if (type == SENSOR_TYPE_UNKNOWN) {
        printf("Unknown sensor type: %s\n", argv[2]);
        return 1;
    }
    
    sensor_rate_t rate = parse_sensor_rate(argv[3]);
    if (rate == SENSOR_RATE_NORMAL && strcmp(argv[3], "normal") != 0) {
        printf("Unknown rate: %s\n", argv[3]);
        return 1;
    }
    
    if (sensor_manager_set_rate(manager, type, rate)) {
        printf("Set rate for %s to %s\n", argv[2], argv[3]);
    } else {
        printf("Failed to set rate\n");
        return 1;
    }
    
    return 0;
}

// Command handler for 'status' command
static int handle_sensor_status(sensor_manager_t manager) {
    sensor_type_t types[SENSOR_MANAGER_MAX_SENSORS];
    bool statuses[SENSOR_MANAGER_MAX_SENSORS];
    
    int count = sensor_manager_get_all_statuses(manager, types, statuses, SENSOR_MANAGER_MAX_SENSORS);
    
    if (count == 0) {
        printf("No sensors found\n");
    } else {
        printf("Sensor Status:\n");
        for (int i = 0; i < count; i++) {
            printf("  %s: %s\n", 
                sensor_type_to_string(types[i]), 
                statuses[i] ? "RUNNING" : "STOPPED");
        }
    }
    
    return 0;
}

// Main sensor command handler with routing to specific handlers
int cmd_sensor(int argc, char *argv[]) {
    if (argc < 2) {
        print_sensor_usage();
        return 1;
    }

    // Get the sensor manager instance
    sensor_manager_t manager = sensor_manager_get_instance();
    if (manager == NULL) {
        printf("Sensor manager not initialized\n");
        return 1;
    }
    
    // Route to appropriate command handler
    if (strcmp(argv[1], "info") == 0) {
        return handle_sensor_info(manager);
    } else if (strcmp(argv[1], "scan") == 0) {
        return handle_sensor_scan(manager);
    } else if (strcmp(argv[1], "i2c_read") == 0) {
        return handle_sensor_i2c_read(manager, argc, argv);
    } else if (strcmp(argv[1], "i2c_write") == 0) {
        return handle_sensor_i2c_write(manager, argc, argv);
    } else if (strcmp(argv[1], "dump") == 0) {
        return handle_sensor_dump(manager, argc, argv);
    } else if (strcmp(argv[1], "start") == 0) {
        return handle_sensor_start(manager, argc, argv);
    } else if (strcmp(argv[1], "stop") == 0) {
        return handle_sensor_stop(manager, argc, argv);
    } else if (strcmp(argv[1], "read") == 0) {
        return handle_sensor_read(manager, argc, argv);
    } else if (strcmp(argv[1], "mode") == 0) {
        return handle_sensor_mode(manager, argc, argv);
    } else if (strcmp(argv[1], "rate") == 0) {
        return handle_sensor_rate(manager, argc, argv);
    } else if (strcmp(argv[1], "status") == 0) {
        return handle_sensor_status(manager);
    } else {
        printf("Unknown command: %s\n", argv[1]);
        print_sensor_usage();
        return 1;
    }
}

// Register sensor commands with the shell
void register_sensor_manager_commands(void) {
    static const shell_command_t sensor_command = {
        cmd_sensor,
        "sensor",
        "Sensor management commands"
    };
    
    shell_register_command(&sensor_command);
}

static int sensor_i2c_scan(i2c_driver_ctx_t* i2c_ctx) {
    if (i2c_ctx == NULL) {
        printf("I2C driver context is NULL\n");
        return 1;
    }
    
    // First do the enhanced scan
    printf("=== I2C Scan ===\n");
    int confirmed_devices = i2c_scan(i2c_ctx);
    
    (void) confirmed_devices;
    
    return 0;
}

// Function to read a register from an I2C device
static int sensor_i2c_read(i2c_driver_ctx_t* i2c_ctx, uint8_t dev_addr, uint8_t reg_addr, uint8_t len) {
    if (i2c_ctx == NULL) {
        printf("I2C driver context is NULL\n");
        return 1;
    }
    
    uint8_t data[32]; // Buffer for read data (max 32 bytes)
    if (len > sizeof(data)) {
        len = sizeof(data);
    }
    
    bool success = i2c_driver_read_bytes(i2c_ctx, dev_addr, reg_addr, data, len);
    
    if (success) {
        printf("Read from device 0x%02X, register 0x%02X: ", dev_addr, reg_addr);
        for (int i = 0; i < len; i++) {
            printf("0x%02X ", data[i]);
        }
        printf("\n");
        
        // Also show as decimal values
        printf("Decimal values: ");
        for (int i = 0; i < len; i++) {
            printf("%d ", data[i]);
        }
        printf("\n");
        
        // Try to interpret as 16-bit values if len is even
        if (len >= 2 && (len % 2) == 0) {
            printf("As 16-bit values: ");
            for (int i = 0; i < len; i += 2) {
                int16_t value = (int16_t) (((data[i+1] << 8) | data[i]) & 0xFFFF); // Assuming little-endian
                printf("%d ", value);
            }
            printf("\n");
        }
    } else {
        printf("Failed to read from device 0x%02X, register 0x%02X\n", dev_addr, reg_addr);
    }
    
    return success ? 0 : 1;
}

// Function to write a value to a register on an I2C device
static int sensor_i2c_write(i2c_driver_ctx_t* i2c_ctx, uint8_t dev_addr, uint8_t reg_addr, const uint8_t* data, uint8_t len) {
    if (i2c_ctx == NULL) {
        printf("I2C driver context is NULL\n");
        return 1;
    }
    
    bool success = i2c_driver_write_bytes(i2c_ctx, dev_addr, reg_addr, data, len);
    
    if (success) {
        printf("Write to device 0x%02X, register 0x%02X: ", dev_addr, reg_addr);
        for (int i = 0; i < len; i++) {
            printf("0x%02X ", data[i]);
        }
        printf("\n");
    } else {
        printf("Failed to write to device 0x%02X, register 0x%02X\n", dev_addr, reg_addr);
    }
    
    return success ? 0 : 1;
}