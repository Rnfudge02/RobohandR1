/**
* @file i2c_driver.h
* @brief Generic I2C driver interface with DMA support for Raspberry Pi Pico
* @author Based on Robert Fudge's work
* @date 2025
*/

#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C driver configuration structure
 */
typedef struct {
    i2c_inst_t* i2c_inst;     /**< I2C hardware instance (i2c0 or i2c1) */
    uint sda_pin;             /**< GPIO pin number for SDA */
    uint scl_pin;             /**< GPIO pin number for SCL */
    uint clock_freq;          /**< I2C clock frequency in Hz */
    bool use_dma;             /**< Whether to use DMA for transfers */
    uint8_t dma_tx_channel;   /**< DMA channel for transmit (set to -1 to allocate automatically) */
    uint8_t dma_rx_channel;   /**< DMA channel for receive (set to -1 to allocate automatically) */
} i2c_driver_config_t;

/**
 * @brief I2C driver context structure
 */
typedef struct {
    i2c_inst_t* i2c_inst;     /**< I2C hardware instance */
    bool initialized;         /**< Whether the driver is initialized */
    uint8_t dma_tx_channel;   /**< DMA channel for transmit */
    uint8_t dma_rx_channel;   /**< DMA channel for receive */
    bool use_dma;             /**< Whether DMA is enabled */
    
    // Callback for DMA completion
    void (*dma_complete_callback)(void* user_data);
    void* dma_user_data;      /**< User data for DMA callback */
} i2c_driver_ctx_t;

/**
 * @brief Initialize the I2C driver
 * 
 * @param config Pointer to driver configuration
 * @return Pointer to driver context or NULL if initialization failed
 */
i2c_driver_ctx_t* i2c_driver_init(const i2c_driver_config_t* config);

/**
 * @brief Get default I2C driver configuration
 * 
 * @param config Pointer to configuration structure to fill
 */
void i2c_driver_get_default_config(i2c_driver_config_t* config);

/**
 * @brief Read bytes from an I2C device
 * 
 * @param ctx Pointer to driver context
 * @param dev_addr I2C device address
 * @param reg_addr Register address to read from
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return true if successful, false otherwise
 */
bool i2c_driver_read_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr, 
                          uint8_t reg_addr, uint8_t* data, size_t len);

/**
 * @brief Write bytes to an I2C device
 * 
 * @param ctx Pointer to driver context
 * @param dev_addr I2C device address
 * @param reg_addr Register address to write to
 * @param data Data to write
 * @param len Number of bytes to write
 * @return true if successful, false otherwise
 */
bool i2c_driver_write_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
                           uint8_t reg_addr, const uint8_t* data, size_t len);

/**
 * @brief Read bytes from an I2C device using DMA
 * 
 * @param ctx Pointer to driver context
 * @param dev_addr I2C device address
 * @param reg_addr Register address to read from
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if the DMA transfer was initiated successfully, false otherwise
 */
bool i2c_driver_read_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
                              uint8_t reg_addr, uint8_t* data, size_t len,
                              void (*callback)(void* user_data), void* user_data);

/**
 * @brief Write bytes to an I2C device using DMA
 * 
 * @param ctx Pointer to driver context
 * @param dev_addr I2C device address
 * @param reg_addr Register address to write to
 * @param data Data to write
 * @param len Number of bytes to write
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if the DMA transfer was initiated successfully, false otherwise
 */
bool i2c_driver_write_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
                               uint8_t reg_addr, const uint8_t* data, size_t len,
                               void (*callback)(void* user_data), void* user_data);

/**
 * @brief Set the callback for DMA completion
 * 
 * @param ctx Pointer to driver context
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if successful, false otherwise
 */
bool i2c_driver_set_dma_callback(i2c_driver_ctx_t* ctx, 
                                void (*callback)(void* user_data),
                                void* user_data);

/**
 * @brief Deinitialize the I2C driver and free resources
 * 
 * @param ctx Pointer to driver context
 * @return true if successful, false otherwise
 */
bool i2c_driver_deinit(i2c_driver_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // I2C_DRIVER_H