/**
* @file servo_manager.h
* @brief Servo manager for RTOS-based servo control.
* @date 2025-05-14
*/

#ifndef SERVO_MANAGER_H
#define SERVO_MANAGER_H

#include "servo_controller.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup servo_man_const Servo Manager Configuration Constants
 * @{
 */

/**
 * @brief Maximum number of servos that can be managed.
 */
#define SERVO_MANAGER_MAX_SERVOS 16

/** @} */ // end of servo_man_const group

/**
 * @defgroup servo_man_struct Servo Manager Data structures
 * @{
 */

/**
 * @brief Servo manager handle.
 */
typedef struct servo_manager_s* servo_manager_t;

/**
 * @brief Servo manager configuration.
 */
typedef struct {
    uint32_t task_period_ms;       // Task period in milliseconds.
    bool enable_all_on_start;      // Whether to enable all servos on manager start.
} servo_manager_config_t;

/** @} */ // end of servo_man_struct group

/**
 * @defgroup servo_man_api Servo Manager Application Programming Interface
 * @{
 */

/**
 * @brief Servo movement callback function type.
 */
typedef void (*servo_movement_callback_t)(uint servo_id, float position, void* user_data);

/**
 * @brief Add a servo to the manager.
 * 
 * @param manager Servo manager handle.
 * @param controller Servo controller handle.
 * @param id Optional user-defined ID. (0 to auto-assign)
 * @return Assigned servo ID or -1 if failed.
 */
int servo_manager_add_servo(servo_manager_t manager, servo_controller_t controller, uint id);

/**
 * @brief Configure sweep mode for a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @param min_pos Minimum sweep position in degrees.
 * @param max_pos Maximum sweep position in degrees.
 * @param speed_deg_per_sec Speed in degrees per second.
 * @return true if sweep was configured successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_configure_sweep(servo_manager_t manager, uint id,
    float min_pos, float max_pos, float speed_deg_per_sec);

/**
 * @brief Create a new servo manager.
 * 
 * @param config Configuration for the servo manager.
 * @return Handle to the servo manager or NULL if creation failed.
 */
servo_manager_t servo_manager_create(const servo_manager_config_t* config);

/**
 * @brief Deinitialize the servo manager.
 * 
 * @return true if deinitialized successfully, false otherwise.
 */
bool servo_manager_deinit(void);

/**
 * @brief Destroy the servo manager and free resources.
 * 
 * @param manager Servo manager handle.
 * @return true if destroyed successfully, false otherwise.
 */
bool servo_manager_destroy(servo_manager_t manager);

/**
 * @brief Disable a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @return true if disabled successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_disable_servo(servo_manager_t manager, uint id);

/**
 * @brief Enable a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @return true if enabled successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_enable_servo(servo_manager_t manager, uint id);

/**
 * @brief Make sure servo task is created.
 * 
 * @return true if task exists or was created successfully.
 */
__attribute__((section(".time_critical")))
bool servo_manager_ensure_task(void);

/**
 * @brief Get default servo manager configuration.
 * 
 * @param config Pointer to configuration structure to fill.
 */
void servo_manager_get_default_config(servo_manager_config_t* config);

/**
 * @brief Get the global servo manager instance.
 * 
 * @return Servo manager handle or NULL if not initialized.
 */
__attribute__((section(".time_critical")))
servo_manager_t servo_manager_get_instance(void);

/**
 * @brief Initialize the servo manager and register with scheduler.
 * 
 * @return true if successful, false otherwise.
 */
bool servo_manager_init(void);

/**
 * @brief Lock access to the servo manager.
 * Prevents concurrent access in multi-threaded environment.
 * 
 * @param manager Servo manager handle.
 * @return true if lock acquired, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_lock(servo_manager_t manager);

/**
 * @brief Register movement callback for all servos.
 * 
 * @param manager Servo manager handle.
 * @param callback Callback function.
 * @param user_data User data to pass to callback.
 * @return true if registered successfully, false otherwise.
 */
bool servo_manager_register_callback(servo_manager_t manager, 
    servo_movement_callback_t callback, void* user_data);

/**
 * @brief Remove a servo from the manager.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID to remove.
 * @return true if removed successfully, false otherwise.
 */
bool servo_manager_remove_servo(servo_manager_t manager, uint id);

/**
 * @brief Set operation mode for a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @param mode Operation mode.
 * @return true if mode was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_set_mode(servo_manager_t manager, uint id, servo_mode_t mode);

/**
 * @brief Set position for a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @param position Position in degrees.
 * @return true if position was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_set_position(servo_manager_t manager, uint id, float position);

/**
 * @brief Set position as percentage for a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @param percentage Position as percentage. (0.0 to 100.0)
 * @return true if position was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_set_position_percent(servo_manager_t manager, uint id, float percentage);

/**
 * @brief Set pulse width directly for a specific servo.
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @param pulse_us Pulse width in microseconds.
 * @return true if pulse width was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_set_pulse(servo_manager_t manager, uint id, uint pulse_us);

/**
 * @brief Set speed for a specific servo. (continuous rotation servos)
 * 
 * @param manager Servo manager handle.
 * @param id Servo ID.
 * @param speed Speed value. (-100.0 to 100.0 percent)
 * @return true if speed was set successfully, false otherwise.
 */
__attribute__((section(".time_critical")))
bool servo_manager_set_speed(servo_manager_t manager, uint id, float speed);

/**
 * @brief Start the servo manager.
 * 
 * @param manager Servo manager handle.
 * @return true if started successfully, false otherwise.
 */
bool servo_manager_start(servo_manager_t manager);

/**
 * @brief Stop the servo manager.
 * 
 * @param manager Servo manager handle.
 * @return true if stopped successfully, false otherwise.
 */
bool servo_manager_stop(servo_manager_t manager);

/**
 * @brief RTOS task function for the servo manager.
 * 
 * This function should be registered with your RTOS scheduler.
 * 
 * @param param Pointer to servo manager handle.
 */
__attribute__((section(".time_critical")))
void servo_manager_task(void* param);

/**
 * @brief Unlock access to the servo manager.
 * 
 * @param manager Servo manager handle.
 */
__attribute__((section(".time_critical")))
void servo_manager_unlock(servo_manager_t manager);

/** @} */ // end of servo_man_api group

/**
 * @defgroup servo_man_cmd Servo Manager Command Interface
 * @{
 */

/**
 * @brief Servo manager command handler.
 *
 * @param argc Argument count.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero on error.
 */
int cmd_servo(int argc, char *argv[]);

/**
 * @brief Register all servo manager commands with the shell.
 */
void register_servo_manager_commands(void);

/** @} */ // end of servo_man_cmd group

#ifdef __cplusplus
}
#endif

#endif // SERVO_MANAGER_H