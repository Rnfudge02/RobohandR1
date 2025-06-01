# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

#
# @file CMakeLists.txt
# @brief Build configuration for the robotic hand control system.
# @author Robert Fudge
# @date 2025
# @copyright Apache 2.0 License
#

cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure compilation happens for the right CPU architecture before including SDK
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/Dependencies/pico-sdk)
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM "rp2350-arm-s")

# IMPORTANT: Set flags before anything else
# These will be in CACHE so they're hard to override
set(CMAKE_ASM_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=hard -mfpu=fpv5-sp-d16" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=hard -mfpu=fpv5-sp-d16" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=hard -mfpu=fpv5-sp-d16" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=hard -mfpu=fpv5-sp-d16" CACHE STRING "" FORCE)

# Create SDK warnings suppression flags
# First, create compile options for just the architecture without warnings
set(ARCH_ONLY_FLAGS
    -mcpu=cortex-m33
    -mthumb
    -march=armv8-m.main+fp+dsp
    -mfloat-abi=hard
    -mfpu=fpv5-sp-d16
    -O3
)

# Next, create SDK compile options WITH warnings suppression
set(SDK_FLAGS
    ${ARCH_ONLY_FLAGS}
    -w  # Suppress all warnings for SDK
)

# Store the standard warning flags for our code
set(PROJECT_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wformat=2
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wundef
    -Wshadow
    -Wcast-qual
    -Wno-system-headers
)

# Now set default flags for SDK with warnings disabled
set(CMAKE_C_FLAGS_INIT "${SDK_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SDK_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${SDK_FLAGS}")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(RobohandR1 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable with our source code
add_executable(RobohandR1 
    ./Src/main.c

    ./Dependencies/bmm350-sensorapi/bmm350.c
    ./Dependencies/bmm350-sensorapi/bmm350_oor.c
    ./Dependencies/bmp390-sensorapi/bmp3.c

    ./Src/Drivers/Devices/bmm350_adapter.c
    ./Src/Drivers/Devices/servo_controller.c
    ./Src/Drivers/Devices/led_driver.c
    ./Src/Drivers/I2C/i2c_driver.c
    ./Src/Drivers/I2C/i2c_sensor_adapter.c

    ./Src/Kernel/kernel_init.c
    ./Src/Kernel/kernel_test_integration.c

    ./Src/Kernel/Manager/interrupt_manager.c
    ./Src/Kernel/Manager/log_manager.c
    ./Src/Kernel/Manager/sensor_manager.c
    ./Src/Kernel/Manager/servo_manager.c
    ./Src/Kernel/Manager/spinlock_manager.c

    ./Src/Kernel/Scheduler/fault_handlers.c
    ./Src/Kernel/Scheduler/scheduler.c
    ./Src/Kernel/Scheduler/scheduler_mpu.c
    ./Src/Kernel/Scheduler/scheduler_tz.c
    
    ./Src/Programs/stats.c
    ./Src/Programs/usb_shell.c
    ./Src/Programs/VectorND/vector_math.c

    ./Src/Tests/test_framework.c
    ./Src/Tests/test_integration.c
    ./Src/Tests/test_scheduler.c
    ./Src/Tests/test_mpu.c
    ./Src/Tests/test_tz.c
)

pico_set_program_name(RobohandR1 "RobohandR1")
pico_set_program_version(RobohandR1 "0.4")

# Configure stdio over USB
pico_enable_stdio_uart(RobohandR1 0)
pico_enable_stdio_usb(RobohandR1 1)

# Add standard include directories
target_include_directories(RobohandR1 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/Dependencies/bmm350-sensorapi
    ${CMAKE_CURRENT_LIST_DIR}/Dependencies/bmp390-sensorapi
    ${CMAKE_CURRENT_LIST_DIR}/Dependencies/cmsis-core/CMSIS/Core/Include
    ${CMAKE_CURRENT_LIST_DIR}/Dependencies/cmsis-dsp/Include

    ${CMAKE_CURRENT_LIST_DIR}/Include

    ${CMAKE_CURRENT_LIST_DIR}/Include/Drivers
    ${CMAKE_CURRENT_LIST_DIR}/Include/Drivers/Devices
    ${CMAKE_CURRENT_LIST_DIR}/Include/Drivers/I2C
    ${CMAKE_CURRENT_LIST_DIR}/Include/Drivers/SPI

    ${CMAKE_CURRENT_LIST_DIR}/Include/Kernel
    ${CMAKE_CURRENT_LIST_DIR}/Include/Kernel/Manager
    ${CMAKE_CURRENT_LIST_DIR}/Include/Kernel/Scheduler

    ${CMAKE_CURRENT_LIST_DIR}/Include/Programs
    ${CMAKE_CURRENT_LIST_DIR}/Include/Programs/VectorND

    ${CMAKE_CURRENT_LIST_DIR}/Include/Tests
    
)

# Use system includes for SDK directories to suppress warnings
target_include_directories(RobohandR1 SYSTEM PUBLIC
    ${PICO_SDK_PATH}/src/rp2_common/pico_platform/include
    ${PICO_SDK_PATH}/src/rp2350/pico_platform/include
    ${PICO_SDK_PATH}/src/common/pico_base/include
    ${PICO_SDK_PATH}/src  # Include all SDK sources as system includes
)

# Add architecture flags and warnings only for our project files
target_compile_options(RobohandR1 PRIVATE
    ${ARCH_ONLY_FLAGS}
    ${PROJECT_WARNINGS}
    -ffunction-sections
    -fdata-sections
)

# Add linker options - correct syntax without spaces in -Wl arguments
target_link_options(RobohandR1 PRIVATE
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

# Generate a map file for detailed memory analysis
set(MAP_FILE "${CMAKE_BINARY_DIR}/RobohandR1.map")
target_link_options(RobohandR1 PRIVATE -Wl,-Map=${MAP_FILE})

# Enable verbose output during build
if(CMAKE_VERBOSE_MAKEFILE)
    add_custom_command(TARGET RobohandR1 POST_BUILD
        COMMAND echo "Building with verbose output"
        VERBATIM
    )
endif()

# Direct approach: Suppress warnings for all SDK libraries
# These are updated based on common SDK components
foreach(SDK_TARGET
    pico_stdlib
    pico_multicore
    pico_platform
    pico_time
    pico_util
    pico_sync
    hardware_adc
    hardware_clocks
    hardware_dma
    hardware_flash
    hardware_gpio
    hardware_i2c
    hardware_interp
    hardware_irq
    hardware_pwm
    hardware_spi
    hardware_timer
    hardware_uart
    hardware_vreg
    hardware_watchdog
    hardware_xosc
    tinyusb_device
    tinyusb_board
)
    if(TARGET ${SDK_TARGET})
        target_compile_options(${SDK_TARGET} INTERFACE -w)
    endif()
endforeach()

# Use the proper linker script for RP2350
pico_set_linker_script(RobohandR1 ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld)

# Define cache-related flags
target_compile_definitions(RobohandR1 PRIVATE
    PICO_RAM_FUNCTION_TRACK=1
    PICO_XIP_CACHE_ENABLED=1
    PICO_FPU_ENABLED=1
)

add_library(CMSISDSP STATIC IMPORTED)
set_target_properties(CMSISDSP PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/Dependencies/build/bin_dsp/libCMSISDSP.a
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/Dependencies/cmsis-dsp/Include;${CMAKE_CURRENT_LIST_DIR}/Dependencies/cmsis-core/CMSIS/Core/Include"
)

# Add required libraries
target_link_libraries(RobohandR1
CMSISDSP
    pico_multicore
    pico_stdlib
    hardware_adc
    hardware_clocks
    hardware_dma
    hardware_flash
    hardware_interp
    hardware_i2c
    hardware_pwm
    hardware_spi
    hardware_timer
    hardware_watchdog
)

# CRITICAL: Force hard float ABI for the target after everything else
# This replaces the problematic line with separate property settings
set_target_properties(RobohandR1 PROPERTIES 
    LINK_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=hard -mfpu=fpv5-sp-d16"
)

pico_add_extra_outputs(RobohandR1)

# Print build configuration
message(STATUS "================ Build Configuration ================")
message(STATUS "Board: ${PICO_BOARD}")
message(STATUS "Platform: ${PICO_PLATFORM}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "=================================================")