/**
* @file sensor_manager_shell_commands.c
* @brief Shell command implementations for sensor manager
* @date 2025-05-13
*/

#include "sensor_manager_shell_commands.h"
#include "sensor_manager.h"
#include "i2c_driver.h"
#include "scheduler.h"
#include "bmm350_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External reference to the global sensor manager
// This will be initialized in sensor_manager_init.c
extern sensor_manager_t g_sensor_manager;
extern i2c_driver_ctx_t* g_i2c_driver;
extern int g_sensor_task_id;

// Shell command handler for sensor operations
int cmd_sensor(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: sensor <command> [args...]\n");
        printf("Commands:\n");
        printf("  info      - Show sensor info\n");
        printf("  start     - Start all sensors\n");
        printf("  stop      - Stop all sensors\n");
        printf("  start <type> - Start specific sensor\n");
        printf("  stop <type>  - Stop specific sensor\n");
        printf("  read <type>  - Read data from sensor\n");
        printf("  mode <type> <mode> - Set sensor power mode\n");
        printf("  rate <type> <rate> - Set sensor rate\n");
        printf("where <type> is: mag, accel, gyro, temp, press, hum\n");
        printf("and <mode> is: off, low, normal, high\n");
        printf("and <rate> is: off, low, normal, high, vhigh\n");
        return 1;
    }

    if (g_sensor_manager == NULL) {
        printf("Sensor manager not initialized\n");
        return 1;
    }

    // Parse sensor type if present
    sensor_type_t type = SENSOR_TYPE_UNKNOWN;
    if (argc > 2) {
        if (strcmp(argv[2], "mag") == 0) {
            type = SENSOR_TYPE_MAGNETOMETER;
        } else if (strcmp(argv[2], "accel") == 0) {
            type = SENSOR_TYPE_ACCELEROMETER;
        } else if (strcmp(argv[2], "gyro") == 0) {
            type = SENSOR_TYPE_GYROSCOPE;
        } else if (strcmp(argv[2], "temp") == 0) {
            type = SENSOR_TYPE_TEMPERATURE;
        } else if (strcmp(argv[2], "press") == 0) {
            type = SENSOR_TYPE_PRESSURE;
        } else if (strcmp(argv[2], "hum") == 0) {
            type = SENSOR_TYPE_HUMIDITY;
        }
    }

    // Process commands
    if (strcmp(argv[1], "info") == 0) {
        printf("Sensor Manager Status:\n");
        printf("Task ID: %d\n", g_sensor_task_id);
        printf("I2C Driver: %s\n", g_i2c_driver ? "Initialized" : "Not initialized");
        printf("Registered sensors:\n");
        printf("  - Magnetometer (BMM350): %s\n", 
               sensor_manager_get_data(g_sensor_manager, SENSOR_TYPE_MAGNETOMETER, NULL) ? "Available" : "Not available");
        printf("  - Accelerometer: %s\n", 
               sensor_manager_get_data(g_sensor_manager, SENSOR_TYPE_ACCELEROMETER, NULL) ? "Available" : "Not available");
        printf("  - Gyroscope: %s\n", 
               sensor_manager_get_data(g_sensor_manager, SENSOR_TYPE_GYROSCOPE, NULL) ? "Available" : "Not available");
        printf("  - Temperature: %s\n", 
               sensor_manager_get_data(g_sensor_manager, SENSOR_TYPE_TEMPERATURE, NULL) ? "Available" : "Not available");
        printf("  - Pressure: %s\n", 
               sensor_manager_get_data(g_sensor_manager, SENSOR_TYPE_PRESSURE, NULL) ? "Available" : "Not available");
        printf("  - Humidity: %s\n", 
               sensor_manager_get_data(g_sensor_manager, SENSOR_TYPE_HUMIDITY, NULL) ? "Available" : "Not available");
        
    } else if (strcmp(argv[1], "start") == 0) {
        if (argc > 2) {
            // Start specific sensor
            if (type == SENSOR_TYPE_UNKNOWN) {
                printf("Unknown sensor type: %s\n", argv[2]);
                return 1;
            }
            
            if (sensor_manager_start_sensor(g_sensor_manager, type)) {
                printf("Started sensor: %s\n", argv[2]);
            } else {
                printf("Failed to start sensor: %s\n", argv[2]);
                return 1;
            }
        } else {
            // Start all sensors
            if (sensor_manager_start_all(g_sensor_manager)) {
                printf("Started all sensors\n");
            } else {
                printf("Failed to start all sensors\n");
                return 1;
            }
        }
        
    } else if (strcmp(argv[1], "stop") == 0) {
        if (argc > 2) {
            // Stop specific sensor
            if (type == SENSOR_TYPE_UNKNOWN) {
                printf("Unknown sensor type: %s\n", argv[2]);
                return 1;
            }
            
            if (sensor_manager_stop_sensor(g_sensor_manager, type)) {
                printf("Stopped sensor: %s\n", argv[2]);
            } else {
                printf("Failed to stop sensor: %s\n", argv[2]);
                return 1;
            }
        } else {
            // Stop all sensors
            if (sensor_manager_stop_all(g_sensor_manager)) {
                printf("Stopped all sensors\n");
            } else {
                printf("Failed to stop all sensors\n");
                return 1;
            }
        }
        
    } else if (strcmp(argv[1], "read") == 0) {
        if (argc < 3) {
            printf("Specify sensor type to read\n");
            return 1;
        }
        
        if (type == SENSOR_TYPE_UNKNOWN) {
            printf("Unknown sensor type: %s\n", argv[2]);
            return 1;
        }
        
        sensor_data_t data;
        if (sensor_manager_get_data(g_sensor_manager, type, &data)) {
            // Data will be printed by the callback, but we can also print it here
            switch(type) {
                case SENSOR_TYPE_MAGNETOMETER:
                    printf("MAG: X: %.2f, Y: %.2f, Z: %.2f\n", 
                        data.xyz.x, data.xyz.y, data.xyz.z);
                    break;
                
                case SENSOR_TYPE_ACCELEROMETER:
                    printf("ACCEL: X: %.2f, Y: %.2f, Z: %.2f\n", 
                        data.xyz.x, data.xyz.y, data.xyz.z);
                    break;
                    
                case SENSOR_TYPE_GYROSCOPE:
                    printf("GYRO: X: %.2f, Y: %.2f, Z: %.2f\n", 
                        data.xyz.x, data.xyz.y, data.xyz.z);
                    break;
                    
                case SENSOR_TYPE_TEMPERATURE:
                    printf("TEMP: %.2f °C\n", data.scalar.value);
                    break;
                    
                case SENSOR_TYPE_PRESSURE:
                    printf("PRESS: %.2f hPa\n", data.scalar.value);
                    break;
                    
                case SENSOR_TYPE_HUMIDITY:
                    printf("HUM: %.2f %%\n", data.scalar.value);
                    break;
                    
                default:
                    break;
            }
        } else {
            printf("No data available from sensor: %s\n", argv[2]);
            return 1;
        }
        
    } else if (strcmp(argv[1], "mode") == 0) {
        if (argc < 4) {
            printf("Specify sensor type and mode\n");
            return 1;
        }
        
        if (type == SENSOR_TYPE_UNKNOWN) {
            printf("Unknown sensor type: %s\n", argv[2]);
            return 1;
        }
        
        sensor_power_mode_t mode = SENSOR_POWER_NORMAL;
        if (strcmp(argv[3], "off") == 0) {
            mode = SENSOR_POWER_OFF;
        } else if (strcmp(argv[3], "low") == 0) {
            mode = SENSOR_POWER_LOW;
        } else if (strcmp(argv[3], "normal") == 0) {
            mode = SENSOR_POWER_NORMAL;
        } else if (strcmp(argv[3], "high") == 0) {
            mode = SENSOR_POWER_HIGH;
        } else {
            printf("Unknown power mode: %s\n", argv[3]);
            return 1;
        }
        
        if (sensor_manager_set_power_mode(g_sensor_manager, type, mode)) {
            printf("Set power mode for %s to %s\n", argv[2], argv[3]);
        } else {
            printf("Failed to set power mode\n");
            return 1;
        }
        
    } else if (strcmp(argv[1], "rate") == 0) {
        if (argc < 4) {
            printf("Specify sensor type and rate\n");
            return 1;
        }
        
        if (type == SENSOR_TYPE_UNKNOWN) {
            printf("Unknown sensor type: %s\n", argv[2]);
            return 1;
        }
        
        sensor_rate_t rate = SENSOR_RATE_NORMAL;
        if (strcmp(argv[3], "off") == 0) {
            rate = SENSOR_RATE_OFF;
        } else if (strcmp(argv[3], "low") == 0) {
            rate = SENSOR_RATE_LOW;
        } else if (strcmp(argv[3], "normal") == 0) {
            rate = SENSOR_RATE_NORMAL;
        } else if (strcmp(argv[3], "high") == 0) {
            rate = SENSOR_RATE_HIGH;
        } else if (strcmp(argv[3], "vhigh") == 0) {
            rate = SENSOR_RATE_VERY_HIGH;
        } else {
            printf("Unknown rate: %s\n", argv[3]);
            return 1;
        }
        
        if (sensor_manager_set_rate(g_sensor_manager, type, rate)) {
            printf("Set rate for %s to %s\n", argv[2], argv[3]);
        } else {
            printf("Failed to set rate\n");
            return 1;
        }
        
    } else if (strcmp(argv[1], "status") == 0) {
        sensor_type_t types[SENSOR_MANAGER_MAX_SENSORS];
        bool statuses[SENSOR_MANAGER_MAX_SENSORS];
        
        sensor_manager_t manager = sensor_manager_get_instance();
        if (manager == NULL) {
            printf("Sensor manager not initialized\n");
            return -1;
        }
        
        int count = sensor_manager_get_all_statuses(manager, types, statuses, SENSOR_MANAGER_MAX_SENSORS);
        
        if (count == 0) {
            printf("No sensors found\n");
        } else {
            printf("Sensor Status:\n");
            for (int i = 0; i < count; i++) {
                const char* type_name = "Unknown";
                
                // Convert type to string
                switch (types[i]) {
                    case SENSOR_TYPE_ACCELEROMETER: type_name = "Accelerometer"; break;
                    case SENSOR_TYPE_GYROSCOPE: type_name = "Gyroscope"; break;
                    case SENSOR_TYPE_MAGNETOMETER: type_name = "Magnetometer"; break;
                    case SENSOR_TYPE_PRESSURE: type_name = "Pressure"; break;
                    case SENSOR_TYPE_TEMPERATURE: type_name = "Temperature"; break;
                    case SENSOR_TYPE_HUMIDITY: type_name = "Humidity"; break;
                    case SENSOR_TYPE_LIGHT: type_name = "Light"; break;
                    case SENSOR_TYPE_PROXIMITY: type_name = "Proximity"; break;
                    case SENSOR_TYPE_IMU: type_name = "IMU"; break;
                    case SENSOR_TYPE_ENV: type_name = "Environmental"; break;
                    default: type_name = "Unknown"; break;
                }
                
                printf("  %s: %s\n", type_name, statuses[i] ? "RUNNING" : "STOPPED");
            }
        }
        
        return 0;
        
    } else {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }

    return 0;
}

// Register sensor commands with the shell
void register_sensor_manager_commands(void) {
    static const shell_command_t sensor_command = {
        "sensor",
        "Sensor management commands",
        cmd_sensor
    };
    
    shell_register_command(&sensor_command);
}