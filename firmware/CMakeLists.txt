cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(mememu)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Maybe not necessary, check performance
set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)

add_executable(mememu)

target_sources(mememu PRIVATE mememu.c)

# Enable USB output, disable UART output.
pico_enable_stdio_usb(mememu 1)
pico_enable_stdio_uart(mememu 0)

target_link_libraries(mememu PRIVATE pico_stdlib)
pico_add_extra_outputs(mememu)
