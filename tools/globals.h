/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Globally available definitions
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

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef MEMCFG_GLOBALS_H
#define MEMCFG_GLOBALS_H

#define MEMORY_SIZE (64*1024)
#define BUFFER_SIZE (2*MEMORY_SIZE)

#define MEM_ATTR_CE_MASK    (1 << 0)
#define MEM_ATTR_RW_MASK    (1 << 1)
#define MEM_ATTR_ENABLED    0
#define MEM_ATTR_DISABLED   MEM_ATTR_CE_MASK
#define MEM_ATTR_RO         0
#define MEM_ATTR_RW         MEM_ATTR_RW_MASK

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define LE16(X) (uint16_t)( (( X << 8) & 0xFF00) | ((X >> 8) & 0xFF) )
#define LE32(X) (uint32_t)( (( X << 24) & 0xFF000000) | (( X << 8) & 0xFF0000) | ((X >> 8) & 0xFF00) | ((X >> 24) & 0xFF) )
#else
#define LE16(X) (X)
#define LE32(X) (X)
#endif
#define HOST16 LE16

#define MAX( a, b ) ( ( (a) > (b) ) ? (a) : (b) )

typedef enum {
    SUCCESS = 1,
    FAILURE = 0
} status_t;

typedef struct mem_block_s {
    struct {
        bool start;     // true if start is set, false otherwise
    } flags;
    uint16_t start;
    size_t count;
    struct mem_block_s *next;
} mem_block_t;

#endif