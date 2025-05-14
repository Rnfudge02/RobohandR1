/**
* @file sensor_manager_init.c
* @brief Sensor manager initialization and task management implementation
* @author Robert Fudge (rnfudge@mun.ca)
* @date 2025-05-13
*/

#include "bmm350_adapter.h"
#include "sensor_manager.h"
#include "sensor_manager_init.h"
#include "scheduler.h"
#include "i2c_driver.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

// Global sensor manager instance (only accessible from this file)
static sensor_manager_t g_global_sensor_manager = NULL;

// Global I2C driver instance
static i2c_driver_ctx_t* g_i2c_driver = NULL;

// Sensor manager task ID from scheduler
static int g_sensor_task_id = -1;

// Spinlock for init/deinit operations
static spin_lock_t* g_init_lock;
static uint g_init_lock_num;

static void sensor_manager_scheduler_task(void *param);
static bool setup_default_sensors(sensor_manager_t manager);
static i2c_sensor_adapter_t setup_bmm350_sensor(sensor_manager_t manager);

/**
 * @brief Sensor manager task function for the scheduler
 * 
 * This function is registered with the scheduler and called periodically.
 * 
 * @param param Parameters (unused)
 */
__attribute__((section(".time_critical")))
static void sensor_manager_scheduler_task(void *param) {
    (void)param; // Unused parameter
    
    // Call the sensor manager task function
    sensor_manager_t manager = sensor_manager_get_instance();
    if (manager != NULL) {
        // Lock the manager during task execution - this is crucial
        // for preventing race conditions during sensor access
        if (sensor_manager_lock(manager)) {
            // Execute sensor manager task
            sensor_manager_task(manager);
            
            // Release the lock
            sensor_manager_unlock(manager);
        }
    }
}

bool sensor_manager_init(void) {
    // Initialize spinlock
    g_init_lock_num = spin_lock_claim_unused(true);
    if (g_init_lock_num == UINT_MAX) {
        printf("Failed to claim spinlock for sensor manager init\n");
        return false;
    }
    g_init_lock = spin_lock_instance(g_init_lock_num);
    
    // Acquire lock to prevent race conditions during initialization
    uint32_t save = spin_lock_blocking(g_init_lock);
    
    // Check if already initialized
    if (g_global_sensor_manager != NULL) {
        spin_unlock(g_init_lock, save);
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
    i2c_config.use_dma = true;       // Enable DMA for better performance
    
    g_i2c_driver = i2c_driver_init(&i2c_config);
    if (g_i2c_driver == NULL) {
        printf("Failed to initialize I2C driver\n");
        spin_unlock(g_init_lock, save);
        return false;
    }
    
    // Initialize sensor manager
    sensor_manager_config_t sm_config;
    sensor_manager_get_default_config(&sm_config);
    sm_config.i2c_ctx = g_i2c_driver;
    sm_config.task_period_ms = 10;  // 10ms (100Hz)
    
    g_global_sensor_manager = sensor_manager_create(&sm_config);
    if (g_global_sensor_manager == NULL) {
        printf("Failed to create sensor manager\n");
        i2c_driver_deinit(g_i2c_driver);
        g_i2c_driver = NULL;
        spin_unlock(g_init_lock, save);
        return false;
    }
    
    // Set up sensors (placeholder for your specific sensors)
    bool sensor_setup_success = setup_default_sensors(g_global_sensor_manager);
    if (!sensor_setup_success) {
        // Non-fatal warning
        printf("Warning: Some sensors failed to initialize\n");
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
        1,                                // Core 0 for sensor tasks
        TASK_TYPE_PERSISTENT              // Runs indefinitely
    );
    
    if (g_sensor_task_id < 0) {
        printf("Failed to create sensor manager task\n");
        sensor_manager_destroy(g_global_sensor_manager);
        g_global_sensor_manager = NULL;
        i2c_driver_deinit(g_i2c_driver);
        g_i2c_driver = NULL;
        spin_unlock(g_init_lock, save);
        return false;
    }
    
    // Release lock
    spin_unlock(g_init_lock, save);
    
    printf("Sensor manager initialized successfully\n");
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
    i2c_sensor_adapter_t mag_adapter = setup_bmm350_sensor(manager);
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
    bmm350_task_tcb_t* bmm350_tcb = bmm350_adapter_init(g_i2c_driver);
    if (bmm350_tcb == NULL) {
        printf("Failed to initialize BMM350 adapter\n");
        return NULL;
    }
    
    // Create sensor adapter
    i2c_sensor_config_t config = {
        .type = SENSOR_TYPE_MAGNETOMETER,
        .mode = SENSOR_POWER_NORMAL,
        .rate = SENSOR_RATE_NORMAL,
        .int_enabled = true,
        .device_addr = BMM350_I2C_ADSEL_SET_LOW  // Default address
    };
    
    i2c_sensor_adapter_t adapter = i2c_sensor_adapter_create(
        g_i2c_driver,
        &config,
        bmm350_adapter_task,
        bmm350_tcb
    );
    
    if (adapter == NULL) {
        printf("Failed to create sensor adapter for BMM350\n");
        bmm350_adapter_deinit(bmm350_tcb);
        return NULL;
    }
    
    // Add to sensor manager
    if (!sensor_manager_add_sensor(manager, adapter)) {
        printf("Failed to add BMM350 sensor to manager\n");
        i2c_sensor_adapter_destroy(adapter);
        return NULL;
    }
    
    return adapter;
}

__attribute__((section(".time_critical")))
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
    uint32_t save = spin_lock_blocking(g_init_lock);
    
    if (g_global_sensor_manager == NULL) {
        spin_unlock(g_init_lock, save);
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
    spin_unlock(g_init_lock, save);
    
    return true;
}