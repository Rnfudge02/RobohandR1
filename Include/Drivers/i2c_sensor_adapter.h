/**
* @file i2c_sensor_adapter.h
* @brief Generic I2C sensor adapter interface for RTOS-based sensor management
* @date 2025-05-13
*/

#ifndef I2C_SENSOR_ADAPTER_H
#define I2C_SENSOR_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include "i2c_driver.h"

/**
 * @brief Generic sensor type enumeration
 */
typedef enum {
    SENSOR_TYPE_UNKNOWN = 0,
    SENSOR_TYPE_ACCELEROMETER,
    SENSOR_TYPE_GYROSCOPE,
    SENSOR_TYPE_MAGNETOMETER,
    SENSOR_TYPE_PRESSURE,
    SENSOR_TYPE_TEMPERATURE,
    SENSOR_TYPE_HUMIDITY,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_PROXIMITY,
    SENSOR_TYPE_IMU,           // Combined inertial measurement unit
    SENSOR_TYPE_ENV            // Combined environmental sensor
} sensor_type_t;

/**
 * @brief Generic sensor power mode
 */
typedef enum {
    SENSOR_POWER_OFF = 0,
    SENSOR_POWER_LOW,
    SENSOR_POWER_NORMAL,
    SENSOR_POWER_HIGH
} sensor_power_mode_t;

/**
 * @brief Generic sensor data rate
 */
typedef enum {
    SENSOR_RATE_OFF = 0,
    SENSOR_RATE_LOW,       // Low rate (e.g., 1-10 Hz)
    SENSOR_RATE_NORMAL,    // Normal rate (e.g., 25-50 Hz)
    SENSOR_RATE_HIGH,      // High rate (e.g., 100-200 Hz)
    SENSOR_RATE_VERY_HIGH  // Very high rate (>200 Hz)
} sensor_rate_t;

/**
 * @brief Generic sensor configuration
 */
typedef struct {
    sensor_type_t type;         // Sensor type
    sensor_power_mode_t mode;   // Power mode
    sensor_rate_t rate;         // Data rate
    bool int_enabled;           // Whether interrupts are enabled
    uint8_t device_addr;        // I2C device address
} i2c_sensor_config_t;

/**
 * @brief Generic sensor data structure
 * 
 * Union to hold different types of sensor data
 */
typedef union {
    struct {
        float x;
        float y;
        float z;
    } xyz;                      // For accelerometer, gyroscope, magnetometer
    
    struct {
        float temperature;
        float pressure;
        float humidity;
    } environmental;            // For environmental sensors
    
    struct {
        float value;            // Generic single value
    } scalar;
} sensor_data_t;

/**
 * @brief Forward declaration of sensor adapter handle
 */
typedef struct i2c_sensor_adapter_s* i2c_sensor_adapter_t;

/**
 * @brief Sensor task function type
 */
typedef void (*sensor_task_func_t)(void* task_data);

/**
 * @brief Sensor data callback function type
 */
typedef void (*sensor_data_callback_t)(sensor_type_t type, const sensor_data_t* data, void* user_data);

/**
 * @brief Create a new I2C sensor adapter
 * 
 * @param i2c_ctx I2C driver context
 * @param config Sensor configuration
 * @param task_func Sensor task function
 * @param task_data Task data (usually a task control block)
 * @return Handle to the sensor adapter or NULL if creation failed
 */
i2c_sensor_adapter_t i2c_sensor_adapter_create(
    i2c_driver_ctx_t* i2c_ctx,
    const i2c_sensor_config_t* config,
    sensor_task_func_t task_func,
    void* task_data
);

/**
 * @brief Start the sensor
 * 
 * @param adapter Sensor adapter handle
 * @return true if started successfully, false otherwise
 */
bool i2c_sensor_adapter_start(i2c_sensor_adapter_t adapter);

/**
 * @brief Stop the sensor
 * 
 * @param adapter Sensor adapter handle
 * @return true if stopped successfully, false otherwise
 */
bool i2c_sensor_adapter_stop(i2c_sensor_adapter_t adapter);

/**
 * @brief Set sensor power mode
 * 
 * @param adapter Sensor adapter handle
 * @param mode Power mode to set
 * @return true if mode was set successfully, false otherwise
 */
bool i2c_sensor_adapter_set_power_mode(i2c_sensor_adapter_t adapter, sensor_power_mode_t mode);

/**
 * @brief Set sensor data rate
 * 
 * @param adapter Sensor adapter handle
 * @param rate Data rate to set
 * @return true if rate was set successfully, false otherwise
 */
bool i2c_sensor_adapter_set_rate(i2c_sensor_adapter_t adapter, sensor_rate_t rate);

/**
 * @brief Register a callback for sensor data
 * 
 * @param adapter Sensor adapter handle
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return true if registered successfully, false otherwise
 */
bool i2c_sensor_adapter_register_callback(
    i2c_sensor_adapter_t adapter,
    sensor_data_callback_t callback,
    void* user_data
);

/**
 * @brief Execute one iteration of the sensor task
 * 
 * This function should be called periodically by the RTOS scheduler.
 * 
 * @param adapter Sensor adapter handle
 */
void i2c_sensor_adapter_task_execute(i2c_sensor_adapter_t adapter);

/**
 * @brief Get the latest sensor data
 * 
 * @param adapter Sensor adapter handle
 * @param data Pointer to data structure to fill
 * @return true if data was retrieved successfully, false otherwise
 */
bool i2c_sensor_adapter_get_data(i2c_sensor_adapter_t adapter, sensor_data_t* data);

/**
 * @brief Get the sensor type
 * 
 * @param adapter Sensor adapter handle
 * @return Sensor type
 */
sensor_type_t i2c_sensor_adapter_get_type(i2c_sensor_adapter_t adapter);

/**
 * @brief Destroy the sensor adapter and free resources
 * 
 * @param adapter Sensor adapter handle
 * @return true if destroyed successfully, false otherwise
 */
bool i2c_sensor_adapter_destroy(i2c_sensor_adapter_t adapter);

#endif // I2C_SENSOR_ADAPTER_H