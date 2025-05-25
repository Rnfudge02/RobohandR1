/**
* @file i2c_driver.h
* @brief Generic I2C driver interface with DMA support for Raspberry Pi Pico.
* @author Robert Fudge
* @date 2025
*/

#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H


#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup i2c_struct I2C Data Structures
 * @{
 */

/**
 * @brief I2C driver configuration structure.
 */
typedef struct {
    uint32_t clock_freq;            // Clock frequency.
    int dma_tx_channel;             // DMA transmit channel (-1 for auto).
    int dma_rx_channel;             // DMA receive channel (-1 for auto).

    i2c_inst_t* i2c_inst;           // I2C hardware instance.

    uint8_t sda_pin;                // SDA pin number.
    uint8_t scl_pin;                // SCL pin number.
    bool use_dma;                   // Whether to use DMA.
    const char* name;               // Name for identification.
} i2c_driver_config_t;

/**
 * @brief I2C driver context structure.
 */
typedef struct i2c_driver_ctx_t {
    uint32_t clock_freq;            // Clock frequency.
    unsigned int dma_rx_channel;            // DMA receive channel.
    unsigned int dma_tx_channel;            // DMA transmit channel.
    unsigned int i2c_lock_num;      // Spinlock number.
    void (*dma_complete_callback)(void* user_data); // DMA completion callback.
    void* dma_user_data;            // User data for DMA callback.

    i2c_inst_t* i2c_inst;           // I2C hardware instance.

    uint8_t scl_pin;                // SCL pin number.
    uint8_t sda_pin;                // SDA pin number.
    bool initialized;               // Whether driver is initialized.
    bool lock_initialized;          // Whether lock is initialized.
    bool use_dma;                   // Whether to use DMA.
    spin_lock_t* i2c_spin_lock;     // Spinlock instance.
    char* name;                     // Name for identification.
} i2c_driver_ctx_t;

/** @} */ // end of i2c_struct group

/**
 * @defgroup i2c_api I2C Application Programming Interface
 * @{
 */

/**
 * @brief Deinitialize the I2C driver and free resources.
 * 
 * @param ctx Pointer to driver context.
 * @return true if successful, false otherwise.
 */
bool i2c_driver_deinit(i2c_driver_ctx_t* ctx);

/**
 * @brief Get default I2C driver configuration.
 * 
 * @param config Pointer to configuration structure to fill.
 */
void i2c_driver_get_default_config(i2c_driver_config_t* config);

/**
 * @brief Initialize the I2C driver.
 * 
 * @param config Pointer to driver configuration.
 * @return Pointer to driver context or NULL if initialization failed.
 */
i2c_driver_ctx_t* i2c_driver_init(const i2c_driver_config_t* config);


bool i2c_driver_probe_address(i2c_driver_ctx_t* ctx, uint8_t addr);

/**
 * @brief Read bytes from an I2C device.
 * 
 * @param ctx Pointer to driver context.
 * @param dev_addr I2C device address.
 * @param reg_addr Register address to read from.
 * @param data Buffer to store read data.
 * @param len Number of bytes to read.
 * @return true if successful, false otherwise.
 */
__attribute__((section(".time_critical")))
bool i2c_driver_read_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr, 
    uint8_t reg_addr, uint8_t* data, size_t len);

/**
 * @brief Read bytes from an I2C device using DMA.
 * 
 * @param ctx Pointer to driver context.
 * @param dev_addr I2C device address.
 * @param reg_addr Register address to read from.
 * @param data Buffer to store read data.
 * @param len Number of bytes to read.
 * @param callback Function to call when DMA transfer completes.
 * @param user_data User data to pass to callback.
 * @return true if the DMA transfer was initiated successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool i2c_driver_read_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
    uint8_t reg_addr, uint8_t* data, size_t len,
    void (*callback)(void* user_data), void* user_data);

/**
 * @brief Set the callback for DMA completion.
 * 
 * @param ctx Pointer to driver context.
 * @param callback Function to call when DMA transfer completes.
 * @param user_data User data to pass to callback.
 * @return true if successful, false otherwise.
 */
bool i2c_driver_set_dma_callback(i2c_driver_ctx_t* ctx, 
    void (*callback)(void* user_data), void* user_data);

/**
 * @brief Write bytes to an I2C device.
 * 
 * @param ctx Pointer to driver context.
 * @param dev_addr I2C device address.
 * @param reg_addr Register address to write to.
 * @param data Data to write.
 * @param len Number of bytes to write.
 * @return true if successful, false otherwise.
 */
__attribute__((section(".time_critical")))
bool i2c_driver_write_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
    uint8_t reg_addr, const uint8_t* data, size_t len);

/**
 * @brief Write bytes to an I2C device using DMA.
 * 
 * @param ctx Pointer to driver context.
 * @param dev_addr I2C device address.
 * @param reg_addr Register address to write to.
 * @param data Data to write.
 * @param len Number of bytes to write.
 * @param callback Function to call when DMA transfer completes.
 * @param user_data User data to pass to callback.
 * @return true if the DMA transfer was initiated successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool i2c_driver_write_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
    uint8_t reg_addr, const uint8_t* data, size_t len,
    void (*callback)(void* user_data), void* user_data);

/** @} */ // end of i2c_api group

/**
 * @brief Enhanced I2C scan with multiple probe methods
 */
__attribute__((section(".time_critical")))
int i2c_scan(i2c_driver_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // I2C_DRIVER_H