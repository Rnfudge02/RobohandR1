#!/bin/bash

# Copyright [2025] [Robert Fudge]
# SPDX-FileCopyrightText: © 2025 Robert Fudge <rnfudge@mun.ca>
# SPDX-License-Identifier: Apache-2.0

# Robohand Container Controller

# ======== TERMINAL FORMATTING ========
# ASCII escape formatting sequences
RESET="\033[0m"
BOLD="\033[1m"
DIM="\033[2m"
ITALIC="\033[3m"
UNDERLINE="\033[4m"
BLINK="\033[5m"

# ASCII foreground formatting sequences
FG_BLACK="\033[30m"
FG_RED="\033[31m"
FG_GREEN="\033[32m"
FG_YELLOW="\033[33m"
FG_BLUE="\033[34m"
FG_MAGENTA="\033[35m"
FG_CYAN="\033[36m"
FG_WHITE="\033[37m"

# ASCII background formatting sequences
BG_BLACK="\033[40m"
BG_RED="\033[41m"
BG_GREEN="\033[42m"
BG_YELLOW="\033[43m"
BG_BLUE="\033[44m"
BG_MAGENTA="\033[45m"
BG_CYAN="\033[46m"
BG_WHITE="\033[47m"

# ======== GLOBAL VARIABLES ========
# Get project root directory (where script is executed)
PROJECT_ROOT=$(pwd)
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="$PROJECT_ROOT/Outputs"
DOCS_DIR="$PROJECT_ROOT/Documentation"
ELF_FILE="$BUILD_DIR/RobohandR1.elf"
UF2_FILE="$BUILD_DIR/RobohandR1.uf2"
MAP_FILE="$BUILD_DIR/RobohandR1.map"
NEW_SDK_PATH="$PROJECT_ROOT/Dependencies/pico-sdk"
PREFIX="arm-none-eabi-"
SCRIPT_NAME=$(basename "$0")

# RP2350 memory characteristics
FLASH_TOTAL=4194304  # 4MB for RP2350
RAM_TOTAL=524288     # 512KB for RP2350

# ======== FUNCTION DEFINITIONS ========

# Function to print banners
print_banner() {
    local title="$1"
    local width=60
    local padding=$(( (width - ${#title}) / 2 ))
    local line=$(printf '=%.0s' $(seq 1 $width))
    
    echo -e "${FG_MAGENTA}${line}${RESET}"
    printf "${FG_MAGENTA}%${padding}s${FG_CYAN}${BOLD}%s${RESET}${FG_MAGENTA}%${padding}s${RESET}\n" "" "$title" ""
    echo -e "${FG_MAGENTA}${line}${RESET}"
}

# Function to log messages
log_message() {
    local level="$1"
    local message="$2"
    
    case "$level" in
        "INFO")
            echo -e "${FG_GREEN}[RobohandR1]${FG_BLUE} $message${RESET}"
            ;;
        "SUCCESS")
            echo -e "${FG_GREEN}[RobohandR1]${FG_GREEN} $message${RESET}"
            ;;
        "WARNING")
            echo -e "${FG_YELLOW}[RobohandR1]${FG_YELLOW} $message${RESET}"
            ;;
        "ERROR")
            echo -e "${FG_RED}[RobohandR1]${FG_RED} $message${RESET}"
            ;;
        *)
            echo -e "${FG_CYAN}[RobohandR1] $message${RESET}"
            ;;
    esac
}

# Function to get the real path to the device
get_dev_path() {
    log_message "INFO" "Retrieving /dev mapping for $1" 
    ID_VEND=${1%:*}
    ID_PROD=${1#*:}
    for path in $(find /sys/ -name idVendor 2>/dev/null | rev | cut -d/ -f 2- | rev); do
        if grep -q $ID_VEND $path/idVendor; then
            if grep -q $ID_PROD $path/idProduct; then
                find $path -name 'device' | rev | cut -d / -f 2 | rev
            fi
        fi
    done
}

# Function to set/update environment variable
update_var() {
    local var_name="$1"
    local new_value="$2"
    
    if grep -qE "^export ${var_name}=" ~/.bashrc; then
        sed -i "s|^export ${var_name}=.*|export ${var_name}=${new_value}|" ~/.bashrc
    else
        echo "export ${var_name}=${new_value}" >> ~/.bashrc
    fi
}

# Function to check dependencies - improved version that checks package installation as well
check_dependencies() {
    local missing_deps=()
    
    # Check for ninja-build
    if ! dpkg -l | grep -q "ninja-build"; then
        missing_deps+=("ninja-build")
    fi
    
    # Check for gcc-arm-none-eabi
    if ! dpkg -l | grep -q "gcc-arm-none-eabi"; then
        missing_deps+=("gcc-arm-none-eabi")
    fi
    
    # Check for other standard dependencies
    for dep in cmake gcc g++ doxygen minicom; do
        if ! command -v $dep &> /dev/null; then
            missing_deps+=("$dep")
        fi
    done
    
    # Check for bc (needed for calculations)
    if ! command -v bc &> /dev/null; then
        missing_deps+=("bc")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_message "WARNING" "Missing dependencies: ${missing_deps[*]}"
        log_message "INFO" "Use 'sudo ./$SCRIPT_NAME -i' to install dependencies"
        return 1
    fi
    
    return 0
}

# Function to check for ARM compiler tools
check_arm_compiler() {
    # Check if ARM GCC is actually available (sometimes dpkg shows package as installed
    # but the binary is not in PATH)
    if ! command -v arm-none-eabi-gcc &> /dev/null; then
        log_message "ERROR" "ARM GCC compiler not found in PATH. Checking installation..."
        
        # Check if package is installed
        if dpkg -l | grep -q "gcc-arm-none-eabi"; then
            log_message "WARNING" "gcc-arm-none-eabi package is installed but not in PATH."
            
            # Try to find where it's installed
            local arm_gcc_path=$(dpkg -L gcc-arm-none-eabi | grep "bin/arm-none-eabi-gcc$" || echo "")
            
            if [ -n "$arm_gcc_path" ]; then
                log_message "INFO" "Found ARM GCC at: $arm_gcc_path"
                log_message "INFO" "Adding its directory to PATH for this session."
                
                # Extract directory and add to PATH for this session
                local arm_gcc_dir=$(dirname "$arm_gcc_path")
                export PATH="$PATH:$arm_gcc_dir"
                
                # Verify again
                if command -v arm-none-eabi-gcc &> /dev/null; then
                    log_message "SUCCESS" "ARM GCC now available in PATH."
                    return 0
                fi
            fi
            
            log_message "ERROR" "Failed to add ARM GCC to PATH. Please add it manually."
            return 1
        else
            log_message "ERROR" "gcc-arm-none-eabi package is not installed."
            log_message "INFO" "Use 'sudo ./$SCRIPT_NAME -i' to install dependencies."
            return 1
        fi
    fi
    
    return 0
}

# Function to check for Ninja build system
check_ninja() {
    # Check if ninja is actually available (sometimes dpkg shows package as installed
    # but the binary is not in PATH)
    if ! command -v ninja &> /dev/null; then
        log_message "ERROR" "Ninja build system not found in PATH. Checking installation..."
        
        # Check if package is installed
        if dpkg -l | grep -q "ninja-build"; then
            log_message "WARNING" "ninja-build package is installed but not in PATH."
            
            # Try to find where it's installed
            local ninja_path=$(dpkg -L ninja-build | grep "bin/ninja$" || echo "")
            
            if [ -n "$ninja_path" ]; then
                log_message "INFO" "Found Ninja at: $ninja_path"
                log_message "INFO" "Adding its directory to PATH for this session."
                
                # Extract directory and add to PATH for this session
                local ninja_dir=$(dirname "$ninja_path")
                export PATH="$PATH:$ninja_dir"
                
                # Verify again
                if command -v ninja &> /dev/null; then
                    log_message "SUCCESS" "Ninja now available in PATH."
                    return 0
                fi
            fi
            
            log_message "ERROR" "Failed to add Ninja to PATH. Please add it manually."
            return 1
        else
            log_message "ERROR" "ninja-build package is not installed."
            log_message "INFO" "Use 'sudo ./$SCRIPT_NAME -i' to install dependencies."
            return 1
        fi
    fi
    
    return 0
}

# Function to generate AI prompt template
# Function to generate AI prompt template
generate_prompt_template() {
    local prompt_file="./Outputs/prompt_file.txt"
    local request=""
    local include_kernel=0
    local include_kernel_manager=0
    local include_kernel_scheduler=0
    local include_kernel_shell=0

    local include_programs=0

    local include_drivers=0
    local include_drivers_devices=0
    local include_drivers_i2c=0
    local include_drivers_spi=0

    local include_tests=0
    local include_test_framework=0
    local include_test_scheduler=0
    local include_test_mpu=0
    local include_test_tz=0
    local include_test_integration=0

    local include_cmake=0
    local include_readme=0
    local include_main=0

    # Parse arguments after -a
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --kernel)
                include_kernel=1
                shift
                ;;
            --kernel-manager)
                include_kernel_manager=1
                shift
                ;;
            --kernel-scheduler)
                include_kernel_scheduler=1
                shift
                ;;
            --kernel-shell)
                include_kernel_shell=1
                shift
                ;;
            --programs)
                include_programs=1
                shift
                ;;
            --drivers)
                include_drivers=1
                shift
                ;;
            --drivers-devices)
                include_drivers_devices=1
                shift
                ;;
            --drivers-i2c)
                include_drivers_i2c=1
                shift
                ;;
            --drivers-spi)
                include_drivers_spi=1
                shift
                ;;
            --cmake)
                include_cmake=1
                shift
                ;;
            --readme)
                include_readme=1
                shift
                ;;
            --main)
                include_main=1
                shift
                ;;
            --tests)
                include_tests=1
                shift
                ;;
            --test-framework)
                include_test_framework=1
                shift
                ;;
            --test-scheduler)
                include_test_scheduler=1
                shift
                ;;
            --test-mpu)
                include_test_mpu=1
                shift
                ;;
            --test-tz)
                include_test_tz=1
                shift
                ;;
            --test-integration)
                include_test_integration=1
                shift
                ;;
            --all)
                include_kernel=1
                include_programs=1
                include_drivers=1
                include_cmake=1
                include_readme=1
                include_main=1
                include_tests=1
                shift
                ;;
            --all-core)
                include_kernel_init=1
                include_kernel_manager=1
                include_kernel_scheduler=1
                include_kernel_shell=1
                shift
                ;;
            --all-drivers)
                include_drivers_devices=1
                include_drivers_i2c=1
                include_drivers_spi=1
                shift
                ;;
            --all-tests)
                include_test_framework=1
                include_test_scheduler=1
                include_test_mpu=1
                include_test_tz=1
                include_test_integration=1
                shift
                ;;
            *)
                # Assume any other argument is the request text
                request+="$1\n"
                shift
                ;;
        esac
    done

    # If generic core is set, enable all core submodules
    if [ $include_kernel -eq 1 ]; then
        include_kernel_manager=1
        include_kernel_scheduler=1
        include_kernel_shell=1
    fi

    # If generic drivers is set, enable all driver submodules
    if [ $include_drivers -eq 1 ]; then
        include_drivers_devices=1
        include_drivers_i2c=1
        include_drivers_spi=1
    fi

    # If generic tests is set, enable all test submodules
    if [ $include_tests -eq 1 ]; then
        include_test_framework=1
        include_test_scheduler=1
        include_test_mpu=1
        include_test_tz=1
        include_test_integration=1
    fi

    log_message "INFO" "Generating prompt template file"
    mkdir -p ./Outputs
    rm -f ./$prompt_file

    # Add README if requested
    if [ $include_readme -eq 1 ] && [ -f "./README.md" ]; then
        log_message "INFO" "Including README"
        echo -e "=== README ===\n" >> $prompt_file
        cat ./README.md >> $prompt_file
        echo -e "\n\n" >> $prompt_file
    fi

    # Add requested component files list
    echo -e "=== PROJECT STRUCTURE ===\n" >> $prompt_file

    # Kernel submodules file listing
    if [ $include_kernel_manager -eq 1 ] || [ $include_kernel_scheduler -eq 1 ] || [ $include_kernel_shell -eq 1 ]; then
        echo -e "Kernel Files:\n" >> $prompt_file
        echo "./Src/Kernel/kernel_init.c" >> $prompt_file

        
        if [ $include_kernel_manager -eq 1 ]; then
            log_message "INFO" "Listing Kernel/Manager files"
            find ./Src/Kernel/Manager -type f | sort >> $prompt_file
        fi
        
        if [ $include_kernel_scheduler -eq 1 ]; then
            log_message "INFO" "Listing Kernel/Scheduler files"
            find ./Src/Kernel/Scheduler -type f | sort >> $prompt_file
        fi
        
        if [ $include_kernel_shell -eq 1 ]; then
            log_message "INFO" "Listing Core/Shell files"
            find ./Src/Kernel/Shell -type f | sort >> $prompt_file
        fi
        
        echo -e "\n" >> $prompt_file
    fi
    
    if [ $include_programs -eq 1 ]; then
        log_message "INFO" "Listing Program files"
        echo -e "Program Files:\n" >> $prompt_file
        find ./Src/Programs -type f | sort >> $prompt_file
        echo -e "\n" >> $prompt_file
    fi
    
    # Drivers submodules file listing
    if [ $include_drivers_devices -eq 1 ] || [ $include_drivers_i2c -eq 1 ] || [ $include_drivers_spi -eq 1 ]; then
        echo -e "Driver Files:\n" >> $prompt_file
        
        if [ $include_drivers_devices -eq 1 ]; then
            log_message "INFO" "Listing Drivers/Devices files"
            find ./Src/Drivers/Devices -type f | sort >> $prompt_file
        fi
        
        if [ $include_drivers_i2c -eq 1 ]; then
            log_message "INFO" "Listing Drivers/I2C files"
            find ./Src/Drivers/I2C -type f | sort >> $prompt_file
        fi
        
        if [ $include_drivers_spi -eq 1 ]; then
            log_message "INFO" "Listing Drivers/SPI files"
            find ./Src/Drivers/SPI -type f | sort >> $prompt_file
        fi
        
        echo -e "\n" >> $prompt_file
    fi

    if [ $include_main -eq 1 ] && [ -f "./Src/main.c" ]; then
        log_message "INFO" "Listing main.c"
        echo -e "Main File:\n" >> $prompt_file
        echo "./Src/main.c" >> $prompt_file
        echo -e "\n" >> $prompt_file
    fi

    # Test framework file listing
    if [ $include_test_framework -eq 1 ] || [ $include_test_scheduler -eq 1 ] || [ $include_test_mpu -eq 1 ] || [ $include_test_tz -eq 1 ] || [ $include_test_integration -eq 1 ]; then
        echo -e "Test Framework Files:\n" >> $prompt_file
        
        if [ $include_test_framework -eq 1 ]; then
            log_message "INFO" "Listing Test Framework core files"
            echo "./Src/Tests/test_framework.c" >> $prompt_file
            echo "./Src/Tests/test_framework.h" >> $prompt_file
        fi
        
        if [ $include_test_scheduler -eq 1 ]; then
            log_message "INFO" "Listing Scheduler test files"
            echo "./Src/Tests/test_scheduler.c" >> $prompt_file
            echo "./Src/Tests/test_scheduler.h" >> $prompt_file
        fi
        
        if [ $include_test_mpu -eq 1 ]; then
            log_message "INFO" "Listing MPU test files"
            echo "./Src/Tests/test_mpu.c" >> $prompt_file
            echo "./Src/Tests/test_mpu.h" >> $prompt_file
        fi
        
        if [ $include_test_tz -eq 1 ]; then
            log_message "INFO" "Listing TrustZone test files"
            echo "./Src/Tests/test_tz.c" >> $prompt_file
            echo "./Src/Tests/test_tz.h" >> $prompt_file
        fi
        
        if [ $include_test_integration -eq 1 ]; then
            log_message "INFO" "Listing Test Integration files"
            echo "./Src/Tests/test_integration.c" >> $prompt_file
            echo "./Src/Tests/test_integration.h" >> $prompt_file
            echo "./Src/Tests/kernel_test_integration.h" >> $prompt_file
        fi
        
        echo -e "\n" >> $prompt_file
    fi

    # Add specific file content
    echo -e "\n=== CODE CONTENT ===\n" >> $prompt_file
    
    cat ./Include/Kernel/kernel_init.h  >> $prompt_file
    cat ./Src/Kernel/kernel_init.c >> $prompt_file
    
    if [ $include_kernel_manager -eq 1 ]; then
        log_message "INFO" "Adding Kernel/Manager code content"
        for FILE in $(find ./Src/Kernel/Manager -type f | sort); do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi
    
    if [ $include_kernel_scheduler -eq 1 ]; then
        log_message "INFO" "Adding Kernel/Scheduler code content"
        for FILE in $(find ./Src/Kernel/Scheduler -type f | sort); do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi

    if [ $include_programs -eq 1 ]; then
        log_message "INFO" "Adding Program code content"
        for FILE in $(find ./Src/Programs -type f | sort); do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi

    # Drivers submodules content
    if [ $include_drivers_devices -eq 1 ]; then
        log_message "INFO" "Adding Drivers/Devices code content"
        for FILE in $(find ./Src/Drivers/Devices -type f | sort); do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi
    
    if [ $include_drivers_i2c -eq 1 ]; then
        log_message "INFO" "Adding Drivers/I2C code content"
        for FILE in $(find ./Src/Drivers/I2C -type f | sort); do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi
    
    if [ $include_drivers_spi -eq 1 ]; then
        log_message "INFO" "Adding Drivers/SPI code content"
        for FILE in $(find ./Src/Drivers/SPI -type f | sort); do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi

    if [ $include_main -eq 1 ] && [ -f "./Src/main.c" ]; then
        log_message "INFO" "Adding main.c content"
        echo -e "\n// File: ./Src/main.c" >> $prompt_file
        cat ./Src/main.c >> $prompt_file
    fi

    if [ $include_cmake -eq 1 ] && [ -f "CMakeLists.txt" ]; then
        echo -e "\n=== CMAKE CONFIGURATION ===\n" >> $prompt_file
        cat CMakeLists.txt >> $prompt_file
    fi

        # Test framework code content
    if [ $include_test_framework -eq 1 ]; then
        log_message "INFO" "Adding Test Framework core code content"
        if [ -f "./Src/Tests/test_framework.h" ]; then
            echo -e "\n// File: ./Src/Tests/test_framework.h" >> $prompt_file
            cat ./Src/Tests/test_framework.h >> $prompt_file
        fi
        if [ -f "./Src/Tests/test_framework.c" ]; then
            echo -e "\n// File: ./Src/Tests/test_framework.c" >> $prompt_file
            cat ./Src/Tests/test_framework.c >> $prompt_file
        fi
    fi
    
    if [ $include_test_scheduler -eq 1 ]; then
        log_message "INFO" "Adding Scheduler test code content"
        if [ -f "./Src/Tests/test_scheduler.h" ]; then
            echo -e "\n// File: ./Src/Tests/test_scheduler.h" >> $prompt_file
            cat ./Src/Tests/test_scheduler.h >> $prompt_file
        fi
        if [ -f "./Src/Tests/test_scheduler.c" ]; then
            echo -e "\n// File: ./Src/Tests/test_scheduler.c" >> $prompt_file
            cat ./Src/Tests/test_scheduler.c >> $prompt_file
        fi
    fi
    
    if [ $include_test_mpu -eq 1 ]; then
        log_message "INFO" "Adding MPU test code content"
        if [ -f "./Src/Tests/test_mpu.h" ]; then
            echo -e "\n// File: ./Src/Tests/test_mpu.h" >> $prompt_file
            cat ./Src/Tests/test_mpu.h >> $prompt_file
        fi
        if [ -f "./Src/Tests/test_mpu.c" ]; then
            echo -e "\n// File: ./Src/Tests/test_mpu.c" >> $prompt_file
            cat ./Src/Tests/test_mpu.c >> $prompt_file
        fi
    fi
    
    if [ $include_test_tz -eq 1 ]; then
        log_message "INFO" "Adding TrustZone test code content"
        if [ -f "./Src/Tests/test_tz.h" ]; then
            echo -e "\n// File: ./Src/Tests/test_tz.h" >> $prompt_file
            cat ./Src/Tests/test_tz.h >> $prompt_file
        fi
        if [ -f "./Src/Tests/test_tz.c" ]; then
            echo -e "\n// File: ./Src/Tests/test_tz.c" >> $prompt_file
            cat ./Src/Tests/test_tz.c >> $prompt_file
        fi
    fi
    
    if [ $include_test_integration -eq 1 ]; then
        log_message "INFO" "Adding Test Integration code content"
        if [ -f "./Src/Tests/test_integration.h" ]; then
            echo -e "\n// File: ./Src/Tests/test_integration.h" >> $prompt_file
            cat ./Src/Tests/test_integration.h >> $prompt_file
        fi
        if [ -f "./Src/Tests/test_integration.c" ]; then
            echo -e "\n// File: ./Src/Tests/test_integration.c" >> $prompt_file
            cat ./Src/Tests/test_integration.c >> $prompt_file
        fi
        if [ -f "./Src/Tests/kernel_test_integration.h" ]; then
            echo -e "\n// File: ./Src/Tests/kernel_test_integration.h" >> $prompt_file
            cat ./Src/Tests/kernel_test_integration.h >> $prompt_file
        fi
    fi

    # Add user request if provided
    if [ -n "$request" ]; then
        echo -e "\n=== USER REQUEST ===\n" >> $prompt_file
        echo -e "$request" >> $prompt_file
    fi

    log_message "SUCCESS" "Prompt template created: $prompt_file"
    log_message "INFO" "File size: $(du -h $prompt_file | cut -f1)"
}

# Function to build the project
build_project() {
    local verbose="$1"
    
    log_message "INFO" "Building Robohand Project"

    # Check for necessary compilers and tools
    check_arm_compiler || return 1
    check_ninja || return 1

    cd Dependencies

    mkdir ./build
    cd build

    cmake   -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE -S.. -G Ninja

    if ninja; then
        log_message "SUCCESS" "Build completed successfully"
    else
        log_message "ERROR" "Build failed"
        cd ..
        return 1
    fi

    cd ../..
    
    # Create build directory if it doesn't exist
    mkdir -p $BUILD_DIR
    rm -rf $BUILD_DIR/*
    cd $BUILD_DIR

    cmake   -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE -S.. -B$BUILD_DIR -G Ninja
    
    if ninja; then
        log_message "SUCCESS" "Build completed successfully"
    else
        log_message "ERROR" "Build failed"
        cd ..
        return 1
    fi

    # Move output files
    cd ..
    mkdir -p $OUTPUT_DIR
    if [ -f "$UF2_FILE" ]; then
        cp $UF2_FILE $OUTPUT_DIR/
        log_message "SUCCESS" "UF2 file copied to $OUTPUT_DIR"
    else
        log_message "WARNING" "UF2 file not found"
    fi

    return 0
}

# Function to analyze memory usage for Raspberry Pi Pico
#!/bin/bash

# Memory Analysis Script for RP2350/Pi Pico Project
#!/bin/bash

# Memory Analysis Script for RP2350/Pi Pico Project
analyze_memory() {
    # Define colors for output
    FG_GREEN="\033[32m"
    FG_YELLOW="\033[33m"
    FG_RED="\033[31m"
    FG_CYAN="\033[36m"
    FG_BLUE="\033[34m"
    BOLD="\033[1m"
    RESET="\033[0m"
    
    # Define memory sizes for RP2350
    local FLASH_TOTAL=2097152  # 2MB
    local RAM_TOTAL=524288     # 512KB
    local SCRATCH_X_TOTAL=4096 # 4KB
    local SCRATCH_Y_TOTAL=4096 # 4KB
    
    # Define ARM toolchain prefix
    PREFIX=${ARM_PREFIX:-arm-none-eabi-}
    
    # Check if ELF file exists
    if [ ! -f "$ELF_FILE" ]; then
        echo -e "${FG_RED}ERROR: ELF file not found: $ELF_FILE${RESET}"
        echo -e "${FG_CYAN}Build the project first with 'cmake --build .'${RESET}"
        return 1
    fi
    
    # Check for map file
    MAP_FILE="${ELF_FILE%.*}.map"
    
    print_header() {
        echo -e "\n${FG_CYAN}${BOLD}$1${RESET}"
        echo -e "${FG_CYAN}$(printf '=%.0s' $(seq 1 ${#1}))${RESET}"
    }
    
    echo -e "\n${BOLD}${FG_CYAN}RP2350 MEMORY ANALYSIS${RESET}"
    echo -e "${FG_CYAN}=======================${RESET}"
    
    # Basic size information
    print_header "1. BASIC MEMORY USAGE"
    ${PREFIX}size --format=berkeley "$ELF_FILE"

    # Extract standard memory usage metrics
    local TEXT_SIZE=$(${PREFIX}size --format=berkeley "$ELF_FILE" | grep -A1 "text" | tail -n1 | awk '{print $1}')
    local DATA_SIZE=$(${PREFIX}size --format=berkeley "$ELF_FILE" | grep -A1 "text" | tail -n1 | awk '{print $2}')
    local BSS_SIZE=$(${PREFIX}size --format=berkeley "$ELF_FILE" | grep -A1 "text" | tail -n1 | awk '{print $3}')
    
    # Calculate totals
    local FLASH_USED=$TEXT_SIZE
    local RAM_USED=$((DATA_SIZE + BSS_SIZE))
    
    # Display percentage usage
    local FLASH_PERCENT=$(echo "scale=2; ($FLASH_USED * 100) / $FLASH_TOTAL" | bc)
    local RAM_PERCENT=$(echo "scale=2; ($RAM_USED * 100) / $RAM_TOTAL" | bc)
    
    # Detailed section sizes - more compatible approach
    print_header "2. DETAILED SECTION SIZES"
    echo -e "${FG_CYAN}Section           Size (bytes)   Address      Description${RESET}"
    echo -e "${FG_CYAN}---------------   ------------   ---------    ---------------------------${RESET}"
    ${PREFIX}size --format=sysv "$ELF_FILE" | sort -k2,2nr | head -20 | awk '{printf "%-17s %-14s %-13s %s\n", $1, $2, $3, "Section"}'
    
    # Program headers showing memory regions
    print_header "3. MEMORY REGIONS"
    echo -e "${FG_CYAN}Region Information:${RESET}"
    ${PREFIX}readelf -l "$ELF_FILE" | grep -A20 "Program Headers"
    
    print_header "4. MEMORY USAGE SUMMARY"
    echo -e "Flash usage: ${FG_GREEN}$FLASH_USED / $FLASH_TOTAL bytes (${FLASH_PERCENT}%)${RESET}"
    echo -e "RAM usage:   ${FG_GREEN}$RAM_USED / $RAM_TOTAL bytes (${RAM_PERCENT}%)${RESET}"
    
    # RAM sections analysis
    print_header "5. RAM CRITICAL CODE SECTIONS"
    echo -e "${FG_CYAN}Sections with time critical code in RAM:${RESET}"
    echo -e "${FG_CYAN}Section Name        Address Range    Size       Flags${RESET}"
    echo -e "${FG_CYAN}------------------  --------------   --------   -----${RESET}"
    ${PREFIX}readelf -S "$ELF_FILE" | grep -i -E "time_critical|\.ram\." || echo "No time critical code sections found"
    
    # Analyze time critical functions - look specifically for address ranges in RAM
    echo -e "\n${FG_CYAN}Functions in time critical section:${RESET}"
    echo -e "${FG_CYAN}Address       Size       Type    Function${RESET}"
    echo -e "${FG_CYAN}------------  ---------  ------  ---------------------------${RESET}"
    # First check if any functions have addresses in RAM region (0x20000000-0x2007FFFF)
    ram_funcs=$(${PREFIX}nm --print-size --numeric-sort "$ELF_FILE" | grep -E "^20[0-9a-f]{6}")
    if [ -n "$ram_funcs" ]; then
        echo "$ram_funcs" | head -15 | awk '{printf "0x%-10s %-10s %-7s %s\n", $1, $2, $3, $4}'
    else
        echo "No functions found in RAM address range (0x20000000-0x2007FFFF)"
    fi
    
    # Look for functions with .time_critical attribute
    echo -e "\n${FG_CYAN}Functions with time_critical attribute:${RESET}"
    echo -e "${FG_CYAN}Address       Size       Type    Function${RESET}"
    echo -e "${FG_CYAN}------------  ---------  ------  ---------------------------${RESET}"
    ${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | grep -i " t " | tail -15 | awk '{printf "%-12s %-10s %-7s %s\n", $1, $2, $3, $4}'
    
    # Check the 10 largest functions to see if they're in RAM or FLASH
    echo -e "\n${FG_CYAN}Largest function memory placement:${RESET}"
    echo -e "${FG_CYAN}Address       Size       Type    Function             Memory${RESET}"
    echo -e "${FG_CYAN}------------  ---------  ------  ------------------   ------${RESET}"
    largest_funcs=$(${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | grep -E " [Tt] " | tail -10)
    if [ -n "$largest_funcs" ]; then
        echo "$largest_funcs" | while read line; do
            addr=$(echo $line | awk '{print $1}')
            size=$(echo $line | awk '{print $2}')
            type=$(echo $line | awk '{print $3}')
            func=$(echo $line | awk '{print $4}')
            
            # Check if address is in RAM range (decimal comparison)
            if [[ $addr -ge 536870912 && $addr -lt 545259520 ]]; then
                region="RAM"
            else
                region="FLASH"
            fi
            printf "%-12s %-10s %-7s %-20.20s %s\n" "$addr" "$size" "$type" "$func" "$region"
        done
    fi
    
    # Special sections for the RP2350
    print_header "6. RP2350 SPECIAL SECTIONS"
    echo -e "${FG_CYAN}Boot2 section (must be <= 256 bytes):${RESET}"
    echo -e "${FG_CYAN}Section Name        Address Range    Size       Flags${RESET}"
    echo -e "${FG_CYAN}------------------  --------------   --------   -----${RESET}"
    ${PREFIX}readelf -S "$ELF_FILE" | grep -E '\.boot2' || echo "No boot2 section found"
    
    echo -e "\n${FG_CYAN}TrustZone secure sections:${RESET}"
    echo -e "${FG_CYAN}Section Name        Address Range    Size       Flags${RESET}"
    echo -e "${FG_CYAN}------------------  --------------   --------   -----${RESET}"
    ${PREFIX}readelf -S "$ELF_FILE" | grep -E 'TZ|secure|NSC' || echo "No TrustZone sections found"
    
    # Find largest symbols
    print_header "7. TOP 20 LARGEST SYMBOLS"
    echo -e "${FG_CYAN}Type Size (bytes)  Human Size   Symbol Name${RESET}"
    echo -e "${FG_CYAN}---- ------------  -----------  ------------------------------${RESET}"
    ${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | grep -v " 0 " | tail -n 20 | awk '
    {
        addr = $1;
        size = $2;
        type = $3;
        symbol = $4;
        for (i=5; i<=NF; i++) symbol = symbol " " $i;
        
        # Format size to human readable
        if (size > 1024*1024)
            human = sprintf("%.1fMB", size/1024/1024);
        else if (size > 1024)
            human = sprintf("%.1fKB", size/1024);
        else
            human = sprintf("%dB", size);
        
        printf "%-4s %-12d %-12s %-30.30s\n", type, size, human, symbol;
    }' || echo "No symbols found"
    
    # Core components analysis
    print_header "8. MODULE SIZE ANALYSIS"
    echo -e "${FG_CYAN}Module Name       Size          Functions   Description${RESET}"
    echo -e "${FG_CYAN}---------------   -----------   ---------   ---------------------------------${RESET}"
    
    # Check for specific modules in your project - more robust calculation
    modules=("scheduler" "log" "sensor" "shell" "stats" "mpu_tz")
    descriptions=(
        "Task scheduler and context switching"
        "Logging system for debug/info output"
        "Sensor management and data processing"
        "Command line interface over USB"
        "Performance statistics collection"
        "Memory protection and TrustZone"
    )
    
    for i in "${!modules[@]}"; do
        module="${modules[$i]}"
        description="${descriptions[$i]}"
        
        # Count the module functions
        function_count=$(${PREFIX}nm "$ELF_FILE" | grep -i "$module" | wc -l)
        if [ $function_count -gt 0 ]; then
            # Get sizes using manual calculation to avoid decimal conversion issues
            module_size=0
            module_info=$(${PREFIX}nm --print-size "$ELF_FILE" | grep -i "$module" | grep -v " 0 ")
            if [ -n "$module_info" ]; then
                while read -r line; do
                    size=$(echo "$line" | awk '{print $2}')
                    if [[ "$size" =~ ^[0-9]+$ ]]; then
                        module_size=$((module_size + size))
                    fi
                done <<< "$module_info"
            fi
            
            if [ $module_size -gt 0 ]; then
                if [ $module_size -gt 1024 ]; then
                    kb_size=$(echo "scale=1; $module_size/1024" | bc)
                    printf "%-17s ${FG_YELLOW}%-13s${RESET} %-11s %s\n" "$module" "${kb_size}KB" "$function_count" "$description"
                else
                    printf "%-17s ${FG_GREEN}%-13s${RESET} %-11s %s\n" "$module" "${module_size}B" "$function_count" "$description"
                fi
            fi
        fi
    done
    
    # Memory Map overview (if map file exists)
    if [ -f "$MAP_FILE" ]; then
        print_header "9. MEMORY MAP HIGHLIGHTS"
        echo -e "${FG_CYAN}Key sections from memory map:${RESET}"
        echo -e "${FG_CYAN}Memory Region     Start Address    End Address      Size${RESET}"
        echo -e "${FG_CYAN}---------------   --------------   --------------   ----------${RESET}"
        grep -A5 "^Memory Configuration" "$MAP_FILE" | grep -v "^Memory Configuration" || echo "Memory configuration not found in map file"
        
        echo -e "\n${FG_CYAN}Total Memory Map available at: ${MAP_FILE}${RESET}"
    fi
    
    # Optimization and features check
    print_header "10. COMPILATION INFORMATION"
    
    # Check if file was compiled with optimization
    local OPTIMIZE_INFO=$(${PREFIX}readelf -p .comment "$ELF_FILE" | grep -o "\-O[0-3s]" || echo "Unknown")
    echo -e "Optimization: ${FG_GREEN}${OPTIMIZE_INFO}${RESET}"
    echo -e "Note: The code also uses __attribute__((section(\".time_critical\"))) for time-critical functions"
    
    # Check for time_critical sections
    echo -e "\n${FG_CYAN}Feature Detection:${RESET}"
    echo -e "${FG_CYAN}Feature               Count       Status${RESET}"
    echo -e "${FG_CYAN}--------------------  ----------  --------------${RESET}"
    
    # Time critical code detection
    time_critical_count=$(${PREFIX}nm "$ELF_FILE" | grep -i "time_critical" | wc -l)
    if [ $time_critical_count -gt 0 ]; then
        printf "%-22s %-12s ${FG_GREEN}%s${RESET}\n" "Time Critical Code" "$time_critical_count" "Detected"
    else
        printf "%-22s %-12s ${FG_YELLOW}%s${RESET}\n" "Time Critical Code" "0" "Not found"
    fi
    
    # Check for functions in RAM
    ram_functions_count=$(${PREFIX}nm --numeric-sort "$ELF_FILE" | grep -E "^20[0-9a-f]{6}" | wc -l)
    if [ $ram_functions_count -gt 0 ]; then
        printf "%-22s %-12s ${FG_GREEN}%s${RESET}\n" "Functions in RAM" "$ram_functions_count" "Detected"
    else
        printf "%-22s %-12s ${FG_YELLOW}%s${RESET}\n" "Functions in RAM" "0" "Not detected"
    fi
    
    # Check other key features
    features=("scheduler" "multicore" "TrustZone" "MPU" "FPU" "DMA" "USB")
    for feature in "${features[@]}"; do
        feature_count=$(${PREFIX}nm "$ELF_FILE" | grep -i "$feature" | wc -l)
        if [ $feature_count -gt 0 ]; then
            printf "%-22s %-12s ${FG_GREEN}%s${RESET}\n" "$feature" "$feature_count" "Enabled"
        else
            printf "%-22s %-12s ${FG_YELLOW}%s${RESET}\n" "$feature" "0" "Not detected"
        fi
    done
    
    # Human-readable size conversion function
    human_readable() {
        size=$1
        if [ $size -gt 1048576 ]; then
            echo "$(echo "scale=1; $size/1048576" | bc)MB"
        elif [ $size -gt 1024 ]; then
            echo "$(echo "scale=1; $size/1024" | bc)KB"
        else
            echo "${size}B"
        fi
    }
    
    # Memory summary with color alerts
    print_header "11. MEMORY USAGE EVALUATION"
    echo -e "${FG_CYAN}Memory Type    Used             Total            Percentage   Status${RESET}"
    echo -e "${FG_CYAN}------------   --------------   --------------   ----------   ----------------${RESET}"
    
    # Flash usage status with color coding
    flash_human=$(human_readable $FLASH_USED)
    flash_total_human=$(human_readable $FLASH_TOTAL)
    if (( $(echo "$FLASH_PERCENT < 75" | bc -l) )); then
        printf "%-14s %-16s %-16s %-12s ${FG_GREEN}%s${RESET}\n" "Flash" "$flash_human" "$flash_total_human" "${FLASH_PERCENT}%" "Good"
    elif (( $(echo "$FLASH_PERCENT < 90" | bc -l) )); then
        printf "%-14s %-16s %-16s %-12s ${FG_YELLOW}%s${RESET}\n" "Flash" "$flash_human" "$flash_total_human" "${FLASH_PERCENT}%" "Getting high"
    else
        printf "%-14s %-16s %-16s %-12s ${FG_RED}%s${RESET}\n" "Flash" "$flash_human" "$flash_total_human" "${FLASH_PERCENT}%" "Critical!"
    fi
    
    # RAM usage status with color coding
    ram_human=$(human_readable $RAM_USED)
    ram_total_human=$(human_readable $RAM_TOTAL)
    if (( $(echo "$RAM_PERCENT < 75" | bc -l) )); then
        printf "%-14s %-16s %-16s %-12s ${FG_GREEN}%s${RESET}\n" "RAM" "$ram_human" "$ram_total_human" "${RAM_PERCENT}%" "Good"
    elif (( $(echo "$RAM_PERCENT < 90" | bc -l) )); then
        printf "%-14s %-16s %-16s %-12s ${FG_YELLOW}%s${RESET}\n" "RAM" "$ram_human" "$ram_total_human" "${RAM_PERCENT}%" "Getting high"
    else
        printf "%-14s %-16s %-16s %-12s ${FG_RED}%s${RESET}\n" "RAM" "$ram_human" "$ram_total_human" "${RAM_PERCENT}%" "Critical!"
    fi
    
    # Function placement
    print_header "12. FUNCTION PLACEMENT RECOMMENDATIONS"
    echo -e "${FG_CYAN}Function Name             Size (bytes)   Current Location   Recommendation${RESET}"
    echo -e "${FG_CYAN}------------------------  ------------   ----------------   ------------------------${RESET}"
    
    # Look for IRQ handlers and critical functions to recommend for RAM placement
    critical_candidates=$(${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | grep -E ' T ' | grep -i -E 'handler|irq|isr|interrupt|schedule|task|time|tick|timer|callback' | tail -10)
    if [ -n "$critical_candidates" ]; then
        # For each candidate, check if it's already in RAM
        echo "$critical_candidates" | while read line; do
            addr=$(echo $line | awk '{print $1}')
            size=$(echo $line | awk '{print $2}')
            name=$(echo $line | awk '{print $4}')
            
            # Check if address is in RAM range - decimal check not reliable, use hex
            if [[ "$addr" =~ ^20 ]]; then
                printf "%-26s %-14s %-18s ${FG_GREEN}%s${RESET}\n" "$name" "$size" "RAM" "Already optimized"
            else
                printf "%-26s %-14s %-18s ${FG_YELLOW}%s${RESET}\n" "$name" "$size" "FLASH" "Move to RAM with .time_critical"
            fi
        done
    else
        echo "No specific recommendations found"
    fi
    
    echo -e "\n${FG_CYAN}${BOLD}MEMORY ANALYSIS COMPLETE${RESET}"
}

# Function to generate documentation
generate_documentation() {
    log_message "INFO" "Generating doxygen documentation"

    # Check if doxygen is available
    if ! command -v doxygen &> /dev/null; then
        log_message "ERROR" "Doxygen not found. Please install it with 'sudo apt install doxygen'"
        return 1
    fi

    # Create documentation directory if it doesn't exist
    mkdir -p $DOCS_DIR

    # Create doxyfile if it doesn't exist
    if [ ! -f "$DOCS_DIR/Robohand.doxyfile" ]; then
        doxygen -g $DOCS_DIR/Robohand.doxyfile
    
        # Update doxyfile with project settings
        sed -i 's/PROJECT_NAME           = "My Project"/PROJECT_NAME           = "RobohandR1"/' Robohand.doxyfile
        sed -i 's|OUTPUT_DIRECTORY       =|OUTPUT_DIRECTORY       = Documentation|' Robohand.doxyfile
        sed -i 's/EXTRACT_ALL            = NO/EXTRACT_ALL            = YES/' Robohand.doxyfile
        sed -i 's/EXTRACT_PRIVATE        = NO/EXTRACT_PRIVATE        = YES/' Robohand.doxyfile
        sed -i 's/EXTRACT_STATIC         = NO/EXTRACT_STATIC         = YES/' Robohand.doxyfile
        sed -i 's/GENERATE_LATEX         = YES/GENERATE_LATEX         = NO/' Robohand.doxyfile
        sed -i 's/HAVE_DOT               = NO/HAVE_DOT               = YES/' Robohand.doxyfile
        sed -i 's/UML_LOOK               = NO/UML_LOOK               = YES/' Robohand.doxyfile
        sed -i 's/CALL_GRAPH             = NO/CALL_GRAPH             = YES/' Robohand.doxyfile
        sed -i 's/CALLER_GRAPH           = NO/CALLER_GRAPH           = YES/' Robohand.doxyfile
    
        # Add these two critical directory configuration changes
        sed -i 's|^INPUT                  =|INPUT                  = ./Include ./Src|' Robohand.doxyfile
        sed -i 's/RECURSIVE              = NO/RECURSIVE              = YES/' Robohand.doxyfile
    fi

    mkdir -p $DOCS_DIR/Doxygen
    cd $DOCS_DIR/Doxygen

    # Generate documentation
    if doxygen ../Robohand.doxyfile; then
        log_message "SUCCESS" "Documentation generated in $DOCS_DIR"
        log_message "INFO" "Open $DOCS_DIR/html/index.html in your browser to view"
    else
        log_message "ERROR" "Failed to generate documentation"
    fi
}

# Function to initialize the development environment
initialize_environment() {
    log_message "INFO" "Initializing system for Robohand development"

    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_message "ERROR" "Please run with sudo: 'sudo ./$SCRIPT_NAME -i'"
        return 1
    fi

    # Install needed apt packages
    log_message "INFO" "Installing required packages"
    apt update && apt install -y minicom cmake build-essential gcc g++ \
        gcc-arm-none-eabi libnewlib-arm-none-eabi doxygen libxapian30 graphviz \
        ninja-build bc

    # Initialize submodules if in a git repository
    if [ -d ".git" ]; then
        log_message "INFO" "Initializing git submodules"
        git submodule update --init --recursive
    else
        log_message "INFO" "Not a git repository, skipping submodule initialization"
    fi

    # Set up environment variables if needed
    if [ -d "$NEW_SDK_PATH" ]; then
        update_var "PICO_SDK_PATH" "$NEW_SDK_PATH"
        log_message "INFO" "Set PICO_SDK_PATH to $NEW_SDK_PATH"
    fi

    log_message "SUCCESS" "Robohand system initialized successfully"
}

# Function to push UF2 file to RP2350 device
push_uf2() {
    local force_mode="$1"
    
    log_message "INFO" "Looking for RP2350 device in bootloader mode"
    
    # Check if UF2 file exists
    if [ ! -f "$UF2_FILE" ]; then
        log_message "ERROR" "UF2 file not found: $UF2_FILE"
        log_message "INFO" "Build the project first with './$SCRIPT_NAME -b'"
        return 1
    fi
    
    # Look for RP2350 in bootloader mode (ID 2e8a:000f)
    local device_found=$(lsusb | grep "2e8a:000f")
    
    if [ -z "$device_found" ]; then
        log_message "WARNING" "RP2350 not found in bootloader mode"
        log_message "INFO" "To enter bootloader mode:"
        log_message "INFO" "1. Hold BOOTSEL button while connecting USB, OR"
        log_message "INFO" "2. Hold BOOTSEL button and press RESET, then release both"
        log_message "INFO" "3. Device should appear as 'RP2350 Boot' in lsusb"
        
        if [ "$force_mode" != "wait" ]; then
            return 1
        fi
        
        log_message "INFO" "Waiting for device to enter bootloader mode..."
        log_message "INFO" "Press Ctrl+C to cancel"
        
        # Wait for device to appear (check every 2 seconds)
        while [ -z "$(lsusb | grep '2e8a:000f')" ]; do
            sleep 2
            printf "."
        done
        echo ""
        log_message "SUCCESS" "RP2350 detected in bootloader mode"
    else
        log_message "SUCCESS" "Found RP2350 in bootloader mode: $device_found"
    fi
    
    # Find the mount point for the RP2350
    local mount_point=""
    local retry_count=0
    local max_retries=10
    
    # Sometimes it takes a moment for the device to mount
    while [ -z "$mount_point" ] && [ $retry_count -lt $max_retries ]; do
        # Look for RP2350 mount point
        mount_point=$(mount | grep -i "rp2350\|rpi-rp2" | awk '{print $3}' | head -n1)
        
        if [ -z "$mount_point" ]; then
            # Also check /media and /mnt for mounted devices
            mount_point=$(find /media /mnt -maxdepth 2 -name "*RP2350*" -o -name "*RPI-RP2*" 2>/dev/null | head -n1)
        fi
        
        if [ -z "$mount_point" ]; then
            log_message "INFO" "Waiting for RP2350 to mount... (attempt $((retry_count + 1))/$max_retries)"
            sleep 1
            retry_count=$((retry_count + 1))
        fi
    done
    
    if [ -z "$mount_point" ]; then
        log_message "ERROR" "Could not find RP2350 mount point"
        log_message "INFO" "Manual steps:"
        log_message "INFO" "1. The device should appear as a USB drive"
        log_message "INFO" "2. Copy $UF2_FILE to the USB drive"
        log_message "INFO" "3. The device will automatically reboot and run your code"
        return 1
    fi
    
    log_message "SUCCESS" "Found RP2350 mounted at: $mount_point"
    
    # Verify it's actually writable and looks like a RP2350
    if [ ! -w "$mount_point" ]; then
        log_message "ERROR" "Mount point $mount_point is not writable"
        log_message "INFO" "You may need to run with sudo permissions"
        return 1
    fi
    
    # Check if it looks like a RP2350 bootloader (should have INFO_UF2.TXT)
    if [ ! -f "$mount_point/INFO_UF2.TXT" ]; then
        log_message "WARNING" "Mount point doesn't contain INFO_UF2.TXT - may not be RP2350"
        log_message "INFO" "Contents of $mount_point:"
        ls -la "$mount_point" 2>/dev/null || echo "Cannot list contents"
        
        read -p "Continue anyway? (y/N): " confirm
        if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
            log_message "INFO" "Push cancelled by user"
            return 1
        fi
    else
        log_message "INFO" "Verified RP2350 bootloader (found INFO_UF2.TXT)"
    fi
    
    # Copy the UF2 file
    log_message "INFO" "Copying UF2 file to device..."
    log_message "INFO" "Source: $UF2_FILE"
    log_message "INFO" "Target: $mount_point/$(basename $UF2_FILE)"
    
    if cp "$UF2_FILE" "$mount_point/"; then
        log_message "SUCCESS" "UF2 file copied successfully"
        log_message "INFO" "File size: $(du -h "$UF2_FILE" | cut -f1)"
        
        # Sync to ensure write is complete
        log_message "INFO" "Syncing filesystem..."
        sync
        
        log_message "SUCCESS" "Programming complete!"
        log_message "INFO" "Device should automatically reboot and run your code"
        log_message "INFO" "You can now connect to serial with './$SCRIPT_NAME -s'"
        
        return 0
    else
        log_message "ERROR" "Failed to copy UF2 file"
        log_message "INFO" "Check permissions and available space"
        return 1
    fi
}

# Function to start serial communication
start_serial() {
    local device="$1"
    
    if [ -z "$device" ]; then
        device="/dev/ttyACM0"
    fi
    
    if [ ! -c "$device" ]; then
        log_message "ERROR" "Serial device $device not found"
        log_message "INFO" "Available serial devices:"
        ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "No serial devices found"
        return 1
    fi
    
    log_message "INFO" "Starting serial communication with $device"
    
    # Check if minicom is installed
    if ! command -v minicom &> /dev/null; then
        log_message "ERROR" "Minicom not found. Please install it with 'sudo apt install minicom'"
        return 1
    fi
    
    # Run minicom with appropriate settings
    sudo minicom -b 115200 -o -D "$device"
    
    log_message "INFO" "Serial communication closed"
}

# Function to display help
display_help() {
    echo -e "${FG_GREEN}${BOLD}Robohand Controller V1.0 - Developed by Robert Fudge${RESET}"
    echo -e "${FG_CYAN}${BOLD}Usage:${RESET} ./$SCRIPT_NAME [OPTION] [ARGS]"
    echo -e ""
    echo -e "${FG_CYAN}${BOLD}Options:${RESET}"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-a" "AI-prompt" "Generate LLM prompt template with options:"
    echo -e "      Core modules:    ${FG_YELLOW}--core-init --core-manager --core-scheduler --core-shell --core-stats${RESET}"
    echo -e "      Driver modules:  ${FG_YELLOW}--drivers-devices --drivers-i2c --drivers-spi${RESET}"
    echo -e "      Other options:   ${FG_YELLOW}--components --cmake --readme --main${RESET}"
    echo -e "      Grouped options: ${FG_YELLOW}--all --all-core --all-drivers${RESET}"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-b" "Build" "Build RobohandR1 project [optional: verbose]"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-d" "Document" "Generate Doxygen documentation"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-h" "Help" "Display this help message"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-i" "Init" "Initialize development environment (requires sudo)"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-m" "Memory" "Analyze memory usage (run after building)"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-p" "Push" "Push UF2 to RP2350 device [optional: wait]"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-s" "Serial" "Start serial communication [optional: device path]"
    echo -e ""
    echo -e "${FG_CYAN}${BOLD}Examples:${RESET}"
    echo -e "  ./$SCRIPT_NAME -b verbose    # Build with verbose output"
    echo -e "  ./$SCRIPT_NAME -m            # Analyze memory usage"
    echo -e "  ./$SCRIPT_NAME -s /dev/ttyACM1  # Connect to specific serial port"
    echo -e "  ./$SCRIPT_NAME -p wait       # Wait for device to enter bootloader mode"
}

# ======== MAIN SCRIPT ========

# Check if no arguments provided
if [ $# -eq 0 ]; then
    display_help
    exit 0
fi

# Parse command line arguments without getopts to handle optional arguments better
while [ $# -gt 0 ]; do
    case "$1" in
        # AI-prompt - generate a template prompt document
        -a)
            shift
            # Collect all remaining arguments for passing to the prompt template function
            prompt_args=()
            while [ $# -gt 0 ] && [[ ! "$1" =~ ^- || "$1" =~ ^--[a-z] ]]; do
                prompt_args+=("$1")
                shift
            done
            # Pass all collected arguments to the function
            generate_prompt_template "${prompt_args[@]}"
        ;;

        # Build - build the project
        -b)
            shift
            # Check if we have an argument and it doesn't start with -
            if [ $# -gt 0 ] && [[ "$1" == "verbose" ]]; then
                build_project "verbose"
                shift
            else
                build_project ""
            fi
            ;;

        # Document - generate documentation
        -d)
            shift
            generate_documentation
            ;;

        # Help - display help message
        -h)
            shift
            display_help
            ;;

        # Init - initialize development environment
        -i)
            shift
            initialize_environment
            ;;
            
        # Memory - analyze memory usage
        -m)
            shift
            analyze_memory
            ;;

        # Push - send uf2 to the Pi Pico 2
        -p)
            shift
            # Check if we have an argument and it doesn't start with -
            if [ $# -gt 0 ] && [[ "$1" == "wait" ]]; then
                push_uf2 "wait"
                shift
            else
                push_uf2 ""
            fi
            ;;

        # Serial - start serial communication
        -s)
            shift
            # Check if we have an argument and it doesn't start with -
            if [ $# -gt 0 ] && [[ ! "$1" =~ ^- ]]; then
                start_serial "$1"
                shift
            else
                start_serial ""
            fi
            ;;

        # Unknown option
        *)
            log_message "ERROR" "Invalid option: $1"
            display_help
            exit 1
            ;;
    esac
done