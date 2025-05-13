/**
* @file sensor_manager_init.c
* @brief Sensor manager initialization and task management
* @date 2025-05-13
*/

#include "sensor_manager.h"
#include "i2c_driver.h"
#include "scheduler.h"
#include "bmm350_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global sensor manager instance
sensor_manager_t g_sensor_manager = NULL;

// I2C driver instance
i2c_driver_ctx_t* g_i2c_driver = NULL;

// Task ID for sensor manager task
int g_sensor_task_id = -1;

// BMM350 task control block
static bmm350_task_tcb_t* g_bmm350_tcb = NULL;

// Forward declaration of the task function
static void sensor_manager_scheduler_task(void *params);

// Sensor data callback function
static void sensor_data_callback(sensor_type_t type, const sensor_data_t* data, void* user_data) {
    (void)user_data;
    
    // Print sensor data based on type (uncomment for debugging)
    /*
    switch(type) {
        case SENSOR_TYPE_MAGNETOMETER:
            printf("[Sensor] MAG: X: %.2f, Y: %.2f, Z: %.2f\n", 
                   data->xyz.x, data->xyz.y, data->xyz.z);
            break;
        
        case SENSOR_TYPE_ACCELEROMETER:
            printf("[Sensor] ACCEL: X: %.2f, Y: %.2f, Z: %.2f\n", 
                   data->xyz.x, data->xyz.y, data->xyz.z);
            break;
            
        case SENSOR_TYPE_GYROSCOPE:
            printf("[Sensor] GYRO: X: %.2f, Y: %.2f, Z: %.2f\n", 
                   data->xyz.x, data->xyz.y, data->xyz.z);
            break;
            
        case SENSOR_TYPE_TEMPERATURE:
            printf("[Sensor] TEMP: %.2f °C\n", data->scalar.value);
            break;
            
        case SENSOR_TYPE_PRESSURE:
            printf("[Sensor] PRESS: %.2f hPa\n", data->scalar.value);
            break;
            
        case SENSOR_TYPE_HUMIDITY:
            printf("[Sensor] HUM: %.2f %%\n", data->scalar.value);
            break;
            
        default:
            printf("[Sensor] Unknown sensor type: %d\n", type);
            break;
    }
    */
}

// Scheduler task function for sensor manager
static void sensor_manager_scheduler_task(void *params) {
    (void)params;
    
    if (g_sensor_manager != NULL) {
        sensor_manager_task(g_sensor_manager);
    }
}

// Initialize the sensor manager and register with scheduler
bool sensor_manager_init(void) {
    printf("Initializing sensor manager...\n");
    
    // Create I2C driver
    i2c_driver_config_t i2c_config;
    i2c_driver_get_default_config(&i2c_config);
    i2c_config.sda_pin = 16;  // Set according to your hardware
    i2c_config.scl_pin = 17;  // Set according to your hardware
    
    g_i2c_driver = i2c_driver_init(&i2c_config);
    if (g_i2c_driver == NULL) {
        printf("Failed to initialize I2C driver\n");
        return false;
    }
    
    printf("I2C driver initialized successfully\n");
    
    // Create sensor manager
    sensor_manager_config_t sm_config;
    sensor_manager_get_default_config(&sm_config);
    sm_config.i2c_ctx = g_i2c_driver;
    sm_config.task_period_ms = 20;  // 50Hz task rate
    
    g_sensor_manager = sensor_manager_create(&sm_config);
    if (g_sensor_manager == NULL) {
        printf("Failed to create sensor manager\n");
        return false;
    }
    
    printf("Sensor manager created successfully\n");
    
    // Register callback for sensor data
    sensor_manager_register_callback(g_sensor_manager, sensor_data_callback, NULL);
    
    // Initialize BMM350 magnetometer
    bmm350_task_params_t bmm_params;
    bmm350_adapter_get_default_params(&bmm_params);
    bmm_params.i2c_ctx = g_i2c_driver;
    
    g_bmm350_tcb = bmm350_adapter_init_with_params(&bmm_params);
    if (g_bmm350_tcb == NULL) {
        printf("Failed to initialize BMM350 adapter\n");
        // Continue anyway, as other sensors might work
    } else {
        printf("BMM350 adapter initialized successfully\n");
        
        // Create sensor adapter for BMM350
        i2c_sensor_config_t sensor_config = {
            .type = SENSOR_TYPE_MAGNETOMETER,
            .mode = SENSOR_POWER_NORMAL,
            .rate = SENSOR_RATE_NORMAL,
            .int_enabled = true,
            .device_addr = bmm_params.device_addr
        };
        
        i2c_sensor_adapter_t mag_adapter = i2c_sensor_adapter_create(
            g_i2c_driver,
            &sensor_config,
            bmm350_adapter_task,
            g_bmm350_tcb
        );
        
        if (mag_adapter == NULL) {
            printf("Failed to create magnetometer adapter\n");
            // Continue anyway, as other sensors might work
        } else {
            printf("Magnetometer adapter created successfully\n");
            
            // Add sensor to manager
            if (!sensor_manager_add_sensor(g_sensor_manager, mag_adapter)) {
                printf("Failed to add magnetometer to sensor manager\n");
                // Continue anyway, as other sensors might work
            } else {
                printf("Magnetometer added to sensor manager successfully\n");
            }
        }
    }
    
    // Create a task for the sensor manager
    g_sensor_task_id = scheduler_create_task(
        sensor_manager_scheduler_task,
        NULL,
        0,  // Default stack size
        TASK_PRIORITY_HIGH,  // Sensors need regular polling
        "sensor_mgr",
        0,  // Core 0
        TASK_TYPE_PERSISTENT
    );
    
    if (g_sensor_task_id < 0) {
        printf("Failed to create sensor manager task\n");
        return false;
    }
    
    printf("Sensor manager task created successfully (ID: %d)\n", g_sensor_task_id);
    
    
    return true;
}

// Get the global sensor manager instance
sensor_manager_t sensor_manager_get_instance(void) {
    return g_sensor_manager;
}