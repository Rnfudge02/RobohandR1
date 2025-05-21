/**
* @file spi_driver.h
* @brief Generic SPI driver interface with DMA support for Raspberry Pi Pico
* @date 2025-05-17
*/

#ifndef SPI_DRIVER_H
#define SPI_DRIVER_H

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI driver configuration structure
 */
typedef struct {
    spi_inst_t* spi_inst;      /**< SPI hardware instance (spi0 or spi1) */
    uint sck_pin;              /**< GPIO pin number for SCK */
    uint mosi_pin;             /**< GPIO pin number for MOSI */
    uint miso_pin;             /**< GPIO pin number for MISO */
    uint cs_pin;               /**< GPIO pin number for CS (set to -1 if not used) */
    bool cs_active_low;        /**< Whether CS is active low (true) or active high (false) */
    uint baudrate;             /**< SPI clock frequency in Hz */
    uint data_bits;            /**< Number of data bits (4-16) */
    spi_cpol_t cpol;           /**< Clock polarity (CPOL) */
    spi_cpha_t cpha;           /**< Clock phase (CPHA) */
    spi_order_t order;         /**< Bit order (MSB or LSB first) */
    bool use_dma;              /**< Whether to use DMA for transfers */
    uint8_t dma_tx_channel;    /**< DMA channel for transmit (set to -1 to allocate automatically) */
    uint8_t dma_rx_channel;    /**< DMA channel for receive (set to -1 to allocate automatically) */
} spi_driver_config_t;

/**
 * @brief SPI driver context structure
 */
typedef struct {
    spi_inst_t* spi_inst;      /**< SPI hardware instance */
    uint cs_pin;               /**< Chip select pin */
    bool cs_active_low;        /**< Whether CS is active low */
    bool initialized;          /**< Whether the driver is initialized */
    uint8_t dma_tx_channel;    /**< DMA channel for transmit */
    uint8_t dma_rx_channel;    /**< DMA channel for receive */
    bool use_dma;              /**< Whether DMA is enabled */
    
    // Callback for DMA completion
    void (*dma_complete_callback)(void* user_data);
    void* dma_user_data;       /**< User data for DMA callback */
} spi_driver_ctx_t;

/**
 * @brief Initialize the SPI driver
 * 
 * @param config Pointer to driver configuration
 * @return Pointer to driver context or NULL if initialization failed
 */
spi_driver_ctx_t* spi_driver_init(const spi_driver_config_t* config);

/**
 * @brief Get default SPI driver configuration
 * 
 * @param config Pointer to configuration structure to fill
 */
void spi_driver_get_default_config(spi_driver_config_t* config);

/**
 * @brief Read data from an SPI device
 * 
 * @param ctx Pointer to driver context
 * @param reg_addr Register address to read from
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return true if successful, false otherwise
 */
bool spi_driver_read_bytes(spi_driver_ctx_t* ctx, 
                          uint8_t reg_addr, uint8_t* data, size_t len);

/**
 * @brief Write data to an SPI device
 * 
 * @param ctx Pointer to driver context
 * @param reg_addr Register address to write to
 * @param data Data to write
 * @param len Number of bytes to write
 * @return true if successful, false otherwise
 */
bool spi_driver_write_bytes(spi_driver_ctx_t* ctx,
                           uint8_t reg_addr, const uint8_t* data, size_t len);

/**
 * @brief Read data from an SPI device using DMA
 * 
 * @param ctx Pointer to driver context
 * @param reg_addr Register address to read from
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if the DMA transfer was initiated successfully, false otherwise
 */
bool spi_driver_read_bytes_dma(spi_driver_ctx_t* ctx,
                              uint8_t reg_addr, uint8_t* data, size_t len,
                              void (*callback)(void* user_data), void* user_data);

/**
 * @brief Write data to an SPI device using DMA
 * 
 * @param ctx Pointer to driver context
 * @param reg_addr Register address to write to
 * @param data Data to write
 * @param len Number of bytes to write
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if the DMA transfer was initiated successfully, false otherwise
 */
bool spi_driver_write_bytes_dma(spi_driver_ctx_t* ctx,
                               uint8_t reg_addr, const uint8_t* data, size_t len,
                               void (*callback)(void* user_data), void* user_data);

/**
 * @brief Perform a simultaneous read/write operation (full-duplex)
 * 
 * @param ctx Pointer to driver context
 * @param tx_data Data to write
 * @param rx_data Buffer to store read data
 * @param len Number of bytes to transfer
 * @return true if successful, false otherwise
 */
bool spi_driver_transfer(spi_driver_ctx_t* ctx, 
                        const uint8_t* tx_data, uint8_t* rx_data, size_t len);

/**
 * @brief Perform a simultaneous read/write operation using DMA (full-duplex)
 * 
 * @param ctx Pointer to driver context
 * @param tx_data Data to write
 * @param rx_data Buffer to store read data
 * @param len Number of bytes to transfer
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if the DMA transfer was initiated successfully, false otherwise
 */
bool spi_driver_transfer_dma(spi_driver_ctx_t* ctx,
                            const uint8_t* tx_data, uint8_t* rx_data, size_t len,
                            void (*callback)(void* user_data), void* user_data);

/**
 * @brief Select the SPI device (assert CS)
 * 
 * @param ctx Pointer to driver context
 * @return true if successful, false otherwise
 */
bool spi_driver_select(spi_driver_ctx_t* ctx);

/**
 * @brief Deselect the SPI device (deassert CS)
 * 
 * @param ctx Pointer to driver context
 * @return true if successful, false otherwise
 */
bool spi_driver_deselect(spi_driver_ctx_t* ctx);

/**
 * @brief Set the callback for DMA completion
 * 
 * @param ctx Pointer to driver context
 * @param callback Function to call when DMA transfer completes
 * @param user_data User data to pass to callback
 * @return true if successful, false otherwise
 */
bool spi_driver_set_dma_callback(spi_driver_ctx_t* ctx, 
                                void (*callback)(void* user_data),
                                void* user_data);

/**
 * @brief Deinitialize the SPI driver and free resources
 * 
 * @param ctx Pointer to driver context
 * @return true if successful, false otherwise
 */
bool spi_driver_deinit(spi_driver_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // SPI_DRIVER_H