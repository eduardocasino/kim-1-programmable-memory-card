cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico-sdk/external/pico_sdk_import.cmake)

project (mememul CXX C ASM)
set (CMAKE_C_STANDARD 11)
set (CMAKE_CXX_STANDARD 17)

set (PICO_BOARD pico_w)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Can't use or the http server does not fit :( 
# set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)

add_executable(mememul)

target_sources(mememul PRIVATE
        mememul.c
        piocfg.c
        dmacfg.c
        wlan.c
        webserver.c
        httpd.c
        config.c
        )

pico_generate_pio_header(mememul ${CMAKE_CURRENT_LIST_DIR}/mememul.pio)

# Script that reserves 132K of flash memory for config and places mem_map at
# the beginning of the physical RAM
pico_set_linker_script(mememul ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

# Enable USB output, disable UART output.
pico_enable_stdio_usb(mememul 1)
pico_enable_stdio_uart(mememul 0)

target_include_directories(mememul PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        )

target_link_libraries(mememul PRIVATE
        pico_stdlib
        hardware_pio
        hardware_dma
        pico_cyw43_arch_lwip_threadsafe_background
        )

pico_add_extra_outputs(mememul)
