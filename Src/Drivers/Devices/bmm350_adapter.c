/**
* @file bmm350_adapter.c
* @brief BMM350 magnetometer driver adapter for RTOS integration
* @date 2025-05-13
*/

#include "log_manager.h"
#include "sensor_manager.h"
#include "bmm350_adapter.h"
#include "i2c_driver.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include <string.h>

static void bmm350_adapter_task_inner_checks(uint32_t current_time, bmm350_task_tcb_t* tcb);

// I2C communication functions for BMM350 driver
static int8_t bmm350_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    bmm350_task_tcb_t* tcb = (bmm350_task_tcb_t*)intf_ptr;
    
    if (tcb == NULL || tcb->params.i2c_ctx == NULL) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "I2C Read: NULL pointer");
        return BMM350_E_NULL_PTR;
    }
    
    bool success = i2c_driver_read_bytes(
        tcb->params.i2c_ctx,
        tcb->params.device_addr,
        reg_addr,
        reg_data,
        len
    );
    
    // Debug logging for first few bytes
    if (len > 0 && reg_data != NULL) {
        log_message(LOG_LEVEL_DEBUG, "BMM350", "I2C Read: addr=0x%02X, reg=0x%02X, len=%d, success=%d, data[0]=0x%02X", 
                    tcb->params.device_addr, reg_addr, len, success, reg_data[0]);
    }
    
    return success ? BMM350_OK : BMM350_E_COM_FAIL;
}

static int8_t bmm350_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    bmm350_task_tcb_t* tcb = (bmm350_task_tcb_t*)intf_ptr;
    
    if (tcb == NULL || tcb->params.i2c_ctx == NULL) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "I2C Write: NULL pointer");
        return BMM350_E_NULL_PTR;
    }
    
    bool success = i2c_driver_write_bytes(
        tcb->params.i2c_ctx,
        tcb->params.device_addr,
        reg_addr,
        reg_data,
        len
    );
    
    log_message(LOG_LEVEL_DEBUG, "BMM350", "I2C Write: addr=0x%02X, reg=0x%02X, len=%d, success=%d", 
                tcb->params.device_addr, reg_addr, len, success);
    
    return success ? BMM350_OK : BMM350_E_COM_FAIL;
}


static void delay_us_tcb(uint32_t period, void *intf_ptr) {
   (void) intf_ptr;
    // Convert microseconds to milliseconds for sleep_ms (round up)
    uint32_t ms = (period + 999) / 1000;
    
    // For very small delays, ensure at least 1ms
    if (ms == 0) {
        ms = 1;
    }
    
    sleep_ms(ms);
}

// Optional callback for DMA transfers
static void bmm350_dma_callback(void* user_data) {
    bmm350_task_tcb_t* tcb = (bmm350_task_tcb_t*)user_data;
    if (tcb != NULL) {
        // Mark data as ready
        tcb->data_ready = true;
    }
}

// Internal function to initialize the BMM350 device
static int8_t bmm350_adapter_init_device(bmm350_task_tcb_t* tcb) {
    int8_t rslt;
    uint8_t chip_id = 0;
    
    log_message(LOG_LEVEL_INFO, "BMM350", "Starting device initialization...");
    
    // Initialize device structure
    tcb->dev.intf_ptr = tcb;
    tcb->dev.read = bmm350_i2c_read;
    tcb->dev.write = bmm350_i2c_write;
    tcb->dev.delay_us = delay_us_tcb;
    
    // Initialize BMM350
    rslt = bmm350_init(&tcb->dev);
    if (rslt != BMM350_OK) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "Init failed with code: %d", rslt);
        return rslt;
    }
    log_message(LOG_LEVEL_INFO, "BMM350", "Basic init successful");
    
    // Read and verify chip ID
    rslt = bmm350_get_regs(BMM350_REG_CHIP_ID, &chip_id, 1, &tcb->dev);
    if (rslt == BMM350_OK) {
        log_message(LOG_LEVEL_INFO, "BMM350", "Chip ID: 0x%02X (expected: 0x33)", chip_id);
    }
    
    // Configure interrupts
    rslt = bmm350_configure_interrupt(BMM350_LATCHED, BMM350_ACTIVE_HIGH, BMM350_INT_OD_PUSHPULL, BMM350_ENABLE, &tcb->dev);
    if (rslt != BMM350_OK) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "Configure interrupt failed: %d", rslt);
        return rslt;
    }
    log_message(LOG_LEVEL_INFO, "BMM350", "Interrupt configured");
    
    // Enable data ready interrupt
    rslt = bmm350_enable_interrupt(BMM350_ENABLE_INTERRUPT, &tcb->dev);
    if (rslt != BMM350_OK) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "Enable interrupt failed: %d", rslt);
        return rslt;
    }
    
    // Set ODR and performance
    rslt = bmm350_set_odr_performance(
        BMM350_DATA_RATE_25HZ,
        BMM350_AVERAGING_2,
        &tcb->dev
    );
    if (rslt != BMM350_OK) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "Set ODR failed: %d", rslt);
        return rslt;
    }
    log_message(LOG_LEVEL_INFO, "BMM350", "ODR set to 25Hz");
    
    // Enable all axes
    rslt = bmm350_enable_axes(
        BMM350_X_EN,
        BMM350_Y_EN,
        BMM350_Z_EN,
        &tcb->dev
    );
    if (rslt != BMM350_OK) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "Enable axes failed: %d", rslt);
        return rslt;
    }
    
    // Set power mode to normal
    rslt = bmm350_set_powermode(BMM350_NORMAL_MODE, &tcb->dev);
    if (rslt != BMM350_OK) {
        log_message(LOG_LEVEL_ERROR, "BMM350", "Set power mode failed: %d", rslt);
        return rslt;
    }
    log_message(LOG_LEVEL_INFO, "BMM350", "Device initialization complete");
    
    return BMM350_OK;
}

bmm350_task_tcb_t* bmm350_adapter_init(i2c_driver_ctx_t* i2c_ctx) {
    bmm350_task_params_t params;
    bmm350_adapter_get_default_params(&params);
    params.i2c_ctx = i2c_ctx;
    
    return bmm350_adapter_init_with_params(&params);
}

bmm350_task_tcb_t* bmm350_adapter_init_with_params(const bmm350_task_params_t* params) {
    if (params == NULL || params->i2c_ctx == NULL) {
        return NULL;
    }
    
    // Allocate task control block
    bmm350_task_tcb_t* tcb = (bmm350_task_tcb_t*)malloc(sizeof(bmm350_task_tcb_t));
    if (tcb == NULL) {
        return NULL;
    }
    
    // Clear the TCB
    memset(tcb, 0, sizeof(bmm350_task_tcb_t));
    
    // Copy parameters
    tcb->params = *params;
    
    // Set initial state
    tcb->state = BMM350_TASK_STATE_IDLE;
    
    // Initialize the DMA callback if DMA is enabled
    if (params->use_dma) {
        i2c_driver_set_dma_callback(params->i2c_ctx, bmm350_dma_callback, tcb);
    }
    
    return tcb;
}

void bmm350_adapter_get_default_params(bmm350_task_params_t* params) {
    if (params == NULL) {
        return;
    }
    
    memset(params, 0, sizeof(bmm350_task_params_t));
    
    params->sampling_rate_ms = 40;    // 25Hz = 40ms period
    params->device_addr = BMM350_I2C_ADSEL_SET_LOW;  // Default BMM350 I2C address
    params->use_dma = false;          // Default to not using DMA
}

void bmm350_adapter_task(void* task_data) {
    bmm350_task_tcb_t* tcb = (bmm350_task_tcb_t*)task_data;
    
    if (tcb == NULL) {
        return;
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // State machine for BMM350 task
    switch (tcb->state) {
        case BMM350_TASK_STATE_IDLE:
            // Nothing to do in idle state
            break;
            
        case BMM350_TASK_STATE_INIT:
            if (bmm350_adapter_init_device(tcb) == BMM350_OK) {
                tcb->state = BMM350_TASK_STATE_RUNNING;
                tcb->last_sample_time = current_time;
            } else {
                tcb->error_count++;
                if (tcb->error_count > 3) {
                    // After multiple failures, go to error state
                    tcb->state = BMM350_TASK_STATE_ERROR;
                }
            }
            break;
            
        case BMM350_TASK_STATE_RUNNING:
            bmm350_adapter_task_inner_checks(current_time, tcb);
            break;
            
        case BMM350_TASK_STATE_ERROR:
            // Attempt recovery every 5 seconds
            if ((current_time - tcb->last_sample_time) >= 5000) {
                // Try to re-initialize the sensor
                if (bmm350_adapter_init_device(tcb) == BMM350_OK) {
                    tcb->state = BMM350_TASK_STATE_RUNNING;
                    tcb->error_count = 0;
                }
                tcb->last_sample_time = current_time;
            }
            break;
            
        case BMM350_TASK_STATE_SUSPENDED:
            // Nothing to do in suspended state
            break;
    }
}

// Modify bmm350_adapter_task_inner_checks function:
static void bmm350_adapter_task_inner_checks(uint32_t current_time, bmm350_task_tcb_t* tcb) {
    if ((current_time - tcb->last_sample_time) >= tcb->params.sampling_rate_ms) {
        int8_t rslt;
        uint8_t int_status = 0;
        
        // Get data ready interrupt status
        rslt = bmm350_get_regs(BMM350_REG_INT_STATUS, &int_status, 1, &tcb->dev);
        
        // If data is ready or we're polling
        if (rslt == BMM350_OK && (int_status & BMM350_DRDY_DATA_REG_MSK)) {
            // Read magnetometer data
            rslt = bmm350_get_compensated_mag_xyz_temp_data(&tcb->mag_data, &tcb->dev);
            
            if (rslt == BMM350_OK) {
                // Update last sample time and data ready flag
                tcb->last_sample_time = current_time;
                tcb->data_ready = true;
                tcb->error_count = 0;
                
                // Transfer data to sensor adapter
                if (tcb->sensor_adapter != NULL) {
                    sensor_data_t sensor_data;
                    sensor_data.xyz.x = tcb->mag_data.x;
                    sensor_data.xyz.y = tcb->mag_data.y;
                    sensor_data.xyz.z = tcb->mag_data.z;
                    sensor_data.timestamp = (float) get_absolute_time();
                    
                    // Update the adapter with new data
                    i2c_sensor_adapter_update_data(
                        (i2c_sensor_adapter_t)tcb->sensor_adapter, 
                        &sensor_data
                    );
                }
                
                return;
            }
            
            tcb->error_count++;
            if (tcb->error_count > 10) {
                tcb->state = BMM350_TASK_STATE_ERROR;
            }
        }
    }
}

bool bmm350_adapter_start(bmm350_task_tcb_t* tcb) {
    if (tcb == NULL) {
        return false;
    }
    
    // Only start if we're in IDLE state
    if (tcb->state != BMM350_TASK_STATE_IDLE) {
        return false;
    }
    
    // Set state to INIT to start the initialization process
    tcb->state = BMM350_TASK_STATE_INIT;
    return true;
}

bool bmm350_adapter_stop(bmm350_task_tcb_t* tcb) {
    if (tcb == NULL) {
        return false;
    }
    
    // If we're running or in error state, put sensor in suspend mode
    if (tcb->state == BMM350_TASK_STATE_RUNNING || tcb->state == BMM350_TASK_STATE_ERROR) {
        int8_t rslt = bmm350_set_powermode(BMM350_SUSPEND_MODE, &tcb->dev);
        if (rslt != BMM350_OK) {
            return false;
        }
    }
    
    // Set state to SUSPENDED
    tcb->state = BMM350_TASK_STATE_SUSPENDED;
    return true;
}

bool bmm350_adapter_get_data(bmm350_task_tcb_t* tcb, struct bmm350_mag_temp_data* mag_data) {
    if (tcb == NULL || mag_data == NULL) {
        return false;
    }
    
    // Only return data if it's fresh
    if (!tcb->data_ready) {
        log_message(LOG_LEVEL_DEBUG, "BMM350", "No new data available");
        return false;
    }
    
    // Copy data to output structure
    *mag_data = tcb->mag_data;
    
    // Reset data ready flag
    tcb->data_ready = false;
    
    log_message(LOG_LEVEL_DEBUG, "BMM350", "Data retrieved: X=%.2f, Y=%.2f, Z=%.2f", 
                mag_data->x, mag_data->y, mag_data->z);
    
    return true;
}

bool bmm350_adapter_set_power_mode(bmm350_task_tcb_t* tcb, uint8_t power_mode) {
    if (tcb == NULL || tcb->state != BMM350_TASK_STATE_RUNNING) {
        return false;
    }
    
    int8_t rslt = bmm350_set_powermode(power_mode, &tcb->dev);
    return (rslt == BMM350_OK);
}

bool bmm350_adapter_set_odr_performance(bmm350_task_tcb_t* tcb, uint8_t odr, uint8_t averaging) {
    if (tcb == NULL || tcb->state != BMM350_TASK_STATE_RUNNING) {
        return false;
    }
    
    int8_t rslt = bmm350_set_odr_performance(odr, averaging, &tcb->dev);
    
    // Update sampling rate based on ODR
    if (rslt == BMM350_OK) {
        // Convert ODR to milliseconds
        switch (odr) {
            case BMM350_DATA_RATE_200HZ:
                tcb->params.sampling_rate_ms = 5;
                break;
            case BMM350_DATA_RATE_100HZ:
                tcb->params.sampling_rate_ms = 10;
                break;
            case BMM350_DATA_RATE_50HZ:
                tcb->params.sampling_rate_ms = 20;
                break;
            case BMM350_DATA_RATE_25HZ:
                tcb->params.sampling_rate_ms = 40;
                break;
            case BMM350_DATA_RATE_12_5HZ:
                tcb->params.sampling_rate_ms = 80;
                break;
            case BMM350_DATA_RATE_6_25HZ:
                tcb->params.sampling_rate_ms = 160;
                break;
            default:
                tcb->params.sampling_rate_ms = 40;  // Default to 25Hz
                break;
        }
    }
    
    return (rslt == BMM350_OK);
}

bool bmm350_adapter_enable_axes(bmm350_task_tcb_t* tcb, uint8_t x_en, uint8_t y_en, uint8_t z_en) {
    if (tcb == NULL || tcb->state != BMM350_TASK_STATE_RUNNING) {
        return false;
    }
    
    int8_t rslt = bmm350_enable_axes(x_en, y_en, z_en, &tcb->dev);
    return (rslt == BMM350_OK);
}

bool bmm350_adapter_configure_interrupt(bmm350_task_tcb_t* tcb) {
    if (tcb == NULL || tcb->state != BMM350_TASK_STATE_RUNNING) {
        return false;
    }
    
    int8_t rslt = bmm350_configure_interrupt(BMM350_LATCHED, BMM350_ACTIVE_HIGH, BMM350_INT_OD_PUSHPULL, BMM350_ENABLE, &tcb->dev);
    return (rslt == BMM350_OK);
}

bool bmm350_adapter_self_test(bmm350_task_tcb_t* tcb, struct bmm350_self_test* result) {
    if (tcb == NULL || result == NULL) {
        return false;
    }
    
    // Self-test requires NORMAL mode
    if (tcb->state != BMM350_TASK_STATE_RUNNING) {
        return false;
    }
    
    int8_t rslt = bmm350_perform_self_test(result, &tcb->dev);
    return (rslt == BMM350_OK);
}

bool bmm350_adapter_deinit(bmm350_task_tcb_t* tcb) {
    if (tcb == NULL) {
        return false;
    }
    
    // If we're running, put the sensor in suspend mode
    if (tcb->state == BMM350_TASK_STATE_RUNNING) {
        bmm350_set_powermode(BMM350_SUSPEND_MODE, &tcb->dev);
    }
    
    // Free the TCB
    free(tcb);
    
    return true;
}