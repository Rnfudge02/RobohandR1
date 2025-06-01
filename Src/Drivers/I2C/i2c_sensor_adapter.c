/**
* @file i2c_sensor_adapter.c
* @brief Generic I2C sensor adapter implementation for RTOS-based sensor management
* @date 2025-05-13
*/

#include "log_manager.h"
#include "i2c_sensor_adapter.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief I2C sensor adapter structure
 */
struct i2c_sensor_adapter_s {
    i2c_driver_ctx_t* i2c_ctx;         // I2C driver context
    i2c_sensor_config_t config;        // Sensor configuration
    sensor_task_func_t task_func;      // Sensor task function
    void* task_data;                   // Task data (usually a TCB)
    sensor_data_callback_t callback;   // Data callback function
    void* callback_data;               // User data for callback
    sensor_data_t latest_data;         // Latest sensor data
    bool data_ready;                   // Flag indicating new data is available
    bool is_running;                   // Whether the sensor is running
};

i2c_sensor_adapter_t i2c_sensor_adapter_create(
    i2c_driver_ctx_t* i2c_ctx,
    const i2c_sensor_config_t* config,
    sensor_task_func_t task_func,
    void* task_data
) {
    if (i2c_ctx == NULL || config == NULL || task_func == NULL) {
        return NULL;
    }
    
    // Allocate adapter structure
    i2c_sensor_adapter_t adapter = (i2c_sensor_adapter_t)malloc(sizeof(struct i2c_sensor_adapter_s));
    if (adapter == NULL) {
        return NULL;
    }
    
    // Initialize the adapter
    memset(adapter, 0, sizeof(struct i2c_sensor_adapter_s));
    adapter->i2c_ctx = i2c_ctx;
    adapter->config = *config;
    adapter->task_func = task_func;
    adapter->task_data = task_data;
    
    return adapter;
}

bool i2c_sensor_adapter_start(i2c_sensor_adapter_t adapter) {
    if (adapter == NULL || adapter->is_running) {
        return false;
    }
    
    // Mark as running
    adapter->is_running = true;
    
    // Reset data ready flag
    adapter->data_ready = false;
    
    return true;
}

bool i2c_sensor_adapter_stop(i2c_sensor_adapter_t adapter) {
    if (adapter == NULL || !adapter->is_running) {
        return false;
    }
    
    // Mark as not running
    adapter->is_running = false;
    
    return true;
}

bool i2c_sensor_adapter_set_power_mode(i2c_sensor_adapter_t adapter, sensor_power_mode_t mode) {
    if (adapter == NULL) {
        return false;
    }
    
    // Update configuration
    adapter->config.mode = mode;
    
    return true;
}

bool i2c_sensor_adapter_set_rate(i2c_sensor_adapter_t adapter, sensor_rate_t rate) {
    if (adapter == NULL) {
        return false;
    }
    
    // Update configuration
    adapter->config.rate = rate;
    
    return true;
}

bool i2c_sensor_adapter_register_callback(
    i2c_sensor_adapter_t adapter,
    sensor_data_callback_t callback,
    void* user_data
) {
    if (adapter == NULL || callback == NULL) {
        return false;
    }
    
    adapter->callback = callback;
    adapter->callback_data = user_data;
    
    return true;
}

void i2c_sensor_adapter_task_execute(i2c_sensor_adapter_t adapter) {
    if (adapter == NULL || !adapter->is_running || adapter->task_func == NULL) {
        return;
    }
    
    // Execute the sensor task
    adapter->task_func(adapter->task_data);
    
    // Check if new data is available
    if (adapter->data_ready && adapter->callback != NULL) {
        // Call the data callback
        adapter->callback(adapter->config.type, &adapter->latest_data, adapter->callback_data);
        
        // Reset data ready flag
        adapter->data_ready = false;
    }
}

bool i2c_sensor_adapter_get_data(i2c_sensor_adapter_t adapter, sensor_data_t* data) {
    if (adapter == NULL || data == NULL) {
        log_message(LOG_LEVEL_ERROR, "Sensor Adapter", "get_data: Invalid parameters.");
        return false;
    }
    
    log_message(LOG_LEVEL_DEBUG, "Sensor Adapter", "Getting data from adapter (type %d).", adapter->config.type);
    
    // Copy latest data to output structure
    *data = adapter->latest_data;
    
    // Reset data ready flag
    adapter->data_ready = false;
    
    return true;
}

sensor_type_t i2c_sensor_adapter_get_type(i2c_sensor_adapter_t adapter) {
    if (adapter == NULL) {
        return SENSOR_TYPE_UNKNOWN;
    }
    
    return adapter->config.type;
}

bool i2c_sensor_adapter_destroy(i2c_sensor_adapter_t adapter) {
    if (adapter == NULL) {
        return false;
    }
    
    // Stop the adapter if it's running
    if (adapter->is_running) {
        i2c_sensor_adapter_stop(adapter);
    }
    
    // Free the adapter structure
    free(adapter);
    
    return true;
}

// Helper function to update sensor data (to be called from specific sensor adapters)
bool i2c_sensor_adapter_update_data(i2c_sensor_adapter_t adapter, const sensor_data_t* data) {
    if (adapter == NULL || data == NULL) {
        return false;
    }
    
    // Copy data to latest data
    adapter->latest_data = *data;
    
    // Set data ready flag
    adapter->data_ready = true;
    
    return true;
}