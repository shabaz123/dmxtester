cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(projname "dmxtester")
project(${projname})

pico_sdk_init()

add_subdirectory(Pico-DMX)

add_executable(${projname}
        main.cpp
        )

target_include_directories(${projname} PRIVATE
        Pico-DMX/src
        )

target_link_libraries(${projname}
        pico_stdlib
        pico_multicore 
        hardware_clocks
        hardware_spi 
        hardware_dma 
        hardware_pio 
        Pico-DMX
        )

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(${projname} 
        pico_cyw43_arch_none
        )
endif ()

# Enable stdio via USB, disable UART I/O (so UART is free for RS-485)
pico_enable_stdio_usb(${projname} 1)
pico_enable_stdio_uart(${projname} 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${projname})
