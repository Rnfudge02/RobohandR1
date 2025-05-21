/**
* @file servo_controller.c
* @brief Servo motor controller implementation for Raspberry Pi Pico PWM
* @date 2025-05-14
*/

#include "servo_controller.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// PWM frequency for servos (typically 50Hz)
#define SERVO_PWM_FREQ 50

// Clock frequency of RP2040
#define CLOCK_FREQ 125000000

/**
 * @brief Servo controller structure
 */
struct servo_controller_s {
    servo_config_t config;           // Servo configuration
    uint slice_num;                  // PWM slice number
    uint channel;                    // PWM channel
    uint actual_freq;                // Actual PWM frequency
    float clock_div;                 // Clock divider
    float current_position;          // Current position in degrees
    uint current_pulse_us;           // Current pulse width in microseconds
    servo_mode_t mode;               // Operation mode
    
    // Sweep mode parameters
    float sweep_min_pos;             // Minimum sweep position
    float sweep_max_pos;             // Maximum sweep position
    float sweep_speed;               // Sweep speed in degrees per second
    float sweep_direction;           // Current sweep direction (1 or -1)
    uint32_t last_update_time;       // Last sweep update time
    
    bool is_enabled;                 // Whether servo output is enabled
};

servo_controller_t servo_controller_create(const servo_config_t* config) {
    if (config == NULL) {
        return NULL;
    }
    
    // Allocate controller structure
    servo_controller_t controller = (servo_controller_t)malloc(sizeof(struct servo_controller_s));
    if (controller == NULL) {
        return NULL;
    }
    
    // Initialize controller
    memset(controller, 0, sizeof(struct servo_controller_s));
    controller->config = *config;
    controller->mode = SERVO_MODE_DISABLED;
    controller->current_position = (config->min_angle_deg + config->max_angle_deg) / 2.0f;
    controller->current_pulse_us = config->center_pulse_us;
    controller->is_enabled = false;
    
    // Configure GPIO for PWM
    gpio_set_function(config->gpio_pin, GPIO_FUNC_PWM);
    
    // Find PWM slice and channel for this GPIO pin
    controller->slice_num = pwm_gpio_to_slice_num(config->gpio_pin);
    controller->channel = pwm_gpio_to_channel(config->gpio_pin);
    
    // Calculate PWM period for the desired frequency
    uint32_t clock_div = 1;
    uint32_t wrap_value = CLOCK_FREQ / (config->update_rate_hz * clock_div);
    
    // If wrap value is too large, increase clock divider
    while (wrap_value > 65535 && clock_div < 256) {
        clock_div++;
        wrap_value = CLOCK_FREQ / (config->update_rate_hz * clock_div);
    }
    
    // Store the clock_div value for later use
    controller->clock_div = (float)clock_div;
    
    // Calculate actual frequency based on divider and wrap value
    controller->actual_freq = CLOCK_FREQ / (wrap_value * clock_div);
    
    // Configure PWM
    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&pwm_cfg, (float)clock_div);
    pwm_config_set_wrap(&pwm_cfg, (uint16_t) ((wrap_value - 1) & 0xFF));
    pwm_init(controller->slice_num, &pwm_cfg, false);
    
    // Initialize sweep parameters
    controller->sweep_min_pos = config->min_angle_deg;
    controller->sweep_max_pos = config->max_angle_deg;
    controller->sweep_speed = 90.0f;  // Default 90 degrees per second
    controller->sweep_direction = 1.0f;
    controller->last_update_time = to_ms_since_boot(get_absolute_time());
    
    return controller;
}

void servo_controller_get_default_config(servo_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(servo_config_t));
    
    // MGR996 standard values
    config->min_pulse_us = 500;         // 500us for -90 degrees
    config->max_pulse_us = 2500;        // 2500us for +90 degrees
    config->center_pulse_us = 1500;     // 1500us for 0 degrees
    config->min_angle_deg = -90.0f;     // -90 degrees
    config->max_angle_deg = 90.0f;      // +90 degrees
    config->inverted = false;           // Not inverted
    config->update_rate_hz = 50;        // 50Hz update rate
    config->gpio_pin = 0;               // Default GPIO pin 0
}

static uint32_t angle_to_pulse(servo_controller_t controller, float angle) {
    // Clamp angle to valid range
    if (angle < controller->config.min_angle_deg) {
        angle = controller->config.min_angle_deg;
    }
    else if (angle > controller->config.max_angle_deg) {
        angle = controller->config.max_angle_deg;
    }
    
    // Calculate the pulse width in microseconds
    float angle_range = controller->config.max_angle_deg - controller->config.min_angle_deg;
    float pulse_range = (float) (controller->config.max_pulse_us - controller->config.min_pulse_us);
    
    float angle_normalized = (angle - controller->config.min_angle_deg) / angle_range;
    if (controller->config.inverted) {
        angle_normalized = 1.0f - angle_normalized;
    }
    
    return (uint32_t)((float) (controller->config.min_pulse_us) + (angle_normalized * pulse_range));
}

static float pulse_to_angle(servo_controller_t controller, uint32_t pulse_us) {
    // Clamp pulse to valid range
    if (pulse_us < controller->config.min_pulse_us) {
        pulse_us = controller->config.min_pulse_us;
    }
    else if (pulse_us > controller->config.max_pulse_us) {
        pulse_us = controller->config.max_pulse_us;
    }
    
    // Calculate the angle in degrees
    float angle_range = controller->config.max_angle_deg - controller->config.min_angle_deg;
    float pulse_range = (float) (controller->config.max_pulse_us - controller->config.min_pulse_us);
    
    float pulse_normalized = (float)(pulse_us - controller->config.min_pulse_us) / pulse_range;
    if (controller->config.inverted) {
        pulse_normalized = 1.0f - pulse_normalized;
    }
    
    return controller->config.min_angle_deg + (pulse_normalized * angle_range);
}

static void set_pwm_duty_cycle(servo_controller_t controller, uint32_t pulse_us) {
    if (!controller->is_enabled) {
        return;
    }
    
    // Calculate duty cycle from pulse width
    uint32_t period_us = 1000000 / controller->actual_freq;
    float duty_cycle = (float)pulse_us / (float)period_us;
    
    // Get wrap value from PWM slice
    uint16_t wrap = (uint16_t) CLOCK_FREQ / (uint16_t) ((float) controller->actual_freq * controller->clock_div) - 1;
    
    // Calculate compare value for the desired duty cycle
    uint16_t compare = (uint16_t)(duty_cycle * (float)wrap);
    
    // Set PWM duty cycle
    pwm_set_chan_level(controller->slice_num, controller->channel, compare);
}

bool servo_controller_set_position(servo_controller_t controller, float position) {
    if (controller == NULL) {
        return false;
    }
    
    // Calculate pulse width for this position
    uint32_t pulse_us = angle_to_pulse(controller, position);
    
    // Update PWM duty cycle
    set_pwm_duty_cycle(controller, pulse_us);
    
    // Store current position and pulse width
    controller->current_position = position;
    controller->current_pulse_us = pulse_us;
    
    // Switch to position mode
    controller->mode = controller->is_enabled ? SERVO_MODE_POSITION : SERVO_MODE_DISABLED;
    
    return true;
}

bool servo_controller_set_position_percent(servo_controller_t controller, float percentage) {
    if (controller == NULL) {
        return false;
    }
    
    // Clamp percentage to 0-100
    if (percentage < 0.0f) {
        percentage = 0.0f;
    }
    else if (percentage > 100.0f) {
        percentage = 100.0f;
    }
    
    // Convert percentage to angle
    float angle_range = controller->config.max_angle_deg - controller->config.min_angle_deg;
    float angle = controller->config.min_angle_deg + (percentage / 100.0f) * angle_range;
    
    return servo_controller_set_position(controller, angle);
}

bool servo_controller_set_pulse(servo_controller_t controller, uint pulse_us) {
    if (controller == NULL) {
        return false;
    }
    
    // Clamp pulse width to valid range
    if (pulse_us < controller->config.min_pulse_us) {
        pulse_us = controller->config.min_pulse_us;
    }
    else if (pulse_us > controller->config.max_pulse_us) {
        pulse_us = controller->config.max_pulse_us;
    }
    
    // Update PWM duty cycle
    set_pwm_duty_cycle(controller, pulse_us);
    
    // Store current pulse width and calculate position
    controller->current_pulse_us = pulse_us;
    controller->current_position = pulse_to_angle(controller, pulse_us);
    
    // Switch to position mode
    controller->mode = controller->is_enabled ? SERVO_MODE_POSITION : SERVO_MODE_DISABLED;
    
    return true;
}

bool servo_controller_set_speed(servo_controller_t controller, float speed) {
    if (controller == NULL) {
        return false;
    }
    
    // Clamp speed to -100% to +100%
    if (speed < -100.0f) {
        speed = -100.0f;
    }
    else if (speed > 100.0f) {
        speed = 100.0f;
    }
    
    // Convert speed to pulse width
    // For continuous rotation servos:
    // - 1500us is stop
    // - min_pulse_us to 1500us is CCW rotation (with varying speed)
    // - 1500us to max_pulse_us is CW rotation (with varying speed)
    uint32_t pulse_us;
    if (speed < 0) {
        // CCW rotation (negative speed)
        float factor = -speed / 100.0f;
        pulse_us = controller->config.center_pulse_us - 
                   (uint32_t)((float)(controller->config.center_pulse_us - controller->config.min_pulse_us) * factor);
    } 
    else if (speed > 0) {
        // CW rotation (positive speed)
        float factor = speed / 100.0f;
        pulse_us = controller->config.center_pulse_us + 
                   (uint32_t)((float)(controller->config.max_pulse_us - controller->config.center_pulse_us) * factor);
    }
    else {
        // Stop (zero speed)
        pulse_us = controller->config.center_pulse_us;
    }
    
    // Update PWM duty cycle
    set_pwm_duty_cycle(controller, pulse_us);
    
    // Store current pulse width
    controller->current_pulse_us = pulse_us;
    
    // Switch to speed mode
    controller->mode = controller->is_enabled ? SERVO_MODE_SPEED : SERVO_MODE_DISABLED;
    
    return true;
}

bool servo_controller_configure_sweep(servo_controller_t controller, 
                                    float min_pos, float max_pos, 
                                    float speed_deg_per_sec) {
    if (controller == NULL) {
        return false;
    }
    
    // Validate parameters
    if (min_pos < controller->config.min_angle_deg) {
        min_pos = controller->config.min_angle_deg;
    }
    
    if (max_pos > controller->config.max_angle_deg) {
        max_pos = controller->config.max_angle_deg;
    }
    
    if (min_pos >= max_pos) {
        return false;
    }
    
    if (speed_deg_per_sec <= 0.0f) {
        return false;
    }
    
    // Store sweep parameters
    controller->sweep_min_pos = min_pos;
    controller->sweep_max_pos = max_pos;
    controller->sweep_speed = speed_deg_per_sec;
    controller->last_update_time = to_ms_since_boot(get_absolute_time());
    
    // Start sweep from current position
    if (controller->current_position < min_pos) {
        controller->current_position = min_pos;
        controller->sweep_direction = 1.0f;
    }
    else if (controller->current_position > max_pos) {
        controller->current_position = max_pos;
        controller->sweep_direction = -1.0f;
    }
    else if (controller->current_position - min_pos < max_pos - controller->current_position) {
        controller->sweep_direction = 1.0f;
    }
    else {
        controller->sweep_direction = -1.0f;
    }
    
    // Set initial position
    servo_controller_set_position(controller, controller->current_position);
    
    // Switch to sweep mode
    controller->mode = controller->is_enabled ? SERVO_MODE_SWEEP : SERVO_MODE_DISABLED;
    
    return true;
}

bool servo_controller_set_mode(servo_controller_t controller, servo_mode_t mode) {
    if (controller == NULL) {
        return false;
    }
    
    if (mode == SERVO_MODE_DISABLED) {
        // Disable servo output
        return servo_controller_disable(controller);
    }
    else if (!controller->is_enabled) {
        // Enable servo output
        servo_controller_enable(controller);
    }
    
    controller->mode = mode;
    
    return true;
}

float servo_controller_get_position(servo_controller_t controller) {
    if (controller == NULL) {
        return 0.0f;
    }
    
    return controller->current_position;
}

uint servo_controller_get_pulse(servo_controller_t controller) {
    if (controller == NULL) {
        return 0;
    }
    
    return controller->current_pulse_us;
}

void servo_controller_task(void* controller_ptr) {
    servo_controller_t controller = (servo_controller_t)controller_ptr;
    
    if (controller == NULL || controller->mode != SERVO_MODE_SWEEP || !controller->is_enabled) {
        return;
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_ms = current_time - controller->last_update_time;
    
    // Update sweep position
    if (elapsed_ms > 0) {
        // Calculate position change
        float degrees_to_move = (float)elapsed_ms * controller->sweep_speed / 1000.0f;
        float new_position = controller->current_position + (degrees_to_move * controller->sweep_direction);
        
        // Check if we hit sweep boundaries
        if (new_position >= controller->sweep_max_pos) {
            new_position = controller->sweep_max_pos;
            controller->sweep_direction = -1.0f;
        }
        else if (new_position <= controller->sweep_min_pos) {
            new_position = controller->sweep_min_pos;
            controller->sweep_direction = 1.0f;
        }
        
        // Update servo position
        servo_controller_set_position(controller, new_position);
        controller->mode = SERVO_MODE_SWEEP;  // Set back to sweep mode
        
        // Update timestamp
        controller->last_update_time = current_time;
    }
}

bool servo_controller_enable(servo_controller_t controller) {
    if (controller == NULL) {
        return false;
    }
    
    // Enable PWM output
    pwm_set_enabled(controller->slice_num, true);
    controller->is_enabled = true;
    
    // Restore the appropriate mode
    if (controller->mode == SERVO_MODE_DISABLED) {
        controller->mode = SERVO_MODE_POSITION;
    }
    
    // Apply current pulse width setting
    set_pwm_duty_cycle(controller, controller->current_pulse_us);
    
    return true;
}

bool servo_controller_disable(servo_controller_t controller) {
    if (controller == NULL) {
        return false;
    }
    
    // Store current mode first
    servo_mode_t previous_mode = controller->mode;
    
    // Set mode to disabled
    controller->mode = SERVO_MODE_DISABLED;
    
    // If PWM should be completely turned off:
    if (previous_mode != SERVO_MODE_POSITION && previous_mode != SERVO_MODE_SPEED) {
        // Disable PWM output
        pwm_set_enabled(controller->slice_num, false);
    } else {
        // For position or speed modes, set to center (neutral) position
        set_pwm_duty_cycle(controller, controller->config.center_pulse_us);
    }
    
    controller->is_enabled = false;
    return true;
}

bool servo_controller_destroy(servo_controller_t controller) {
    if (controller == NULL) {
        return false;
    }
    
    // Disable the servo
    servo_controller_disable(controller);
    
    // Free the controller structure
    free(controller);
    
    return true;
}

servo_mode_t servo_controller_get_mode(servo_controller_t controller) {
    if (controller == NULL) {
        return SERVO_MODE_DISABLED;
    }
    
    return controller->mode;
}

uint servo_controller_get_gpio_pin(servo_controller_t controller) {
    if (controller == NULL) {
        return 0;
    }
    
    return controller->config.gpio_pin;
}