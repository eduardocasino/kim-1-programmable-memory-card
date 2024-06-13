#
# KIM-1 Programmable Memory Board
#   https://github.com/eduardocasino/kim-1-programmable-memory-card
#
#  Copyright (C) 2024 Eduardo Casino
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, Version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301, USA.
#

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

add_subdirectory(picowi)
add_subdirectory(fatfs)

# set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)

add_executable(mememul)

target_sources(mememul PRIVATE
        main.c
        mememul.c
        video.c
        dmacfg.c
        fdc.c
        imd.c
        wlan.c
        webserver.c
        httpd.c
        config.c
        )

target_compile_options(mememul PRIVATE "-Wno-builtin-macro-redefined")

pico_generate_pio_header(mememul ${CMAKE_CURRENT_LIST_DIR}/mememul.pio)
pico_generate_pio_header(mememul ${CMAKE_CURRENT_LIST_DIR}/video.pio)


# Script that reserves 132K of flash memory for config and places mem_map at
# the beginning of the physical RAM
pico_set_linker_script(mememul ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

# Enable USB output, disable UART output.
pico_enable_stdio_usb(mememul 1)
pico_enable_stdio_uart(mememul 0)

target_include_directories(mememul PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        picowi
        fatfs
        )

target_link_libraries(mememul PRIVATE
        pico_stdlib
        pico_multicore
        pico_sync
        hardware_pio
        hardware_dma
        hardware_rtc
        picowi
        FatFs
)

pico_add_extra_outputs(mememul)
