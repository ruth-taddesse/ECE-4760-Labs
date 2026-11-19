add_executable(SPORTS_CONTROLLER wireless/controller.c wireless/radio.c wireless/wire.c
    imu.c mpu6050.c bowling_model.c)
add_executable(SPORTS_GROUND bowling_demo.c sports_views.c bowling_model.c
    wireless/radio.c wireless/wire.c VGA/vga16_graphics_v3.c)
target_compile_definitions(SPORTS_GROUND PRIVATE SPORTS_WIRELESS_GROUND=1 SPORTS_SINGLE_BUFFER=1)
foreach(target SPORTS_CONTROLLER SPORTS_GROUND)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(${target} pico_stdlib pico_multicore pico_rand pico_cyw43_arch_lwip_poll
        hardware_adc hardware_i2c hardware_clocks hardware_dma hardware_pio hardware_sync)
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_add_extra_outputs(${target})
endforeach()
foreach(pio_file hsync vsync rgb)
    pico_generate_pio_header(SPORTS_GROUND ${CMAKE_CURRENT_LIST_DIR}/../VGA/${pio_file}.pio)
endforeach()
