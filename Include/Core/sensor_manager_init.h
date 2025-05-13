/**
* @file sensor_manager_init.h
* @brief Sensor manager initialization and task management
* @date 2025-05-13
*/

#ifndef SENSOR_MANAGER_INIT_H
#define SENSOR_MANAGER_INIT_H

#include "sensor_manager.h"
#include <stdbool.h>

/**
 * @brief Initialize the sensor manager and register with scheduler
 * 
 * @return true if successful, false otherwise
 */
bool sensor_manager_init(void);

/**
 * @brief Get the global sensor manager instance
 * 
 * @return Sensor manager handle or NULL if not initialized
 */
sensor_manager_t sensor_manager_get_instance(void);

#endif // SENSOR_MANAGER_INIT_H