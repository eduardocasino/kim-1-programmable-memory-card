add_library(FatFs INTERFACE)


target_sources(FatFs INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/ff15/source/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/ff15/source/ffsystem.c
    ${CMAKE_CURRENT_LIST_DIR}/ff15/source/ffunicode.c
    ${CMAKE_CURRENT_LIST_DIR}/glue.c
    ${CMAKE_CURRENT_LIST_DIR}/spi_sdmmc.c
    ${CMAKE_CURRENT_LIST_DIR}/f_util.c
)

target_include_directories(FatFs INTERFACE
    ff15/source
)

target_link_libraries(FatFs INTERFACE
    hardware_rtc
    hardware_spi
    pico_stdlib
)
