/**
* @file spi_driver.c
* @brief Generic SPI driver implementation with DMA support for Raspberry Pi Pico
* @date 2025-05-17
*/

#include "scheduler.h"
#include "spi_driver.h"
#include "spinlock_manager.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include <limits.h>
#include <string.h>
#include <stdlib.h>

// Global context for DMA ISR
static spi_driver_ctx_t* g_spi_dma_ctx = NULL;
static uint spi_lock_num = UINT_MAX;

// DMA interrupt handler
static void spi_driver_dma_handler(void) {
    if (g_spi_dma_ctx == NULL || g_spi_dma_ctx->dma_complete_callback == NULL) {
        return;
    }
    
    // Check if either channel raised the interrupt
    bool tx_complete = dma_hw->ints0 & (1u << g_spi_dma_ctx->dma_tx_channel);
    bool rx_complete = dma_hw->ints0 & (1u << g_spi_dma_ctx->dma_rx_channel);
    
    if (tx_complete || rx_complete) {
        // Clear the interrupt flags
        if (tx_complete) {
            dma_hw->ints0 = 1u << g_spi_dma_ctx->dma_tx_channel;
        }
        if (rx_complete) {
            dma_hw->ints0 = 1u << g_spi_dma_ctx->dma_rx_channel;
        }
        
        // Wait for both channels to complete
        if (tx_complete && rx_complete) {
            // For read operations, we wait for both TX (command) and RX (data) to complete
            g_spi_dma_ctx->dma_complete_callback(g_spi_dma_ctx->dma_user_data);
        }
        else if (tx_complete && g_spi_dma_ctx->dma_rx_channel == (uint8_t)-1) {
            // For write-only operations (no RX DMA), we only wait for TX
            g_spi_dma_ctx->dma_complete_callback(g_spi_dma_ctx->dma_user_data);
        }
    }
}

spi_driver_ctx_t* spi_driver_init(const spi_driver_config_t* config) {
    if (config == NULL) {
        return NULL;
    }

    spi_driver_ctx_t* ctx = (spi_driver_ctx_t*)malloc(sizeof(spi_driver_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    spi_lock_num = hw_spinlock_allocate(SPINLOCK_CAT_SPI, "spi_driver");
    
    // Zero out the context to avoid undefined behavior
    memset(ctx, 0, sizeof(spi_driver_ctx_t));
    
    // Store the SPI instance and CS pin settings
    ctx->spi_inst = config->spi_inst;
    ctx->cs_pin = config->cs_pin;
    ctx->cs_active_low = config->cs_active_low;
    ctx->use_dma = config->use_dma;
    
    // Configure SPI pins
    gpio_set_function(config->sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->mosi_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->miso_pin, GPIO_FUNC_SPI);
    
    // Configure CS pin if specified
    if (config->cs_pin != (uint)-1) {
        gpio_init(config->cs_pin);
        gpio_set_dir(config->cs_pin, GPIO_OUT);
        gpio_put(config->cs_pin, !config->cs_active_low); // Deassert CS initially
    }
    
    // Initialize SPI peripheral with default settings
    spi_init(config->spi_inst, config->baudrate);
    
    // Configure SPI format
    spi_set_format(config->spi_inst, 
                   config->data_bits, 
                   config->cpol, 
                   config->cpha, 
                   config->order);
    
    // Set up DMA if enabled
    if (config->use_dma) {
        // Claim DMA channels
        if (config->dma_tx_channel != (uint8_t)-1) {
            ctx->dma_tx_channel = config->dma_tx_channel;
        } else {
            ctx->dma_tx_channel = (uint8_t) (dma_claim_unused_channel(true) & 0xFF);
        }
        
        if (config->dma_rx_channel != (uint8_t)-1) {
            ctx->dma_rx_channel = config->dma_rx_channel;
        } else {
            ctx->dma_rx_channel = (uint8_t) (dma_claim_unused_channel(true) & 0xFF);
        }
        
        // Check if channel allocation succeeded
        if (ctx->dma_tx_channel == (uint8_t)-1 || ctx->dma_rx_channel == (uint8_t)-1) {
            free(ctx);
            return NULL;
        }
        
        // Set up DMA interrupt handler
        irq_set_exclusive_handler(DMA_IRQ_0, spi_driver_dma_handler);
        irq_set_enabled(DMA_IRQ_0, true);
        
        // Store this context for the ISR
        g_spi_dma_ctx = ctx;
    }
    
    ctx->initialized = true;
    return ctx;
}

void spi_driver_get_default_config(spi_driver_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(spi_driver_config_t));
    
    config->spi_inst = spi0;        // Default to spi0
    config->sck_pin = 18;           // Default SCK pin for spi0
    config->mosi_pin = 19;          // Default MOSI pin for spi0
    config->miso_pin = 16;          // Default MISO pin for spi0
    config->cs_pin = 17;            // Default CS pin for spi0
    config->cs_active_low = true;   // Default CS is active low
    config->baudrate = 1000000;     // Default to 1 MHz
    config->data_bits = 8;          // Default to 8 bits
    config->cpol = SPI_CPOL_0;      // Default to CPOL 0
    config->cpha = SPI_CPHA_0;      // Default to CPHA 0
    config->order = SPI_MSB_FIRST;  // Default to MSB first
    config->use_dma = false;        // Default to not using DMA
    config->dma_tx_channel = -1;    // Auto-allocate
    config->dma_rx_channel = -1;    // Auto-allocate
}

bool spi_driver_select(spi_driver_ctx_t* ctx) {
    if (ctx == NULL || !ctx->initialized || ctx->cs_pin == (uint)-1) {
        return false;
    }
    
    // Assert CS according to active level
    gpio_put(ctx->cs_pin, ctx->cs_active_low ? 0 : 1);
    return true;
}

bool spi_driver_deselect(spi_driver_ctx_t* ctx) {
    if (ctx == NULL || !ctx->initialized || ctx->cs_pin == (uint)-1) {
        return false;
    }
    
    // Deassert CS according to active level
    gpio_put(ctx->cs_pin, ctx->cs_active_low ? 1 : 0);
    return true;
}

bool spi_driver_transfer(spi_driver_ctx_t* ctx, 
                        const uint8_t* tx_data, uint8_t* rx_data, size_t len) {
    if (ctx == NULL || !ctx->initialized || (tx_data == NULL && rx_data == NULL) || len == 0) {
        return false;
    }
    
    // Acquire lock
    uint32_t save = hw_spinlock_acquire(spi_lock_num, scheduler_get_current_task());
    
    // Prepare for transfer
    bool success = true;
    
    // If only transmitting, use write_blocking
    if (tx_data != NULL && rx_data == NULL) {
        int result = spi_write_blocking(ctx->spi_inst, tx_data, len);
        success = (result == len);
    }
    // If only receiving, use read_blocking
    else if (tx_data == NULL && rx_data != NULL) {
        int result = spi_read_blocking(ctx->spi_inst, 0, rx_data, len);
        success = (result == len);
    }
    // If both transmitting and receiving, use write_read_blocking
    else {
        int result = spi_write_read_blocking(ctx->spi_inst, tx_data, rx_data, len);
        success = (result == len);
    }
    
    // Release lock
    hw_spinlock_release(spi_lock_num, save);

    
    return success;
}

bool spi_driver_read_bytes(spi_driver_ctx_t* ctx, 
                          uint8_t reg_addr, uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }

    // Acquire lock
    uint32_t save = hw_spinlock_acquire(spi_lock_num, scheduler_get_current_task());
    
    bool success = false;
    
    // Select the device
    spi_driver_select(ctx);
    
    // Send the register address (with read bit set, typically bit 7)
    // Note: This depends on the specific protocol of the device
    uint8_t read_addr = reg_addr | 0x80; // Common convention for read bit
    int result = spi_write_blocking(ctx->spi_inst, &read_addr, 1);
    
    if (result == 1) {
        // Read the data from the register
        result = spi_read_blocking(ctx->spi_inst, 0, data, len);
        success = (result == len);
    }
    
    // Deselect the device
    spi_driver_deselect(ctx);
    
    // Release lock
    hw_spinlock_release(spi_lock_num, save);

    
    return success;
}

bool spi_driver_write_bytes(spi_driver_ctx_t* ctx,
                           uint8_t reg_addr, const uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }

    // Acquire lock
    uint32_t save = hw_spinlock_acquire(spi_lock_num, scheduler_get_current_task());
    
    bool success = false;
    
    // Prepare buffer: [register address, data...]
    uint8_t* buffer = (uint8_t*)malloc(len + 1);
    if (buffer != NULL) {
        buffer[0] = reg_addr & 0x7F; // Clear read bit (if using bit 7 convention)
        memcpy(buffer + 1, data, len);
        
        // Select the device
        spi_driver_select(ctx);
        
        // Write to device
        int result = spi_write_blocking(ctx->spi_inst, buffer, len + 1);
        success = (result == len + 1);
        
        // Deselect the device
        spi_driver_deselect(ctx);
        
        free(buffer);
    }
    
    // Release lock
    hw_spinlock_release(spi_lock_num, save);

    
    return success;
}

bool spi_driver_transfer_dma(spi_driver_ctx_t* ctx,
                            const uint8_t* tx_data, uint8_t* rx_data, size_t len,
                            void (*callback)(void* user_data), void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma || 
        (tx_data == NULL && rx_data == NULL) || len == 0) {
        return false;
    }
    
    // Store callback information
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    // Configure DMA for transmit if needed
    if (tx_data != NULL) {
        dma_channel_config tx_config = dma_channel_get_default_config(ctx->dma_tx_channel);
        channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
        channel_config_set_read_increment(&tx_config, true);
        channel_config_set_write_increment(&tx_config, false);
        channel_config_set_dreq(&tx_config, spi_get_dreq(ctx->spi_inst, true));
        
        dma_channel_configure(
            ctx->dma_tx_channel,
            &tx_config,
            &spi_get_hw(ctx->spi_inst)->dr,  // Destination: SPI data register
            tx_data,                         // Source: tx_data
            len,                             // Number of transfers
            true                             // Start immediately
        );
        
        // Enable the TX DMA interrupt if we're not using RX
        if (rx_data == NULL) {
            dma_channel_set_irq0_enabled(ctx->dma_tx_channel, true);
        }
    }
    
    // Configure DMA for receive if needed
    if (rx_data != NULL) {
        dma_channel_config rx_config = dma_channel_get_default_config(ctx->dma_rx_channel);
        channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
        channel_config_set_read_increment(&rx_config, false);
        channel_config_set_write_increment(&rx_config, true);
        channel_config_set_dreq(&rx_config, spi_get_dreq(ctx->spi_inst, false));
        
        dma_channel_configure(
            ctx->dma_rx_channel,
            &rx_config,
            rx_data,                         // Destination: rx_data
            &spi_get_hw(ctx->spi_inst)->dr,  // Source: SPI data register
            len,                             // Number of transfers
            true                             // Start immediately
        );
        
        // Enable the RX DMA interrupt
        dma_channel_set_irq0_enabled(ctx->dma_rx_channel, true);
    }
    
    return true;
}

bool spi_driver_read_bytes_dma(spi_driver_ctx_t* ctx,
                              uint8_t reg_addr, uint8_t* data, size_t len,
                              void (*callback)(void* user_data), void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma || data == NULL || len == 0) {
        return false;
    }
    
    // Store callback information
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    // Select the device
    spi_driver_select(ctx);
    
    // First, we need to send the register address (with read bit set)
    uint8_t read_addr = reg_addr | 0x80; // Common convention for read bit
    
    // Wait for SPI to be available
    while (spi_is_busy(ctx->spi_inst));
    
    // Send the address (blocking)
    spi_write_blocking(ctx->spi_inst, &read_addr, 1);
    
    // Now set up DMA for reading data
    dma_channel_config rx_config = dma_channel_get_default_config(ctx->dma_rx_channel);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_dreq(&rx_config, spi_get_dreq(ctx->spi_inst, false));
    
    // Configure and start DMA
    dma_channel_configure(
        ctx->dma_rx_channel,
        &rx_config,
        data,                             // Destination
        &spi_get_hw(ctx->spi_inst)->dr,   // Source
        len,                              // Number of transfers
        true                              // Start immediately
    );
    
    // Configure a transmit DMA for sending dummy bytes while receiving
    dma_channel_config tx_config = dma_channel_get_default_config(ctx->dma_tx_channel);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config, false);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, spi_get_dreq(ctx->spi_inst, true));
    
    // For reading, we'll send repeated dummy bytes (0xFF)
    uint8_t dummy_byte = 0xFF;
    dma_channel_configure(
        ctx->dma_tx_channel,
        &tx_config,
        &spi_get_hw(ctx->spi_inst)->dr,   // Destination
        &dummy_byte,                      // Source: dummy byte
        len,                              // Number of transfers
        true                              // Start immediately
    );
    
    // Enable DMA interrupts
    dma_channel_set_irq0_enabled(ctx->dma_rx_channel, true);
    
    // Note: We don't deselect the device here - that will be done in the callback
    
    return true;
}

bool spi_driver_write_bytes_dma(spi_driver_ctx_t* ctx,
                               uint8_t reg_addr, const uint8_t* data, size_t len,
                               void (*callback)(void* user_data), void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma || data == NULL || len == 0) {
        return false;
    }
    
    // Store callback information
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    // Prepare buffer: [register address, data...]
    uint8_t* buffer = (uint8_t*)malloc(len + 1);
    if (buffer == NULL) {
        return false;
    }
    
    buffer[0] = reg_addr & 0x7F; // Clear read bit (if using bit 7 convention)
    memcpy(buffer + 1, data, len);
    
    // Select the device
    spi_driver_select(ctx);
    
    // Configure DMA for transmit
    dma_channel_config tx_config = dma_channel_get_default_config(ctx->dma_tx_channel);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, spi_get_dreq(ctx->spi_inst, true));
    
    // Configure and start DMA
    dma_channel_configure(
        ctx->dma_tx_channel,
        &tx_config,
        &spi_get_hw(ctx->spi_inst)->dr,   // Destination
        buffer,                          // Source
        len + 1,                         // Number of transfers (including register address)
        true                             // Start immediately
    );
    
    // Enable DMA interrupt
    dma_channel_set_irq0_enabled(ctx->dma_tx_channel, true);
    
    // Note: We don't deselect the device here - that will be done in the callback
    // Note: We don't free the buffer here because DMA is still using it.
    // We'd need to make a callback to free it after DMA completes.
    
    return true;
}

bool spi_driver_set_dma_callback(spi_driver_ctx_t* ctx, 
                                void (*callback)(void* user_data),
                                void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma) {
        return false;
    }
    
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    return true;
}

bool spi_driver_deinit(spi_driver_ctx_t* ctx) {
    if (ctx == NULL || !ctx->initialized) {
        return false;
    }
    
    // Clean up DMA resources if used
    if (ctx->use_dma) {
        dma_channel_set_irq0_enabled(ctx->dma_rx_channel, false);
        dma_channel_set_irq0_enabled(ctx->dma_tx_channel, false);
        
        dma_channel_unclaim(ctx->dma_rx_channel);
        dma_channel_unclaim(ctx->dma_tx_channel);
        
        if (g_spi_dma_ctx == ctx) {
            g_spi_dma_ctx = NULL;
        }
    }
    
    // Reset CS pin to input (if used)
    if (ctx->cs_pin != (uint)-1) {
        gpio_set_dir(ctx->cs_pin, GPIO_IN);
    }
    
    // Free the context
    free(ctx);
    
    return true;
}