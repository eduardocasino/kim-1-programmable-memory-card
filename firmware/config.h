/*
 * Configuration for the KIM-1 Programmable Memory Board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 *  Copyright (C) 2024 Eduardo Casino
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, Version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#include "imd.h"

#define MEM_MAP_SIZE        0x10000

#define MAX_SSID_LEN        32
#define MAX_PASSWD_LEN      64

#define MEM_ATTR_ENABLED    0
#define MEM_ATTR_DISABLED   ( 1 << 8 )
#define MEM_ATTR_RDONLY     0
#define MEM_ATTR_WRITEABLE  ( 1 << 9 ) 

#define MEM_ATTR_CE_MASK    ( 1 << 8 )
#define MEM_ATTR_RW_MASK    ( 1 << 9  ) 

#define MEM_ATTR_MASK       ( MEM_ATTR_CE_MASK | MEM_ATTR_RW_MASK )
#define MEM_DATA_MASK       0xFF


typedef struct {
    uint16_t        memory[MEM_MAP_SIZE];
    struct {
        uint32_t    country;
        char        ssid[MAX_SSID_LEN];
        char        passwd[MAX_PASSWD_LEN];
    } network;
    struct {
        uint16_t    system;
        uint16_t    address;
    } video;
    struct {
        bool        enabled;
        bool        optswitch;
        uint16_t    usrram;
        uint16_t    sysram;
        struct {
            char        imagename[MAX_FILE_NAME_LEN+1];
            bool        readonly;
        } drives[MAX_DRIVES];
    } fdc;
} config_t;

extern config_t config;

// In order to do quick checks of memory availability (enabled/disabled) and writeability,
// we need to perform 16bit DMA transfers and twice the memory for storing the 64KBytes
// of the KIM-1 address map. The base address of the memmap has to have the lower 17 bits
// to 0 so we can calculate the target address by ORing the base with the value of the
// address bus shifted 1 bit to the left (because of the 16bit transfer).
// mem_map is defined in memmap_custom.ld and it is placed at the beginning of the
// physical RAM, so it is well aligned. Had to do that because setting the alignment
// with a directive and letting the linker to do the placement left not enough contiguous
// ram space for other variables.
//
extern uint16_t mem_map[MEM_MAP_SIZE];

void config_copy_default_memory_map( uint16_t * mem_map );

#endif /* CONFIG_H */