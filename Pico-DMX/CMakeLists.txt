add_library(Pico-DMX INTERFACE)
target_sources(Pico-DMX INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/DmxInput.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/DmxOutput.cpp
)
target_include_directories(Pico-DMX INTERFACE
    src
)
target_link_libraries(Pico-DMX INTERFACE
        hardware_clocks
        hardware_dma
        hardware_pio
        pico_stdlib
)
