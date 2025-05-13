/**
* @file sensor_manager.c
* @brief Sensor manager implementation for RTOS-based sensor integration
* @date 2025-05-13
*/

#include "sensor_manager.h"
#include "i2c_sensor_adapter.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <stdlib.h>
#include <string.h>
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
    sensor_manager_callback_t callback;                 // Data callback function
    void* callback_data;                                // User data for callback
    bool is_running;                                   // Whether the manager is running
    
    // Access control fields
    spin_lock_t* access_lock;                          // Lock for thread-safe access
    uint32_t lock_owner;                               // ID of task that acquired the lock (0 = none)
    uint32_t lock_save;                                // Saved state for unlocking
};

// Forward declaration of internal callback function
static void sensor_manager_internal_callback(sensor_type_t type, const sensor_data_t* data, void* user_data);

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
    
    // Initialize the access lock
    uint lock_num = spin_lock_claim_unused(true);
    if (lock_num == -1) {
        free(manager);
        return NULL;
    }
    
    manager->access_lock = spin_lock_instance(lock_num);
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
        printf("Error: Sensor manager is NULL\n");
        return false;
    }
    
    if (type == SENSOR_TYPE_UNKNOWN) {
        printf("Error: Unknown sensor type\n");
        return false;
    }
    
    // Acquire lock
    if (!sensor_manager_lock(manager)) {
        printf("Error: Failed to acquire sensor manager lock\n");
        return false;
    }
    
    bool result = false;
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, start it
            printf("Starting sensor of type %d\n", type);
            if (i2c_sensor_adapter_start(manager->sensors[i].adapter)) {
                manager->sensors[i].is_active = true;
                printf("Sensor started successfully\n");
                result = true;
            } else {
                printf("Failed to start sensor adapter\n");
            }
            break;
        }
    }
    
    // Release lock
    sensor_manager_unlock(manager);
    
    if (!result) {
        printf("Sensor of type %d not found or failed to start\n", type);
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

bool sensor_manager_get_data(sensor_manager_t manager, sensor_type_t type, sensor_data_t* data) {
    if (manager == NULL || type == SENSOR_TYPE_UNKNOWN || data == NULL) {
        return false;
    }
    
    // Find the sensor
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS; i++) {
        if (manager->sensors[i].adapter != NULL &&
            i2c_sensor_adapter_get_type(manager->sensors[i].adapter) == type) {
            // Found the sensor, get data
            return i2c_sensor_adapter_get_data(manager->sensors[i].adapter, data);
        }
    }
    
    // Sensor not found
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
    if (manager->access_lock != NULL) {
        // Ensure no one else has the lock
        if (manager->lock_owner != 0) {
            spin_unlock(manager->access_lock, manager->lock_save);
        }
        
        // Release the lock number back to the system
        // Note: The SDK doesn't have a direct way to release a spin lock instance
        // Best practice is to set it to NULL to prevent further use
        manager->access_lock = NULL;
    }
    
    // Free the manager structure
    free(manager);
    
    return true;
}

int sensor_manager_get_all_statuses(
    sensor_manager_t manager,
    sensor_type_t* types,
    bool* statuses,
    int max_sensors
) {
    if (manager == NULL || types == NULL || statuses == NULL || max_sensors <= 0) {
        return 0;
    }
    
    int count = 0;
    
    // Loop through all sensors
    for (int i = 0; i < SENSOR_MANAGER_MAX_SENSORS && count < max_sensors; i++) {
        if (manager->sensors[i].adapter != NULL) {
            types[count] = i2c_sensor_adapter_get_type(manager->sensors[i].adapter);
            statuses[count] = manager->sensors[i].is_active;
            count++;
        }
    }
    
    return count;
}

// Internal callback function for sensor data
static void sensor_manager_internal_callback(sensor_type_t type, const sensor_data_t* data, void* user_data) {
    sensor_manager_t manager = (sensor_manager_t)user_data;
    
    if (manager == NULL || data == NULL) {
        return;
    }
    
    // Forward the data to the registered callback if any
    if (manager->callback != NULL) {
        manager->callback(type, data, manager->callback_data);
    }
}

// Add to struct sensor_manager_s:
spin_lock_t* access_lock;  // Lock for thread-safe access
uint32_t lock_owner;       // ID of task that acquired the lock (0 = none)

// Implement locking functions:
bool sensor_manager_lock(sensor_manager_t manager) {
    if (manager == NULL || manager->access_lock == NULL) {
        return false;
    }
    
    // Get current task ID for ownership tracking
    int task_id = scheduler_get_current_task();
    
    // Check if we already own the lock
    if (manager->lock_owner == task_id && task_id != 0) {
        // Already locked by us, just return true
        return true;
    }
    
    // Try to acquire the lock
    uint32_t save = spin_lock_blocking(manager->access_lock);
    
    // Store owner and save value
    manager->lock_owner = task_id;
    manager->lock_save = save;
    
    return true;
}

void sensor_manager_unlock(sensor_manager_t manager) {
    if (manager == NULL || manager->access_lock == NULL) {
        return;
    }
    
    // Only unlock if we're the owner
    int task_id = scheduler_get_current_task();
    if (manager->lock_owner == task_id || task_id == 0) {
        // Release the lock
        spin_unlock(manager->access_lock, manager->lock_save);
        manager->lock_owner = 0;
    }
}