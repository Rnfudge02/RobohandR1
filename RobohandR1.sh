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
generate_prompt_template() {
    local prompt_file="prompt_file.txt"
    local request=""
    local include_core=0
    local include_drivers=0
    local include_cmake=0
    local include_readme=0

    # Parse arguments after -a
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --core)
                include_core=1
                shift
                ;;
            --drivers)
                include_drivers=1
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
            --all)
                include_core=1
                include_drivers=1
                include_cmake=1
                include_readme=1
                shift
                ;;
            *)
                # Assume any other argument is the request text
                request+="$1\n"
                shift
                ;;
        esac
    done

    log_message "INFO" "Generating prompt template file"
    rm -rf ./$prompt_file

    # Add README if requested
    if [ $include_readme -eq 1 ] && [ -f "./README.md" ]; then
        log_message "INFO" "Including README"
        cat ./README.md >> $prompt_file
    fi

    # Add requested components
    echo -e "=== PROJECT STRUCTURE ===" >> $prompt_file
    if [ $include_core -eq 1 ]; then
        find ./Include/Core ./Src/Core -type f | sort >> $prompt_file
    fi
    if [ $include_drivers -eq 1 ]; then
        find ./Include/Drivers ./Src/Drivers -type f | sort >> $prompt_file
    fi

    # Add specific file content
    echo -e "\n=== CODE CONTENT ===" >> $prompt_file
    if [ $include_core -eq 1 ]; then
        for FILE in ./Include/Core/* ./Src/Core/*; do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi

    if [ $include_drivers -eq 1 ]; then
        for FILE in ./Include/Drivers/* ./Src/Drivers/*; do
            if [ -f "$FILE" ]; then
                echo -e "\n// File: $FILE" >> $prompt_file
                cat $FILE >> $prompt_file
            fi
        done
    fi

    if [ $include_cmake -eq 1 ] && [ -f "CMakeLists.txt" ]; then
        echo -e "\n=== CMAKE CONFIGURATION ===" >> $prompt_file
        cat CMakeLists.txt >> $prompt_file
    fi

    # Add user request if provided
    if [ -n "$request" ]; then
        echo -e "\n=== USER REQUEST ===" >> $prompt_file
        echo -e "$request" >> $prompt_file
    fi

    log_message "SUCCESS" "Prompt template created: $prompt_file"
}

# Function to build the project
build_project() {
    local verbose="$1"
    
    log_message "INFO" "Building Robohand Project"

    # Check for necessary compilers and tools
    check_arm_compiler || return 1
    check_ninja || return 1
    
    # Create build directory if it doesn't exist
    mkdir -p $BUILD_DIR
    rm -rf $BUILD_DIR/*
    cd $BUILD_DIR

    # Configure and build with Ninja
    if [ "$verbose" == "verbose" ]; then
        log_message "INFO" "Using verbose build mode"
        cmake .. -G Ninja -DCMAKE_VERBOSE_MAKEFILE=ON
    else
        cmake .. -G Ninja
    fi
    
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

# Function to analyze memory usage
analyze_memory() {
    # First check if ARM tools are available
    check_arm_compiler || return 1
    
    if [ ! -f "$ELF_FILE" ]; then
        log_message "ERROR" "ELF file not found: $ELF_FILE"
        log_message "INFO" "Build the project first with './$SCRIPT_NAME -b'"
        return 1
    fi

    print_banner "MEMORY ANALYSIS FOR RP2350 PROJECT"

    # Basic size information
    echo -e "\n${FG_CYAN}${BOLD}1. BASIC MEMORY USAGE${RESET}"
    ${PREFIX}size --format=berkeley "$ELF_FILE"

    # Detailed section sizes
    echo -e "\n${FG_CYAN}${BOLD}2. DETAILED SECTION SIZES${RESET}"
    ${PREFIX}size --format=sysv "$ELF_FILE"

    # Extract memory usage using size
    local TEXT_SIZE=$(${PREFIX}size --format=berkeley "$ELF_FILE" | grep -A1 "text" | tail -n1 | awk '{print $1}')
    local DATA_SIZE=$(${PREFIX}size --format=berkeley "$ELF_FILE" | grep -A1 "text" | tail -n1 | awk '{print $2}')
    local BSS_SIZE=$(${PREFIX}size --format=berkeley "$ELF_FILE" | grep -A1 "text" | tail -n1 | awk '{print $3}')

    # Calculate total RAM and FLASH usage
    local FLASH_USED=$TEXT_SIZE
    local RAM_USED=$((DATA_SIZE + BSS_SIZE))

    # Use bc if available, otherwise use basic math
    if command -v bc &> /dev/null; then
        local FLASH_PERCENT=$(echo "scale=2; ($FLASH_USED * 100) / $FLASH_TOTAL" | bc)
        local RAM_PERCENT=$(echo "scale=2; ($RAM_USED * 100) / $RAM_TOTAL" | bc)
    else
        local FLASH_PERCENT=$(( $FLASH_USED * 100 / $FLASH_TOTAL ))
        local RAM_PERCENT=$(( $RAM_USED * 100 / $RAM_TOTAL ))
    fi

    echo -e "\n${FG_CYAN}${BOLD}3. MEMORY USAGE SUMMARY${RESET}"
    echo -e "Flash usage: ${FG_GREEN}$FLASH_USED / $FLASH_TOTAL bytes (${FLASH_PERCENT}%)${RESET}"
    echo -e "RAM usage: ${FG_GREEN}$RAM_USED / $RAM_TOTAL bytes (${RAM_PERCENT}%)${RESET}"

    # RAM functions analysis
    echo -e "\n${FG_CYAN}${BOLD}4. RAM FUNCTIONS ANALYSIS${RESET}"
    echo -e "Sections placed in RAM:"
    ${PREFIX}readelf -S "$ELF_FILE" | grep -E "\.data\.ram_func|\.text\.ram_func" || echo "No RAM function sections found"

    # Extract RAM functions specifically
    echo -e "\nFunctions explicitly placed in RAM:"
    ${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | grep -E "\.data\.ram_func|\.text\.ram_func" | sort -nr -k2 || echo "No RAM functions found"

    # Calculate total RAM functions size
    local RAM_FUNC_SIZE=$(${PREFIX}size --format=sysv "$ELF_FILE" | grep -E "\.data\.ram_func|\.text\.ram_func" | awk '{sum += $2} END {print sum}')
    if [ -z "$RAM_FUNC_SIZE" ]; then
        RAM_FUNC_SIZE=0
    fi

    echo -e "Total RAM functions size: ${FG_GREEN}$RAM_FUNC_SIZE bytes${RESET}"
    
    # Calculate percentage if bc is available
    if command -v bc &> /dev/null; then
        local RAM_FUNC_PERCENT=$(echo "scale=2; ($RAM_FUNC_SIZE * 100) / $RAM_TOTAL" | bc)
        echo -e "Percentage of RAM used for functions: ${FG_GREEN}${RAM_FUNC_PERCENT}%${RESET}"
    else
        local RAM_FUNC_PERCENT=$(( $RAM_FUNC_SIZE * 100 / $RAM_TOTAL ))
        echo -e "Percentage of RAM used for functions: ${FG_GREEN}${RAM_FUNC_PERCENT}%${RESET}"
    fi

    # Find largest symbols
    echo -e "\n${FG_CYAN}${BOLD}5. TOP 20 LARGEST SYMBOLS${RESET}"
    ${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | grep -v " 0 " | tail -n 20 || echo "No symbols found"

    # Memory map
    if [ -f "$MAP_FILE" ]; then
        echo -e "\n${FG_CYAN}${BOLD}6. MEMORY MAP${RESET}"
        echo -e "Complete memory map available at: $MAP_FILE"
        ${PREFIX}readelf -e "$ELF_FILE" | grep -A15 "Program Headers" || echo "No program headers found"
    fi

    echo -e "\n${FG_CYAN}${BOLD}7. COMPILATION INFORMATION${RESET}"
    local COMPILER_VERSION=$(${PREFIX}gcc --version | head -n1)
    echo -e "Compiler: $COMPILER_VERSION"

    # Check if file was compiled with optimization
    local OPTIMIZE_INFO=$(${PREFIX}readelf -p .comment "$ELF_FILE" | grep -o "\-O[0-3s]" || echo "Unknown")
    echo -e "Optimization: $OPTIMIZE_INFO"

    print_banner "MEMORY USAGE ANALYSIS"

    # Human-readable size conversion
    human_readable() {
        echo $1 | awk '
            function human(x) {
                if (x < 1000) return x "B"
                x /= 1024
                if (x < 1000) return sprintf("%.1fKB", x)
                x /= 1024
                if (x < 1000) return sprintf("%.1fMB", x)
                x /= 1024
                return sprintf("%.1fGB", x)
            }
            {print human($1)}'
    }

    # Basic memory usage with human-readable sizes
    echo -e "\n${FG_CYAN}${BOLD}MEMORY SUMMARY${RESET}"
    local MEM_INFO=$(${PREFIX}size --format=berkeley "$ELF_FILE" | tail -1)
    local TEXT_SIZE=$(echo "$MEM_INFO" | awk '{print $1}')
    local DATA_SIZE=$(echo "$MEM_INFO" | awk '{print $2}')
    local BSS_SIZE=$(echo "$MEM_INFO" | awk '{print $3}')
    
    echo -e "Program Size (Flash): ${FG_GREEN}$(human_readable $TEXT_SIZE)${RESET} (Code + Constants)"
    echo -e "  Initialized Data: ${FG_GREEN}$(human_readable $DATA_SIZE)${RESET} (Global variables)"
    echo -e "  Zeroed Data: ${FG_GREEN}$(human_readable $BSS_SIZE)${RESET} (Uninitialized variables)"
    
    local TOTAL_RAM=$((DATA_SIZE + BSS_SIZE))
    local RAM_PERCENT=$(echo "scale=2; ($TOTAL_RAM * 100) / $RAM_TOTAL" | bc)
    echo -e "\nTotal RAM Usage: ${FG_YELLOW}$(human_readable $TOTAL_RAM)${RESET} / ${FG_CYAN}$(human_readable $RAM_TOTAL)${RESET} (${FG_GREEN}${RAM_PERCENT}%${RESET})"

    # Flash usage breakdown
    echo -e "\n${FG_CYAN}${BOLD}FLASH MEMORY BREAKDOWN${RESET}"
    ${PREFIX}size --format=sysv "$ELF_FILE" | awk '
    /^\.text/ || /^\.rodata/ || /^\.data/ {
        sum += $2
        printf "%-20s %10s (%.1f%% of Flash)\n", $1, $2, ($2/'$FLASH_TOTAL')*100
    }
    END {
        printf "%-20s %10s (%.1f%% of Flash)\n", "TOTAL", sum, (sum/'$FLASH_TOTAL')*100
    }'

    # Top memory consumers with proper headers
    echo -e "\n${FG_CYAN}${BOLD}TOP MEMORY USERS${RESET}"
    ${PREFIX}nm --print-size --size-sort --radix=d "$ELF_FILE" | \
    grep -v " 0 " | \
    awk '{
        # Extract relevant fields
        addr = $1;
        size = $2;
        type = $3;
        symbol = $4;
        for (i=5; i<=NF; i++) symbol = symbol " " $i;
        printf("%s|%d|%s\n", type, size, symbol);
    }' | \
    sort -t'|' -k2,2nr | \
    head -n 20 | \
    awk -F'|' '
    BEGIN {
        printf "%-4s %-12s %-12s %s\n", "Type", "Size(bytes)", "Human", "Symbol Name"
        printf "%-4s %-12s %-12s %s\n", "----", "-----------", "-----", "-----------"
    }
    {
        type = $1
        size = $2
        symbol = $3
        
        if (size > 1024*1024) {
            human = sprintf("%.1fMB", size/1024/1024)
        } else if (size > 1024) {
            human = sprintf("%.1fKB", size/1024)
        } else {
            human = sprintf("%dB", size)
        }
        
        printf "%-4s %-12d %-12s %s\n", type, size, human, symbol
    }'

    # Compilation information with better optimization detection
    echo -e "\n${FG_CYAN}${BOLD}COMPILATION INFORMATION${RESET}"
    local COMPILER_VERSION=$(${PREFIX}gcc --version | head -n1)
    local OPTIMIZE_INFO=$(${PREFIX}readelf -p .comment "$ELF_FILE" | grep -m1 -o "\-O[0-9a-z]*" || echo "Unknown")
    
    echo -e "Compiler: ${FG_GREEN}$COMPILER_VERSION${RESET}"
    echo -e "Optimization: ${FG_GREEN}${OPTIMIZE_INFO#-O}${RESET}"
    echo -e "Float ABI: ${FG_GREEN}hard${RESET}"
    echo -e "Core Features: ${FG_GREEN}Cortex-M33 + FPU + DSP${RESET}"
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
    if [ ! -f "./Robohand.doxyfile" ]; then
        doxygen -g Robohand.doxyfile
    
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

    # Generate documentation
    if doxygen Robohand.doxyfile; then
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
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-a" "AI-prompt" "Generate LLM prompt template [options: --core, --drivers, --cmake, --readme, --all]"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-b" "Build" "Build RobohandR1 project [optional: verbose]"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-d" "Document" "Generate Doxygen documentation"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-h" "Help" "Display this help message"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-i" "Init" "Initialize development environment (requires sudo)"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-m" "Memory" "Analyze memory usage (run after building)"
    printf "${FG_GREEN}%-4s %-15s${RESET} %s\n" "-s" "Serial" "Start serial communication [optional: device path]"
    echo -e ""
    echo -e "${FG_CYAN}${BOLD}Examples:${RESET}"
    echo -e "  ./$SCRIPT_NAME -b verbose    # Build with verbose output"
    echo -e "  ./$SCRIPT_NAME -m            # Analyze memory usage"
    echo -e "  ./$SCRIPT_NAME -s /dev/ttyACM1  # Connect to specific serial port"
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