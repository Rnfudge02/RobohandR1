/**
* @file spi_sensor_adapter.c
* @brief Generic SPI sensor adapter implementation for RTOS-based sensor management
* @date 2025-05-17
*/

#include "log_manager.h"
#include "spi_sensor_adapter.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief SPI sensor adapter structure
 */
struct spi_sensor_adapter_s {
    spi_driver_ctx_t* spi_ctx;         // SPI driver context
    spi_sensor_config_t config;        // Sensor configuration
    spi_sensor_task_func_t task_func;  // Sensor task function
    void* task_data;                   // Task data (usually a TCB)
    spi_sensor_data_callback_t callback; // Data callback function
    void* callback_data;               // User data for callback
    sensor_data_t latest_data;         // Latest sensor data
    bool data_ready;                   // Flag indicating new data is available
    bool is_running;                   // Whether the sensor is running
    
    // Interrupt related fields
    uint int_pin;                      // GPIO pin for interrupts (if used)
    bool int_enabled;                  // Whether interrupts are enabled
};

spi_sensor_adapter_t spi_sensor_adapter_create(
    spi_driver_ctx_t* spi_ctx,
    const spi_sensor_config_t* config,
    spi_sensor_task_func_t task_func,
    void* task_data
) {
    if (spi_ctx == NULL || config == NULL || task_func == NULL) {
        return NULL;
    }
    
    // Allocate adapter structure
    spi_sensor_adapter_t adapter = (spi_sensor_adapter_t)malloc(sizeof(struct spi_sensor_adapter_s));
    if (adapter == NULL) {
        return NULL;
    }
    
    // Initialize the adapter
    memset(adapter, 0, sizeof(struct spi_sensor_adapter_s));
    adapter->spi_ctx = spi_ctx;
    adapter->config = *config;
    adapter->task_func = task_func;
    adapter->task_data = task_data;
    
    // Initialize interrupt pin if specified
    if (config->int_enabled && config->int_pin != (uint)-1) {
        adapter->int_pin = config->int_pin;
        adapter->int_enabled = true;
        
        // Configure the interrupt pin as input with pull-up
        gpio_init(adapter->int_pin);
        gpio_set_dir(adapter->int_pin, GPIO_IN);
        gpio_pull_up(adapter->int_pin);
        
        // Note: Actual interrupt handling would be done in the task function
    }
    
    return adapter;
}

bool spi_sensor_adapter_start(spi_sensor_adapter_t adapter) {
    if (adapter == NULL || adapter->is_running) {
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "SPI Sensor Adapter", "Starting SPI sensor of type %d", adapter->config.type);
    
    // Mark as running
    adapter->is_running = true;
    
    // Reset data ready flag
    adapter->data_ready = false;
    
    return true;
}

bool spi_sensor_adapter_stop(spi_sensor_adapter_t adapter) {
    if (adapter == NULL || !adapter->is_running) {
        return false;
    }
    
    log_message(LOG_LEVEL_INFO, "SPI Sensor Adapter", "Stopping SPI sensor of type %d", adapter->config.type);
    
    // Mark as not running
    adapter->is_running = false;
    
    return true;
}

bool spi_sensor_adapter_set_power_mode(spi_sensor_adapter_t adapter, sensor_power_mode_t mode) {
    if (adapter == NULL) {
        return false;
    }
    
    // Update configuration
    adapter->config.mode = mode;
    
    log_message(LOG_LEVEL_DEBUG, "SPI Sensor Adapter", "Setting power mode %d for sensor type %d", 
             mode, adapter->config.type);
    
    // Note: The actual power mode setting would be done in the task function
    
    return true;
}

bool spi_sensor_adapter_set_rate(spi_sensor_adapter_t adapter, sensor_rate_t rate) {
    if (adapter == NULL) {
        return false;
    }
    
    // Update configuration
    adapter->config.rate = rate;
    
    log_message(LOG_LEVEL_DEBUG, "SPI Sensor Adapter", "Setting data rate %d for sensor type %d", 
             rate, adapter->config.type);
    
    // Note: The actual rate setting would be done in the task function
    
    return true;
}

bool spi_sensor_adapter_register_callback(
    spi_sensor_adapter_t adapter,
    spi_sensor_data_callback_t callback,
    void* user_data
) {
    if (adapter == NULL || callback == NULL) {
        return false;
    }
    
    adapter->callback = callback;
    adapter->callback_data = user_data;
    
    return true;
}

void spi_sensor_adapter_task_execute(spi_sensor_adapter_t adapter) {
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

bool spi_sensor_adapter_get_data(spi_sensor_adapter_t adapter, sensor_data_t* data) {
    if (adapter == NULL || data == NULL) {
        log_message(LOG_LEVEL_ERROR, "SPI Sensor Adapter", "get_data: Invalid parameters.");
        return false;
    }
    
    log_message(LOG_LEVEL_DEBUG, "SPI Sensor Adapter", "Getting data from SPI adapter (type %d).", adapter->config.type);
    
    // Copy latest data to output structure
    *data = adapter->latest_data;
    
    // Reset data ready flag
    adapter->data_ready = false;
    
    return true;
}

sensor_type_t spi_sensor_adapter_get_type(spi_sensor_adapter_t adapter) {
    if (adapter == NULL) {
        return SENSOR_TYPE_UNKNOWN;
    }
    
    return adapter->config.type;
}

bool spi_sensor_adapter_destroy(spi_sensor_adapter_t adapter) {
    if (adapter == NULL) {
        return false;
    }
    
    // Stop the adapter if it's running
    if (adapter->is_running) {
        spi_sensor_adapter_stop(adapter);
    }
    
    // Disable the interrupt if used
    if (adapter->int_enabled && adapter->int_pin != (uint)-1) {
        // Reset the interrupt pin to input without pull-up
        gpio_set_dir(adapter->int_pin, GPIO_IN);
        gpio_disable_pulls(adapter->int_pin);
    }
    
    // Free the adapter structure
    free(adapter);
    
    return true;
}

// Helper function to update sensor data (to be called from specific sensor adapters)
bool spi_sensor_adapter_update_data(spi_sensor_adapter_t adapter, const sensor_data_t* data) {
    if (adapter == NULL || data == NULL) {
        return false;
    }
    
    // Copy data to latest data
    adapter->latest_data = *data;
    
    // Set data ready flag
    adapter->data_ready = true;
    
    return true;
}