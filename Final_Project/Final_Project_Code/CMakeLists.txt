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

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(MPU6050_IMU C CXX ASM)

set(pioasm_DIR "C:/Users/Kaelem/.pico-sdk/tools/2.3.1/pioasm")

# initialize the sdk
pico_sdk_init()

add_executable(MPU6050_IMU)

# must match with pio filename and executable name from above
pico_generate_pio_header(MPU6050_IMU ${CMAKE_CURRENT_LIST_DIR}/VGA/hsync.pio)
pico_generate_pio_header(MPU6050_IMU ${CMAKE_CURRENT_LIST_DIR}/VGA/vsync.pio)
pico_generate_pio_header(MPU6050_IMU ${CMAKE_CURRENT_LIST_DIR}/VGA/rgb.pio)

# must match with executable name and source file names
target_sources(MPU6050_IMU PRIVATE imu_training_data.c
                                    imu.c bowling.c basketball.c cup_pong.c
                                    VGA/vga16_graphics_v3.c mpu6050.c)

# Add pico_multicore which is required for multicore functionality
target_link_libraries(  MPU6050_IMU
                        pico_stdlib
                        pico_bootsel_via_double_reset
                        pico_multicore
                        hardware_pwm
                        hardware_dma
                        hardware_irq
                        hardware_adc
                        hardware_pio
                        hardware_sync
                        hardware_i2c
                        hardware_clocks
                        )

pico_enable_stdio_usb(MPU6050_IMU 1)
pico_enable_stdio_uart(MPU6050_IMU 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(MPU6050_IMU)

# Separate first-game prototype. The data-collection target remains available.
add_executable(BOWLING_DEMO bowling_demo.c bowling_model.c imu.c mpu6050.c
               VGA/vga16_graphics_v3.c)
foreach(pio_file hsync vsync rgb)
    pico_generate_pio_header(BOWLING_DEMO ${CMAKE_CURRENT_LIST_DIR}/VGA/${pio_file}.pio)
endforeach()
target_link_libraries(BOWLING_DEMO pico_stdlib pico_bootsel_via_double_reset
    pico_multicore hardware_dma hardware_irq hardware_pio hardware_sync
    hardware_i2c hardware_clocks)
pico_enable_stdio_usb(BOWLING_DEMO 1)
pico_enable_stdio_uart(BOWLING_DEMO 0)
pico_add_extra_outputs(BOWLING_DEMO)
