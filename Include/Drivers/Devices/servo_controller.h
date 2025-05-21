/**
* @file servo_controller.h
* @brief Servo motor controller for Raspberry Pi Pico PWM
* @date 2025-05-14
*/

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Servo operation modes
 */
typedef enum {
    SERVO_MODE_DISABLED = 0,    // Servo output disabled
    SERVO_MODE_POSITION,        // Position control mode
    SERVO_MODE_SPEED,           // Speed control mode (continuous rotation servos)
    SERVO_MODE_SWEEP            // Automatic sweeping mode
} servo_mode_t;

/**
 * @brief Servo configuration structure
 */
typedef struct {
    uint gpio_pin;              // GPIO pin for servo control
    uint min_pulse_us;          // Minimum pulse width in microseconds (typically 500-1000)
    uint max_pulse_us;          // Maximum pulse width in microseconds (typically 2000-2500)
    uint center_pulse_us;       // Center position pulse width (typically 1500)
    float min_angle_deg;        // Minimum angle in degrees
    float max_angle_deg;        // Maximum angle in degrees
    bool inverted;              // Whether direction is inverted
    uint16_t update_rate_hz;    // PWM update rate in Hz (typically 50Hz)
} servo_config_t;

/**
 * @brief Servo controller handle
 */
typedef struct servo_controller_s* servo_controller_t;

/**
 * @brief Create a new servo controller
 * 
 * @param config Servo configuration
 * @return Servo controller handle or NULL if creation failed
 */
servo_controller_t servo_controller_create(const servo_config_t* config);

/**
 * @brief Get default servo configuration for MGR996 servo
 * 
 * @param config Pointer to config structure to fill
 */
void servo_controller_get_default_config(servo_config_t* config);

/**
 * @brief Set servo position
 * 
 * @param controller Servo controller handle
 * @param position Position in degrees
 * @return true if position was set successfully, false otherwise
 */
bool servo_controller_set_position(servo_controller_t controller, float position);

/**
 * @brief Set servo position as percentage
 * 
 * @param controller Servo controller handle
 * @param percentage Position as percentage (0.0 to 100.0)
 * @return true if position was set successfully, false otherwise
 */
bool servo_controller_set_position_percent(servo_controller_t controller, float percentage);

/**
 * @brief Set servo pulse width directly
 * 
 * @param controller Servo controller handle
 * @param pulse_us Pulse width in microseconds
 * @return true if pulse width was set successfully, false otherwise
 */
bool servo_controller_set_pulse(servo_controller_t controller, uint pulse_us);

/**
 * @brief Set servo speed (for continuous rotation servos)
 * 
 * @param controller Servo controller handle
 * @param speed Speed value (-100.0 to 100.0 percent)
 * @return true if speed was set successfully, false otherwise
 */
bool servo_controller_set_speed(servo_controller_t controller, float speed);

/**
 * @brief Configure servo sweep mode
 * 
 * @param controller Servo controller handle
 * @param min_pos Minimum sweep position in degrees
 * @param max_pos Maximum sweep position in degrees
 * @param speed_deg_per_sec Speed in degrees per second
 * @return true if sweep was configured successfully, false otherwise
 */
bool servo_controller_configure_sweep(servo_controller_t controller, 
    float min_pos, float max_pos, float speed_deg_per_sec);

/**
 * @brief Set servo operation mode
 * 
 * @param controller Servo controller handle
 * @param mode Operation mode
 * @return true if mode was set successfully, false otherwise
 */
bool servo_controller_set_mode(servo_controller_t controller, servo_mode_t mode);

/**
 * @brief Get current servo position
 * 
 * @param controller Servo controller handle
 * @return Current position in degrees
 */
float servo_controller_get_position(servo_controller_t controller);

/**
 * @brief Get current servo pulse width
 * 
 * @param controller Servo controller handle
 * @return Current pulse width in microseconds
 */
uint servo_controller_get_pulse(servo_controller_t controller);

/**
 * @brief Execute servo controller task
 * 
 * This should be called periodically to update servo position in sweep mode.
 * 
 * @param controller Servo controller handle
 */
void servo_controller_task(void* controller);

/**
 * @brief Enable the servo output
 * 
 * @param controller Servo controller handle
 * @return true if enabled successfully, false otherwise
 */
bool servo_controller_enable(servo_controller_t controller);

/**
 * @brief Disable the servo output
 * 
 * @param controller Servo controller handle
 * @return true if disabled successfully, false otherwise
 */
bool servo_controller_disable(servo_controller_t controller);

/**
 * @brief Destroy servo controller and free resources
 * 
 * @param controller Servo controller handle
 * @return true if destroyed successfully, false otherwise
 */
bool servo_controller_destroy(servo_controller_t controller);

/**
 * @brief Get the current operating mode of the servo
 * 
 * @param controller Servo controller handle
 * @return Current servo mode
 */
servo_mode_t servo_controller_get_mode(servo_controller_t controller);

/**
 * @brief Get the GPIO pin used by the servo
 * 
 * @param controller Servo controller handle
 * @return GPIO pin number
 */
uint servo_controller_get_gpio_pin(servo_controller_t controller);

#ifdef __cplusplus
}
#endif

#endif // SERVO_CONTROLLER_H