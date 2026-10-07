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
project(VGA_Animation_Demo C CXX ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# initialize the sdk
pico_sdk_init()

add_executable(VGA_Animation_Demo)

add_compile_options(-Ofast)

# must match with pio filename and executable name from above
pico_generate_pio_header(VGA_Animation_Demo ${CMAKE_CURRENT_LIST_DIR}/VGA/hsync.pio)
pico_generate_pio_header(VGA_Animation_Demo ${CMAKE_CURRENT_LIST_DIR}/VGA/vsync.pio)
pico_generate_pio_header(VGA_Animation_Demo ${CMAKE_CURRENT_LIST_DIR}/VGA/rgb4.pio)
pico_generate_pio_header(VGA_Animation_Demo ${CMAKE_CURRENT_LIST_DIR}/VGA/line_sync.pio)

# must match with executable name and source file names
target_sources(VGA_Animation_Demo PRIVATE   #rotary_encoder_and_vga.c
                                            # galton_16_peg.c
                                            #galton_16_peg_opt.c
                                            galton_16_peg_opt_kaelem.c
                                            VGA/vga16_graphics_v3.c)

# Pre-render the background into flash, regenerating with geometry/font changes.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GALTON_BACKGROUND ${CMAKE_CURRENT_BINARY_DIR}/galton_background.h)
add_custom_command(OUTPUT ${GALTON_BACKGROUND}
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/tools/generate_galton_background.py
        ${CMAKE_CURRENT_LIST_DIR}/galton_16_peg_opt_kaelem.c
        ${CMAKE_CURRENT_LIST_DIR}/VGA/font_glcd.c
        ${GALTON_BACKGROUND}
    DEPENDS tools/generate_galton_background.py galton_16_peg_opt_kaelem.c VGA/font_glcd.c
    VERBATIM)
target_sources(VGA_Animation_Demo PRIVATE ${GALTON_BACKGROUND})
target_include_directories(VGA_Animation_Demo PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# must match with executable name
target_link_libraries(  VGA_Animation_Demo
                        PRIVATE
                        pico_stdlib
                        pico_divider
                        pico_multicore
                        pico_sync
                        pico_bootsel_via_double_reset
                        pico_multicore
                        hardware_pio
                        hardware_dma
                        hardware_adc
                        hardware_irq
                        hardware_clocks
                        hardware_pll
                        hardware_spi)

# must match with executable name
pico_add_extra_outputs(VGA_Animation_Demo)
