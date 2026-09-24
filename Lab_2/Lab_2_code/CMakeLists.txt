# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
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
project(Galton_Board C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(Galton_Board)

pico_generate_pio_header(Galton_Board ${CMAKE_CURRENT_LIST_DIR}/VGA/hsync.pio)
pico_generate_pio_header(Galton_Board ${CMAKE_CURRENT_LIST_DIR}/VGA/vsync.pio)
pico_generate_pio_header(Galton_Board ${CMAKE_CURRENT_LIST_DIR}/VGA/rgb.pio)

target_sources(Galton_Board PRIVATE galton.c VGA/vga16_graphics_v3.c)

target_link_libraries(  Galton_Board 
                        pico_divider
                        pico_multicore
                        hardware_pll
                        pico_stdlib 
                        pico_bootsel_via_double_reset 
                        hardware_spi 
                        hardware_timer 
                        hardware_irq 
                        pico_sync
                        hardware_sync
                        hardware_clocks
                        hardware_vreg
                        hardware_adc
                        hardware_pio
                        hardware_dma)

pico_add_extra_outputs(Galton_Board)