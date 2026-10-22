# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# cmake version
cmake_minimum_required(VERSION 3.13)

# Let editor tooling use the same include paths and compiler definitions as
# the Pico build.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(PWM_Helicopter C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(PWM_Helicopter)

# must match with executable name and source file names
target_sources(PWM_Helicopter PRIVATE #pwm-helicopter.c
                                        imu_demo.c
                                        mpu6050.c)

pico_enable_stdio_usb(PWM_Helicopter 1)
pico_enable_stdio_uart(PWM_Helicopter 0)

# Add pico_multicore which is required for multicore functionality
target_link_libraries(PWM_Helicopter pico_stdlib pico_multicore pico_bootsel_via_double_reset hardware_pwm hardware_irq)

# create map/bin/hex file etc.
pico_add_extra_outputs(PWM_Helicopter)
