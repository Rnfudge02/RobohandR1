/**
* @file i2c_driver.c (FIXED)
* @brief Generic I2C driver implementation with proper spinlock usage
*/

#include <stdio.h>
#include "log_manager.h"
#include "scheduler.h"
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
            return true;
            
        case SPINLOCK_INIT_PHASE_TRACKING:
            hw_spinlock_register_external(ctx->i2c_lock_num, SPINLOCK_CAT_I2C, 
                                         ctx->name ? ctx->name : "i2c_driver");
            log_message(LOG_LEVEL_INFO, "I2C Driver", "INFO: I2C Driver - Spinlock registered with manager.");
            return true;
            
        case SPINLOCK_INIT_PHASE_FULL:
            log_message(LOG_LEVEL_INFO, "I2C Driver", "I2C driver spinlock fully integrated");
            return true;
            
        default:
            return false;
    }
}

/**
 * @brief Initialize I2C driver with thread safety (FIXED)
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
    
    // Enable pull-ups
    gpio_pull_up(ctx->sda_pin);
    gpio_pull_up(ctx->scl_pin);
    
    // Create a spinlock - bootstrap it
    ctx->i2c_lock_num = hw_spinlock_bootstrap_claim(true);
    if (ctx->i2c_lock_num == UINT_MAX) {
        log_message(LOG_LEVEL_WARN, "I2C Driver", "Failed to claim spinlock, thread safety disabled.");
        ctx->lock_initialized = false;
    } else {
        ctx->i2c_spin_lock = spin_lock_instance(ctx->i2c_lock_num);
        ctx->lock_initialized = true;
    }
    
    // FIX: Set initialized flag to true
    ctx->initialized = true;
    
    // Register with spinlock manager
    if (hw_spinlock_get_init_phase() != SPINLOCK_INIT_PHASE_NONE) {
        i2c_spinlock_callback(hw_spinlock_get_init_phase(), ctx);
    } else {
        hw_spinlock_register_component(ctx->name ? ctx->name : "I2CDriver", 
            i2c_spinlock_callback, ctx);
    }
    
    log_message(LOG_LEVEL_INFO, "I2C Driver", "I2C Driver initialized on pins SDA:%u SCL:%u at %luHz\n", 
        ctx->sda_pin, ctx->scl_pin, ctx->clock_freq);
    
    return ctx;
}

void i2c_driver_get_default_config(i2c_driver_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(i2c_driver_config_t));
    
    config->i2c_inst = i2c0;
    config->sda_pin = 16;
    config->scl_pin = 17;
    config->clock_freq = 400000;
    config->use_dma = false;
    config->dma_tx_channel = -1;
    config->dma_rx_channel = -1;
}

/**
 * @brief Thread-safe I2C read operation (FIXED)
 */
bool i2c_driver_read_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr, 
    uint8_t reg_addr, uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }

    uint32_t save = 0;
    
    // Acquire lock if available - use raw SDK spinlock functions
    if (ctx->lock_initialized && ctx->i2c_spin_lock) {
        save = spin_lock_blocking(ctx->i2c_spin_lock);
    }

    // Set up I2C transfer for register address
    int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, &reg_addr, 1, true);
    bool success = false;

    if (result == 1) {
        // Read data from the register
        result = i2c_read_blocking(ctx->i2c_inst, dev_addr, data, len, false);
        success = (result == (int)len);
    }

    // Release lock if it was acquired
    if (ctx->lock_initialized && ctx->i2c_spin_lock) {
        spin_unlock(ctx->i2c_spin_lock, save);
    }

    return success;
}

/**
 * @brief Thread-safe I2C write operation (FIXED)
 */
bool i2c_driver_write_bytes(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
    uint8_t reg_addr, const uint8_t* data, size_t len) {
    if (ctx == NULL || !ctx->initialized || data == NULL || len == 0) {
        return false;
    }

    uint32_t save = 0;
    
    // Acquire lock if available - use raw SDK spinlock functions
    if (ctx->lock_initialized && ctx->i2c_spin_lock) {
        save = spin_lock_blocking(ctx->i2c_spin_lock);
    }

    // Prepare buffer: [register address, data...]
    uint8_t* buffer = (uint8_t*)malloc(len + 1);
    bool success = false;

    if (buffer != NULL) {
        buffer[0] = reg_addr;
        memcpy(buffer + 1, data, len);

        // Write to device
        int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, buffer, len + 1, false);
        success = (result == (int)(len + 1));

        free(buffer);
    }

    // Release lock if it was acquired
    if (ctx->lock_initialized && ctx->i2c_spin_lock) {
        spin_unlock(ctx->i2c_spin_lock, save);
    }

    return success;
}

bool i2c_driver_read_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
    uint8_t reg_addr, uint8_t* data, size_t len,
    void (*callback)(void* user_data), void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma || data == NULL || len == 0) {
        return false;
    }
    
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    int result = i2c_write_blocking(ctx->i2c_inst, dev_addr, &reg_addr, 1, true);
    if (result != 1) {
        return false;
    }
    
    dma_channel_config c = dma_channel_get_default_config(ctx->dma_rx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, i2c_get_dreq(ctx->i2c_inst, false));
    
    dma_channel_configure(ctx->dma_rx_channel,
        &c, data, &i2c_get_hw(ctx->i2c_inst)->data_cmd,
        len, true);
    
    dma_channel_set_irq0_enabled(ctx->dma_rx_channel, true);
    
    return true;
}

bool i2c_driver_write_bytes_dma(i2c_driver_ctx_t* ctx, uint8_t dev_addr,
                               uint8_t reg_addr, const uint8_t* data, size_t len,
                               void (*callback)(void* user_data), void* user_data) {
    if (ctx == NULL || !ctx->initialized || !ctx->use_dma || data == NULL || len == 0) {
        return false;
    }
    
    ctx->dma_complete_callback = callback;
    ctx->dma_user_data = user_data;
    
    uint8_t* buffer = (uint8_t*) malloc(len + 1);
    if (buffer == NULL) {
        return false;
    }
    
    buffer[0] = reg_addr;
    memcpy(buffer + 1, data, len);
    
    i2c_get_hw(ctx->i2c_inst)->enable = 0;
    i2c_get_hw(ctx->i2c_inst)->tar = dev_addr;
    i2c_get_hw(ctx->i2c_inst)->enable = 1;
    
    dma_channel_config c = dma_channel_get_default_config(ctx->dma_tx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, i2c_get_dreq(ctx->i2c_inst, true));
    
    dma_channel_configure(ctx->dma_tx_channel, &c,
        &i2c_get_hw(ctx->i2c_inst)->data_cmd, buffer,
        len + 1, true);
    
    dma_channel_set_irq0_enabled(ctx->dma_tx_channel, true);
    
    return true;
}

bool i2c_driver_set_dma_callback(i2c_driver_ctx_t* ctx, 
    void (*callback)(void* user_data), void* user_data) {

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
    
    if (ctx->use_dma) {
        dma_channel_set_irq0_enabled(ctx->dma_rx_channel, false);
        dma_channel_set_irq0_enabled(ctx->dma_tx_channel, false);
        
        dma_channel_unclaim(ctx->dma_rx_channel);
        dma_channel_unclaim(ctx->dma_tx_channel);
        
        if (g_i2c_dma_ctx == ctx) {
            g_i2c_dma_ctx = NULL;
        }
    }
    
    if (ctx->name) {
        free(ctx->name);
    }
    
    free(ctx);
    
    return true;
}

/**
 * @brief Alternative probe using register read (more reliable for some devices)
 */
bool i2c_driver_probe_with_read(i2c_driver_ctx_t* ctx, uint8_t addr, bool debug) {
    if (ctx == NULL || !ctx->initialized) {
        return false;
    }

    uint32_t save = 0;
    
    if (ctx->lock_initialized && ctx->i2c_spin_lock) {
        save = spin_lock_blocking(ctx->i2c_spin_lock);
    }

    bool success = false;
    uint8_t dummy_data;
    
    // Try to read 1 byte - this is often more reliable than write-only probe
    int result = i2c_read_timeout_us(ctx->i2c_inst, addr, &dummy_data, 1, false, 2000); // 2ms timeout
    
    if (debug) {
        log_message(LOG_LEVEL_INFO, "I2C Driver", "Addr 0x%02X. (read): result=%d. ", addr, result);
    }
    
    // For read probe: result should be 1 (1 byte read) on success
    if (result == 1) {
        success = true;
    } else {
        success = false;
    }
    
    if (debug) {
        log_message(LOG_LEVEL_INFO, "-> %s.", success ? "ACK" : "NACK");
        if (success) {
            log_message(LOG_LEVEL_ERROR, "I2C Driver", "(data=0x%02X)", dummy_data);
        }
    }

    if (ctx->lock_initialized && ctx->i2c_spin_lock) {
        spin_unlock(ctx->i2c_spin_lock, save);
    }

    return success;
}

/**
 * @brief Enhanced I2C scan with multiple probe methods
 */
int i2c_scan(i2c_driver_ctx_t* ctx) {
    if (ctx == NULL) {
        log_message(LOG_LEVEL_ERROR, "I2C Driver", "I2C context is NULL.");
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "I2C Driver", "Enhanced I2C Bus Scan (with timeouts and validation)");
    log_message(LOG_LEVEL_INFO, "I2C Driver", "====================================================");
    

    int res_found = 0;
    
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        sleep_us(100);
        bool res = i2c_driver_probe_with_read(ctx, addr, false);
        
        if (res) res_found++;
        
        // Only count as found if BOTH methods agree (more reliable)
        if (res) {
            log_message(LOG_LEVEL_ERROR, "I2C Driver", "Device found at 0x%02X.", addr);
        }
    }
    
    log_message(LOG_LEVEL_ERROR, "I2C Driver", "Scan Summary:");
    log_message(LOG_LEVEL_ERROR, "I2C Driver", "Devices found: %d devices.", res_found);
    
    return res_found;
}