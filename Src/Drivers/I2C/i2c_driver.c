/**
* @file i2c_driver.c
* @brief Generic I2C driver implementation with DMA support for Raspberry Pi Pico
* @author Based on Robert Fudge's work
* @date 2025
*/


#include <stdio.h>      // For printf
#include "log_manager.h" // For LOG_INFO
#include "scheduler.h"   // For scheduler_get_current_task
#include "i2c_driver.h"
#include "spinlock_manager.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include <limits.h>
#include <string.h>
#include <stdlib.h>


// Global context for DMA ISR
static i2c_driver_ctx_t* g_i2c_dma_ctx = NULL;

static uint i2c_lock_num = UINT_MAX;

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

static bool i2c_spinlock_callback(spinlock_init_phase_t phase, void* context) {
    const i2c_driver_ctx_t* ctx = (i2c_driver_ctx_t*)context;
    if (ctx == NULL) {
        return false;
    }
    
    switch (phase) {
        case SPINLOCK_INIT_PHASE_CORE:
            // Already initialized with a bootstrap spinlock
            return true;
            
        case SPINLOCK_INIT_PHASE_TRACKING:
            // Register the spinlock
            hw_spinlock_register_external(ctx->i2c_lock_num, SPINLOCK_CAT_I2C, ctx->name ? ctx->name : "i2c_driver");
            printf("INFO: I2C Driver - Spinlock registered with manager\n");
            return true;
            
        case SPINLOCK_INIT_PHASE_FULL:
            // Now we can use proper logging
            LOG_INFO("I2C Driver", "I2C driver spinlock fully integrated with manager");
            return true;
            
        default:
            return false;
    }
}

/**
 * @brief Initialize I2C driver with thread safety
 */
i2c_driver_ctx_t* i2c_driver_init(const i2c_driver_config_t* config) {
    if (config == NULL || config->i2c_inst == NULL) {
        return NULL;
    }
    
    // Allocate driver context
    i2c_driver_ctx_t* ctx = (i2c_driver_ctx_t*)malloc(sizeof(struct i2c_driver_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    
    // Initialize context
    memset(ctx, 0, sizeof(struct i2c_driver_ctx_t));
    ctx->i2c_inst = config->i2c_inst;
    ctx->sda_pin = config->sda_pin;
    ctx->scl_pin = config->scl_pin;
    ctx->clock_freq = config->clock_freq > 0 ? config->clock_freq : 100000;
    ctx->use_dma = config->use_dma;
    ctx->name = config->name ? strdup(config->name) : NULL;
    
    // Initialize hardware
    i2c_init(ctx->i2c_inst, ctx->clock_freq);
    
    // Configure pins
    gpio_set_function(ctx->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(ctx->scl_pin, GPIO_FUNC_I2C);
    
    // Enable pull-ups (this is generally recommended for I2C)
    gpio_pull_up(ctx->sda_pin);
    gpio_pull_up(ctx->scl_pin);
    
    // Create a spinlock - bootstrap it
    ctx->i2c_lock_num = hw_spinlock_bootstrap_claim(true);
    if (ctx->i2c_lock_num == UINT_MAX) {
        printf("WARNING: I2C driver failed to claim spinlock, thread safety disabled\n");
        ctx->lock_initialized = false;
    } else {
        ctx->i2c_spin_lock = spin_lock_instance(ctx->i2c_lock_num);
        ctx->lock_initialized = true;
    }
    
    // Register with spinlock manager if it's initialized
    if (hw_spinlock_get_init_phase() != SPINLOCK_INIT_PHASE_NONE) {
        i2c_spinlock_callback(hw_spinlock_get_init_phase(), ctx);
    } else {
        // Register for future phases
        hw_spinlock_register_component(ctx->name ? ctx->name : "I2CDriver", 
                                      i2c_spinlock_callback, ctx);
    }
    
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

/**
 * @brief Thread-safe I2C read operation
 */
bool i2c_driver_read_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr, 
    uint8_t reg_addr, uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }

    // Acquire lock
    uint32_t save = hw_spinlock_acquire(i2c_lock_num, scheduler_get_current_task());

    // Set up I2C transfer for register address
    int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, &reg_addr, 1, true);
    bool success = false;

    if (result == 1) {
        // Read data from the register
        result = i2c_read_blocking(ctx->i2c_inst, dev_addr, data, len, false);
        success = (result == len);
    }

    // Release lock
    hw_spinlock_release(i2c_lock_num, save);

    return success;
}

/**
 * @brief Thread-safe I2C write operation
 */
bool i2c_driver_write_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
    uint8_t reg_addr, const uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }

    // Acquire lock
    uint32_t save = hw_spinlock_acquire(i2c_lock_num, scheduler_get_current_task());

    // Prepare buffer: [register address, data...]
    uint8_t* buffer = (uint8_t*)malloc(len + 1);
    bool success = false;

    if (buffer != NULL) {
        buffer[0] = reg_addr;
        memcpy(buffer + 1, data, len);

        // Write to device
        int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, buffer, len + 1, false);
        success = (result == len + 1);

        free(buffer);
    }

    // Release lock
    hw_spinlock_release(i2c_lock_num, save);


    return success;
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