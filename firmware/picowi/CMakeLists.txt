# PicoWi project, see http://iosoft.blog/picowi for details

# Set debug or release version
#set (RELEASE 1)

cmake_minimum_required(VERSION 3.13)
include(../pico-sdk/external/pico_sdk_import.cmake)
pico_sdk_init()

#include($ENV{PICO_SDK_PATH}/tools/CMakeLists.txt)

# Set name of project (as PROJECT_NAME) and C/C++ standards
project(picowi C CXX ASM)
set (CMAKE_C_STANDARD 11)

set (PICO_BOARD pico_w)

# Set to 0 for Pico-W CYW43439, 1 for Murata 1DX (CYW4343W)
# It may be necessary to reduce PIO_SPI_FREQ when using an external device
#set (CHIP_4343W 1)

# Picowi library souce files
set (PICOWI_SRCE   picowi_pico.c  picowi_init.c
    picowi_wifi.c  picowi_ioctl.c picowi_scan.c 
    picowi_event.c picowi_join.c
    picowi_ip.c    picowi_udp.c   picowi_dhcp.c
    picowi_dns.c   picowi_net.c   picowi_tcp.c
    picowi_web.c)

set (FW_FILE firmware/fw_43439.c)

# Enable compiler warnings
add_compile_options(-Wall)

add_compile_options(-O3 -DNDEBUG)

# Create picowi library
add_library(picowi ${PICOWI_SRCE} ${FW_FILE})
target_link_libraries(picowi pico_stdlib)

# EOF
