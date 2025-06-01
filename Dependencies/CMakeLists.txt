cmake_minimum_required (VERSION 3.13)

set(ARCH_ONLY_FLAGS
    -mcpu=cortex-m33
    -mthumb
    -march=armv8-m.main+fp+dsp
    -mfloat-abi=hard
    -mfpu=fpv5-sp-d16
    -O3
)

# Pull in Pico SDK (must be before project)
include(../pico_sdk_import.cmake) 

set(CMSISDSP ${CMAKE_CURRENT_LIST_DIR}/cmsis-dsp)
set(CMSISCORE ${CMAKE_CURRENT_LIST_DIR}/cmsis-core/CMSIS/Core)



# Define the project
project (cmsis-dsp VERSION 0.1)

# Initialise the Pico SDK
pico_sdk_init()

add_subdirectory(${CMSISDSP}/Source bin_dsp)

target_compile_options(CMSISDSP PUBLIC
    ${ARCH_ONLY_FLAGS}
    -Wsign-compare
    -Wdouble-promotion
    -O3
    -ffast-math
    -DNDEBUG
    -Wall
    -Wextra 
    -Werror
    -fshort-enums 
    #-fshort-wchar
)