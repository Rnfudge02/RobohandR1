/**
* @file bmm350_adapter.h
* @brief BMM350 magnetometer driver adapter for RTOS integration
* @date 2025-05-13
*/

#ifndef BMM350_ADAPTER_H
#define BMM350_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "bmm350.h"        // Include the BMM350 sensor API header.
#include "i2c_driver.h"    // Include the I2C driver header.

/**
 * @brief Task states for the BMM350 sensor task.
 */
typedef enum {
    BMM350_TASK_STATE_IDLE,         // Task is idle.
    BMM350_TASK_STATE_INIT,         // Task is initializing.
    BMM350_TASK_STATE_RUNNING,      // Task is running normally.
    BMM350_TASK_STATE_ERROR,        // Task encountered an error.
    BMM350_TASK_STATE_SUSPENDED     // Task is suspended.
} bmm350_task_state_t;

/**
 * @brief Parameters for the BMM350 sensor task.
 */
__attribute__((aligned(32)))
typedef struct {
    uint32_t sampling_rate_ms;      // Sampling rate in milliseconds.
    uint8_t device_addr;            // I2C device address.
    uint8_t gpio_int_pin;               // Pin for physical interrupts.
    bool use_dma;                   // Whether to use DMA for I2C transfers.
    bool use_hw_interrupt;         // Flag to use hardware interrupt.
    i2c_driver_ctx_t* i2c_ctx;      // I2C driver context.
} bmm350_task_params_t;

/**
 * @brief Task control block for the BMM350 sensor task.
 */
__attribute__((aligned(32)))
typedef struct {
    bmm350_task_state_t state;      // Current task state.
    bmm350_task_params_t params;    // Task parameters.
    struct bmm350_dev dev;          // BMM350 device structure.
    struct bmm350_mag_temp_data mag_data; // Latest magnetometer readings.
    uint32_t last_sample_time;      // Time of last sample.
    uint32_t error_count;           // Error counter.
    void* sensor_adapter;           // Pointer to the appropriate sensor adapter.
    bool data_ready;                // Flag to indicate new data is available.
} bmm350_task_tcb_t;

/**
 * @brief Initialize the BMM350 adapter with default parameters.
 * 
 * @param i2c_ctx Pointer to I2C driver context.
 * @return Pointer to task control block or NULL if initialization failed.
 */
bmm350_task_tcb_t* bmm350_adapter_init(i2c_driver_ctx_t* i2c_ctx);

/**
 * @brief Initialize the BMM350 adapter with custom parameters.
 * 
 * @param params Pointer to task parameters.
 * @return Pointer to task control block or NULL if initialization failed.
 */
bmm350_task_tcb_t* bmm350_adapter_init_with_params(const bmm350_task_params_t* params);

/**
 * @brief Get default BMM350 task parameters.
 * 
 * @param params Pointer to parameter structure to fill.
 */
void bmm350_adapter_get_default_params(bmm350_task_params_t* params);

/**
 * @brief BMM350 task function for RTOS scheduler.
 * 
 * This is the main function to register with your RTOS task scheduler.
 * 
 * @param task_data Pointer to task control block. (bmm350_task_tcb_t*)
 */
__attribute__((section(".time_critical")))
void bmm350_adapter_task(void* task_data);

/**
 * @brief Start the BMM350 sensor task.
 * 
 * @param tcb Pointer to task control block.
 * @return true if started successfully, false otherwise.
 */
bool bmm350_adapter_start(bmm350_task_tcb_t* tcb);

/**
 * @brief Stop the BMM350 sensor task.
 * 
 * @param tcb Pointer to task control block.
 * @return true if stopped successfully, false otherwise.
 */
bool bmm350_adapter_stop(bmm350_task_tcb_t* tcb);

/**
 * @brief Get the latest magnetometer data.
 * 
 * @param tcb Pointer to task control block.
 * @param mag_data Pointer to structure to fill with magnetometer data.
 * @return true if data was successfully retrieved, false otherwise.
 */
__attribute__((section(".time_critical")))
bool bmm350_adapter_get_data(bmm350_task_tcb_t* tcb, struct bmm350_mag_temp_data* mag_data);

/**
 * @brief Set the sensor power mode.
 * 
 * @param tcb Pointer to task control block.
 * @param power_mode Power mode to set. (see BMM350 definitions)
 * @return true if mode was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool bmm350_adapter_set_power_mode(bmm350_task_tcb_t* tcb, uint8_t power_mode);

/**
 * @brief Set the ODR and performance settings
 * 
 * @param tcb Pointer to task control block.
 * @param odr Output data rate setting. (see BMM350 definitions)
 * @param averaging Averaging setting. (see BMM350 definitions)
 * @return true if settings were applied successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool bmm350_adapter_set_odr_performance(bmm350_task_tcb_t* tcb, uint8_t odr, uint8_t averaging);

/**
 * @brief Enable or disable sensor axes.
 * 
 * @param tcb Pointer to task control block.
 * @param x_en X-axis enable setting.
 * @param y_en Y-axis enable setting.
 * @param z_en Z-axis enable setting.
 * @return true if axes were configured successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool bmm350_adapter_enable_axes(bmm350_task_tcb_t* tcb, uint8_t x_en, uint8_t y_en, uint8_t z_en);

/**
 * @brief Configure sensor interrupts.
 * 
 * @param tcb Pointer to task control block.
 * @return true if interrupts were configured successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool bmm350_adapter_configure_interrupt(bmm350_task_tcb_t* tcb);

/**
 * @brief Perform self-test of the sensor.
 * 
 * @param tcb Pointer to task control block.
 * @param result Pointer to self-test result structure.
 * @return true if self-test was performed successfully, false otherwise.
 */
bool bmm350_adapter_self_test(bmm350_task_tcb_t* tcb, struct bmm350_self_test* result);

/**
 * @brief Deinitialize the BMM350 adapter and free resources.
 * 
 * @param tcb Pointer to task control block.
 * @return true if deinitialized successfully, false otherwise.
 */
bool bmm350_adapter_deinit(bmm350_task_tcb_t* tcb);

#ifdef __cplusplus
}
#endif

#endif // BMM350_ADAPTER_H