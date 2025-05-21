# RobohandR1 Firmware

## Overview

RobohandR1 is an embedded firmware project for the Raspberry Pi Pico 2W (RP2350) microcontroller that provides a robust framework for robotics applications. The system includes a multi-core cooperative/preemptive scheduler, comprehensive statistics collection, hardware diagnostics, and sensor integration.

## Features

- **Dual-core Scheduler**: Supports both cooperative and preemptive multitasking with 5 priority levels, core affinity, and runtime statistics
- **USB Shell Interface**: Command-line interface for system interaction and debugging with command completion
- **Statistics Collection**: System-wide performance metrics including CPU usage, memory usage, and task execution timing
- **Hardware Diagnostics**: Cache and FPU status detection, benchmark capabilities, and optimization suggestions
- **IMU Integration**: Driver for ICM42670-P 6-axis IMU with DMA support and double buffering for efficient data acquisition

## Hardware Requirements

- Raspberry Pi Pico 2W (RP2350)
- ICM42670-P IMU connected via I2C
- USB connection for shell interface

## Building the Project

1. Ensure you have the Raspberry Pi Pico SDK installed and properly configured
2. Configure the CMake build system:
   ```
   mkdir build
   cd build
   cmake ..
   ```
3. Build the project:
   ```
   make
   ```
4. Flash the firmware to your Raspberry Pi Pico 2W:
   ```
   make flash
   ```

## Project Structure

```
./Include
├── Core              # Core system components
│   ├── hardware_stats.h
│   ├── hardware_stats_shell_commands.h
│   ├── mem_usage.h
│   ├── scheduler.h
│   ├── scheduler_shell_commands.h
│   ├── stats.h
│   ├── stats_shell_commands.h
│   └── usb_shell.h
└── Drivers           # Hardware device drivers
    ├── icm42670.h
    └── icm42670_shell_commands.h
    
./Src
├── Core              # Core implementations
│   ├── hardware_stats.c
│   ├── hardware_stats_shell_commands.c
│   ├── mem_usage.c
│   ├── scheduler.c
│   ├── scheduler_shell_commands.c
│   ├── stats.c
│   ├── stats_shell_commands.c
│   └── usb_shell.c
├── Drivers           # Driver implementations
│   ├── icm42670.c
│   └── icm42670_shell_commands.c
└── main.c            # Application entry point
```

## Shell Commands

The firmware provides a comprehensive set of shell commands for interacting with the system:

### Scheduler Commands
- `scheduler <start|stop|status>` - Control and monitor the scheduler
- `task create <n> <priority> <core> [type]` - Create a test task
- `ps` - List all tasks
- `stats` - Show scheduler statistics
- `trace <on|off>` - Enable/disable scheduler tracing

### System Stats Commands
- `sys_stats` - Show system performance statistics
- `task_stats` - Show task timing statistics
- `opt [suggest]` - Show/suggest optimizations
- `buffers` - Show registered buffers
- `statreset <all|tasks>` - Reset statistics

### Hardware Diagnostic Commands
- `hw_stats [status|detail|benchmark|monitor]` - View and test cache/FPU functionality

### IMU Commands
- WIP

## Extending the System

### Adding a New Command
1. Create a handler function with the signature: `int cmd_handler(int argc, char *argv[])`
2. Create a `shell_command_t` structure with your command name, help text, and handler
3. Register the command using `shell_register_command(&your_command)`

### Adding a New Driver
1. Create header (.h) and implementation (.c) files in the appropriate directories
2. Follow the driver template pattern in the existing code
3. Implement shell commands for interacting with your device
4. Register your shell commands during initialization

## Performance Considerations

The system is designed for real-time applications with these optimizations:
- Critical functions placed in RAM for faster execution
- XIP cache enabled for improved flash performance
- FPU enabled for accelerated floating-point calculations
- Multicore task distribution for parallel processing

## License

- This project is licensed under the Apache-2.0 license, which is found in the included LICENSE file.

## Contributors

- Robert Fudge (rnfudge@mun.ca)