/**
* @file i2c_driver.c
* @brief Generic I2C driver implementation with DMA support for Raspberry Pi Pico
* @author Based on Robert Fudge's work
* @date 2025
*/

#include "i2c_driver.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdlib.h>

// Global context for DMA ISR
static i2c_driver_ctx_t* g_i2c_dma_ctx = NULL;

// DMA interrupt handler
static void i2c_driver_dma_handler(void) {
    if (g_i2c_dma_ctx == NULL || g_i2c_dma_ctx->dma_complete_callback == NULL) {
        return;
    }
    
    // Check if this is our channel raising the interrupt
    if (dma_hw->ints0 & (1u << g_i2c_dma_ctx->dma_rx_channel)) {
        // Clear the interrupt
        dma_hw->ints0 = 1u << g_i2c_dma_ctx->dma_rx_channel;
        
        // Call the user callback
        g_i2c_dma_ctx->dma_complete_callback(g_i2c_dma_ctx->dma_user_data);
    }
}

i2c_driver_ctx_t* i2c_driver_init(const i2c_driver_config_t* config) {
    if (config == NULL) {
        return NULL;
    }

    i2c_driver_ctx_t* ctx = (i2c_driver_ctx_t*)malloc(sizeof(i2c_driver_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    
    // Zero out the context to avoid undefined behavior
    memset(ctx, 0, sizeof(i2c_driver_ctx_t));
    
    // Store the I2C instance
    ctx->i2c_inst = config->i2c_inst;
    ctx->use_dma = config->use_dma;
    
    // Configure I2C pins
    gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(config->sda_pin);
    gpio_pull_up(config->scl_pin);
    
    // Initialize I2C peripheral
    i2c_init(config->i2c_inst, config->clock_freq);
    
    // Set up DMA if enabled
    if (config->use_dma) {
        // Claim DMA channels
        if (config->dma_tx_channel != (uint8_t)-1) {
            ctx->dma_tx_channel = config->dma_tx_channel;
        } else {
            ctx->dma_tx_channel = dma_claim_unused_channel(true);
        }
        
        if (config->dma_rx_channel != (uint8_t)-1) {
            ctx->dma_rx_channel = config->dma_rx_channel;
        } else {
            ctx->dma_rx_channel = dma_claim_unused_channel(true);
        }
        
        // Check if channel allocation succeeded
        if (ctx->dma_tx_channel == (uint8_t)-1 || ctx->dma_rx_channel == (uint8_t)-1) {
            free(ctx);
            return NULL;
        }
        
        // Set up DMA interrupt handler
        irq_set_exclusive_handler(DMA_IRQ_0, i2c_driver_dma_handler);
        irq_set_enabled(DMA_IRQ_0, true);
        
        // Store this context for the ISR
        g_i2c_dma_ctx = ctx;
    }
    
    ctx->initialized = true;
    return ctx;
}

void i2c_driver_get_default_config(i2c_driver_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(i2c_driver_config_t));
    
    config->i2c_inst = i2c0;     // Default to i2c0
    config->sda_pin = 16;        // Default SDA pin for i2c0
    config->scl_pin = 17;        // Default SCL pin for i2c0
    config->clock_freq = 400000; // Default to 400 kHz
    config->use_dma = false;     // Default to not using DMA
    config->dma_tx_channel = -1; // Auto-allocate
    config->dma_rx_channel = -1; // Auto-allocate
}

bool i2c_driver_read_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr, 
                          uint8_t reg_addr, uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }
    
    // Set up I2C transfer for register address
    int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, &reg_addr, 1, true);
    if (result != 1) {
        return false;
    }
    
    // Read data from the register
    result = i2c_read_blocking(ctx->i2c_inst, dev_addr, data, len, false);
    return (result == len);
}

bool i2c_driver_write_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
                           uint8_t reg_addr, const uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }
    
    // Prepare buffer: [register address, data...]
    uint8_t* buffer = (uint8_t*)malloc(len + 1);
    if (buffer == NULL) {
        return false;
    }
    
    buffer[0] = reg_addr;
    memcpy(buffer + 1, data, len);
    
    // Write to device
    int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, buffer, len + 1, false);
    free(buffer);
    
    return (result == len + 1);
}

bool i2c_driver_read_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
                              uint8_t reg_addr, uint8_t* data, size_t len,
                              void (*callback)(void* user_data), void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma || data == NULL || len == 0) {
        return false;
    }
    
    // Store callback information
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    // First, we need to write the register address (this can't easily be done with DMA)
    int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, &reg_addr, 1, true);
    if (result != 1) {
        return false;
    }
    
    // Configure DMA for reading
    dma_channel_config c = dma_channel_get_default_config(ctx->dma_rx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, i2c_get_dreq(ctx->i2c_inst, false));
    
    // Set up the DMA to read from the I2C data register to our buffer
    dma_channel_configure(
        ctx->dma_rx_channel,
        &c,
        data,                               // Destination
        &i2c_get_hw(ctx->i2c_inst)->data_cmd, // Source
        len,                                // Number of transfers
        true                                // Start immediately
    );
    
    // Enable the DMA interrupt
    dma_channel_set_irq0_enabled(ctx->dma_rx_channel, true);
    
    return true;
}

bool i2c_driver_write_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
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
    
    buffer[0] = reg_addr;
    memcpy(buffer + 1, data, len);
    
    // Configure the I2C for transmit
    i2c_get_hw(ctx->i2c_inst)->enable = 0;
    i2c_get_hw(ctx->i2c_inst)->tar = dev_addr;
    i2c_get_hw(ctx->i2c_inst)->enable = 1;
    
    // Configure DMA for writing
    dma_channel_config c = dma_channel_get_default_config(ctx->dma_tx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, i2c_get_dreq(ctx->i2c_inst, true));
    
    // Set up the DMA to write from our buffer to the I2C data register
    dma_channel_configure(
        ctx->dma_tx_channel,
        &c,
        &i2c_get_hw(ctx->i2c_inst)->data_cmd, // Destination
        buffer,                             // Source
        len + 1,                            // Number of transfers
        true                                // Start immediately
    );
    
    // Enable the DMA interrupt
    dma_channel_set_irq0_enabled(ctx->dma_tx_channel, true);
    
    // Note: We can't free the buffer here because DMA is still using it.
    // We'd need to make a callback to free it after DMA completes.
    
    return true;
}

bool i2c_driver_set_dma_callback(i2c_driver_ctx_t* ctx, 
                                void (*callback)(void* user_data),
                                void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma) {
        return false;
    }
    
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    return true;
}

bool i2c_driver_deinit(i2c_driver_ctx_t* ctx) {
    if (ctx == NULL || !ctx->initialized) {
        return false;
    }
    
    // Clean up DMA resources if used
    if (ctx->use_dma) {
        dma_channel_set_irq0_enabled(ctx->dma_rx_channel, false);
        dma_channel_set_irq0_enabled(ctx->dma_tx_channel, false);
        
        dma_channel_unclaim(ctx->dma_rx_channel);
        dma_channel_unclaim(ctx->dma_tx_channel);
        
        if (g_i2c_dma_ctx == ctx) {
            g_i2c_dma_ctx = NULL;
        }
    }
    
    // Free the context
    free(ctx);
    
    return true;
}