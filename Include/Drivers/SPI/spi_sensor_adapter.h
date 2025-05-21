/**
* @file spi_sensor_adapter.h
* @brief Generic SPI sensor adapter interface for RTOS-based sensor management
* @date 2025-05-17
*/

#ifndef SPI_SENSOR_ADAPTER_H
#define SPI_SENSOR_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "spi_driver.h"
#include "i2c_sensor_adapter.h" // For sensor_type_t and sensor_data_t

/**
 * @brief Generic SPI sensor configuration
 */
typedef struct {
    sensor_type_t type;         // Sensor type
    sensor_power_mode_t mode;   // Power mode
    sensor_rate_t rate;         // Data rate
    bool int_enabled;           // Whether interrupts are enabled
    uint int_pin;               // GPIO pin for interrupt (if used)
    uint8_t device_id;          // Device ID or address
} spi_sensor_config_t;

/**
 * @brief Forward declaration of sensor adapter handle
 */
typedef struct spi_sensor_adapter_s* spi_sensor_adapter_t;

/**
 * @brief Sensor task function type
 */
typedef void (*spi_sensor_task_func_t)(void* task_data);

/**
 * @brief Sensor data callback function type
 */
typedef void (*spi_sensor_data_callback_t)(sensor_type_t type, const sensor_data_t* data, void* user_data);

/**
 * @brief Create a new SPI sensor adapter
 * 
 * @param spi_ctx SPI driver context
 * @param config Sensor configuration
 * @param task_func Sensor task function
 * @param task_data Task data (usually a task control block)
 * @return Handle to the sensor adapter or NULL if creation failed
 */
spi_sensor_adapter_t spi_sensor_adapter_create(
    spi_driver_ctx_t* spi_ctx,
    const spi_sensor_config_t* config,
    spi_sensor_task_func_t task_func,
    void* task_data
);

/**
 * @brief Start the sensor
 * 
 * @param adapter Sensor adapter handle
 * @return true if started successfully, false otherwise
 */
bool spi_sensor_adapter_start(spi_sensor_adapter_t adapter);

/**
 * @brief Stop the sensor
 * 
 * @param adapter Sensor adapter handle
 * @return true if stopped successfully, false otherwise
 */
bool spi_sensor_adapter_stop(spi_sensor_adapter_t adapter);

/**
 * @brief Set sensor power mode
 * 
 * @param adapter Sensor adapter handle
 * @param mode Power mode to set
 * @return true if mode was set successfully, false otherwise
 */
bool spi_sensor_adapter_set_power_mode(spi_sensor_adapter_t adapter, sensor_power_mode_t mode);

/**
 * @brief Set sensor data rate
 * 
 * @param adapter Sensor adapter handle
 * @param rate Data rate to set
 * @return true if rate was set successfully, false otherwise
 */
bool spi_sensor_adapter_set_rate(spi_sensor_adapter_t adapter, sensor_rate_t rate);

/**
 * @brief Register a callback for sensor data
 * 
 * @param adapter Sensor adapter handle
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return true if registered successfully, false otherwise
 */
bool spi_sensor_adapter_register_callback(
    spi_sensor_adapter_t adapter,
    spi_sensor_data_callback_t callback,
    void* user_data
);

/**
 * @brief Execute one iteration of the sensor task
 * 
 * This function should be called periodically by the RTOS scheduler.
 * 
 * @param adapter Sensor adapter handle
 */
void spi_sensor_adapter_task_execute(spi_sensor_adapter_t adapter);

/**
 * @brief Get the latest sensor data
 * 
 * @param adapter Sensor adapter handle
 * @param data Pointer to data structure to fill
 * @return true if data was retrieved successfully, false otherwise
 */
bool spi_sensor_adapter_get_data(spi_sensor_adapter_t adapter, sensor_data_t* data);

/**
 * @brief Get the sensor type
 * 
 * @param adapter Sensor adapter handle
 * @return Sensor type
 */
sensor_type_t spi_sensor_adapter_get_type(spi_sensor_adapter_t adapter);

/**
 * @brief Destroy the sensor adapter and free resources
 * 
 * @param adapter Sensor adapter handle
 * @return true if destroyed successfully, false otherwise
 */
bool spi_sensor_adapter_destroy(spi_sensor_adapter_t adapter);

#ifdef __cplusplus
}
#endif

#endif // SPI_SENSOR_ADAPTER_H