/**
* @file sensor_manager.h
* @brief Sensor manager for RTOS-based sensor integration
* @date 2025-05-13
*/

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "i2c_sensor_adapter.h"
#include "i2c_driver.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup sensor_man_const Sensor Management Configuration Constants
 * @{
 */

/**
 * @brief Maximum number of sensors that can be managed.
 */
#define SENSOR_MANAGER_MAX_SENSORS 8

/** @} */ // end of sensor_man_const group

/**
 * @defgroup sensor_man_struct Sensor Management Data structures
 * @{
 */

/**
 * @brief Sensor manager configuration.
 */
typedef struct {
    uint32_t task_period_ms;           // Task period in milliseconds.
    i2c_driver_ctx_t* i2c_ctx;         // I2C driver context.
} sensor_manager_config_t;

/**
 * @brief Sensor manager handle.
 */
typedef struct sensor_manager_s* sensor_manager_t;

/** @} */ // end of sensor_man_struct group

/**
 * @defgroup sensor_man_api Sensor Manager Application Programming Interface
 * @{
 */

/**
 * @brief Sensor data callback function type.
 */
typedef void (*sensor_manager_callback_t)(sensor_type_t type, const sensor_data_t* data, void* user_data);

/**
 * @brief Add a sensor to the manager.
 * 
 * @param manager Sensor manager handle.
 * @param adapter Sensor adapter handle.
 * @return true if added successfully, false otherwise.
 */
bool sensor_manager_add_sensor(sensor_manager_t manager, i2c_sensor_adapter_t adapter);

/**
 * @brief Create a new sensor manager.
 * 
 * @param config Configuration for the sensor manager.
 * @return Handle to the sensor manager or NULL if creation failed.
 */
sensor_manager_t sensor_manager_create(const sensor_manager_config_t* config);

/**
 * @brief Destroy the sensor manager and free resources.
 * 
 * @param manager Sensor manager handle.
 * @return true if destroyed successfully, false otherwise.
 */
bool sensor_manager_destroy(sensor_manager_t manager);

/**
 * @brief Get the status of all sensors.
 * 
 * @param manager Sensor manager handle.
 * @param types Array to store sensor types.
 * @param statuses Array to store sensor statuses. (true = running, false = stopped)
 * @param max_sensors Maximum number of sensors to retrieve.
 * @return Number of sensors found.
 */
__attribute__((section(".time_critical")))
int sensor_manager_get_all_statuses(sensor_manager_t manager,
    sensor_type_t* types, bool* statuses, int max_sensors);

/**
 * @brief Get the latest data from a specific sensor.
 * 
 * @param manager Sensor manager handle.
 * @param type Sensor type.
 * @param data Pointer to data structure to fill.
 * @return true if data was retrieved successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool sensor_manager_get_data(sensor_manager_t manager, sensor_type_t type, sensor_data_t* data);

/**
 * @brief Get default sensor manager configuration.
 * 
 * @param config Pointer to configuration structure to fill.
 */
void sensor_manager_get_default_config(sensor_manager_config_t* config);

/**
 * @brief Get the I2C driver context for debugging.
 * 
 * @param manager Sensor manager handle.
 * @return I2C driver context or NULL if not available.
 */
__attribute__((section(".time_critical")))
i2c_driver_ctx_t* sensor_manager_get_i2c_context(sensor_manager_t manager);

/**
 * @brief Get the global sensor manager instance.
 * 
 * @return Sensor manager handle or NULL if not initialized.
 */
__attribute__((section(".time_critical")))
sensor_manager_t sensor_manager_get_instance(void);

/**
 * @brief Initialize the sensor manager and register with scheduler.
 * 
 * @return true if successful, false otherwise.
 */
bool sensor_manager_init(void);

/**
 * @brief Lock access to the sensor manager.
 * Prevents concurrent access in multi-threaded environment.
 * 
 * @param manager Sensor manager handle.
 * @return true if lock acquired, false otherwise.
 */
__attribute__((section(".time_critical")))
bool sensor_manager_lock(sensor_manager_t manager);

/**
 * @brief Remove a sensor from the manager.
 * 
 * @param manager Sensor manager handle.
 * @param type Sensor type to remove.
 * @return true if removed successfully, false otherwise.
 */
bool sensor_manager_remove_sensor(sensor_manager_t manager, sensor_type_t type);

/**
 * @brief Register a callback for all sensor data.
 * 
 * @param manager Sensor manager handle.
 * @param callback Callback function.
 * @param user_data User data to pass to callback.
 * @return true if registered successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool sensor_manager_register_callback(sensor_manager_t manager,
    sensor_manager_callback_t callback, void* user_data);

/**
 * @brief Set power mode for a specific sensor.
 * 
 * @param manager Sensor manager handle.
 * @param type Sensor type.
 * @param mode Power mode to set.
 * @return true if mode was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool sensor_manager_set_power_mode(sensor_manager_t manager, sensor_type_t type, sensor_power_mode_t mode);

/**
 * @brief Set data rate for a specific sensor.
 * 
 * @param manager Sensor manager handle.
 * @param type Sensor type.
 * @param rate Data rate to set.
 * @return true if rate was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool sensor_manager_set_rate(sensor_manager_t manager, sensor_type_t type, sensor_rate_t rate);

/**
 * @brief Start all sensors.
 * 
 * @param manager Sensor manager handle.
 * @return true if all sensors started successfully, false otherwise.
 */
bool sensor_manager_start_all(sensor_manager_t manager);

/**
 * @brief Start a specific sensor.
 * 
 * @param manager Sensor manager handle.
 * @param type Sensor type to start.
 * @return true if sensor started successfully, false otherwise.
 */
bool sensor_manager_start_sensor(sensor_manager_t manager, sensor_type_t type);

/**
 * @brief Stop all sensors.
 * 
 * @param manager Sensor manager handle.
 * @return true if all sensors stopped successfully, false otherwise.
 */
bool sensor_manager_stop_all(sensor_manager_t manager);

/**
 * @brief Stop a specific sensor.
 * 
 * @param manager Sensor manager handle.
 * @param type Sensor type to stop.
 * @return true if sensor stopped successfully, false otherwise.
 */
bool sensor_manager_stop_sensor(sensor_manager_t manager, sensor_type_t type);

/**
 * @brief RTOS task function for the sensor manager.
 * 
 * This function should be registered with your RTOS scheduler.
 * 
 * @param param Pointer to sensor manager handle.
 */
__attribute__((section(".time_critical")))
void sensor_manager_task(void* param);

/**
 * @brief Unlock access to the sensor manager.
 * 
 * @param manager Sensor manager handle.
 */
__attribute__((section(".time_critical")))
void sensor_manager_unlock(sensor_manager_t manager);

/** @} */ // end of sensor_man_api group

/**
 * @defgroup sensor_man_cmd Sensor Command Interface
 * @{
 */

/**
 * @brief Sensor manager command handler.
 *
 * @param argc Argument count.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_sensor(int argc, char *argv[]);

/**
 * @brief Register all sensor manager commands with the shell.
 */
void register_sensor_manager_commands(void);

/** @} */ // end of sensor_man_cmd group

#ifdef __cplusplus
}
#endif

#endif // SENSOR_MANAGER_H