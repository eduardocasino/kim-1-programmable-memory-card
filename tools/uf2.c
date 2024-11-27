/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * UF2 file format support
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "globals.h"
#include "uf2.h"

#define UF2_BLOCK_SIZE      512
#define UF2_MAX_DATA_SIZE   256
#define UF2_MAGIC_FIRST     0x0A324655      // "UF2\n"
#define UF2_MAGIC_SECOND    0x9E5D5157
#define UF2_MAGIC_FINAL     0x0AB16F30
#define UF2_FLAGS           0x00002000      // familyID present
#define UFS_BLOCK_DATA      0x00000100      // 256 bytes
#define UF2_PICO_FAMILY     0xE48BFF56

typedef struct {
    struct header_s {
        uint32_t magic_first;
        uint32_t magic_second;
        uint32_t flags;
        uint32_t data_destination;
        uint32_t data_size;
        uint32_t block_number;
        uint32_t total_blocks;
        uint32_t family;
    } __attribute__((packed)) header;
    uint8_t data[UF2_BLOCK_SIZE - sizeof( struct header_s ) - sizeof( uint32_t )];
    uint32_t magic_final;    
} __attribute__((packed)) uf2_block_t;


status_t uf2_write( FILE *file, uint8_t *data, size_t size, uint32_t address )
{
    static uf2_block_t block = {
        {
            LE32( UF2_MAGIC_FIRST ),
            LE32( UF2_MAGIC_SECOND ),
            LE32( UF2_FLAGS ),
            0,
            UF2_MAX_DATA_SIZE,
            0,
            0,
            LE32( UF2_PICO_FAMILY )
        },
        { 0 },
        UF2_MAGIC_FINAL
    };

    status_t status = SUCCESS;

    size_t total_blocks = (size + UF2_MAX_DATA_SIZE -1)/UF2_MAX_DATA_SIZE;

    for ( size_t block_num = 0; block_num < total_blocks; ++block_num )
    {
        size_t data_size = (block_num < total_blocks - 1 ) ? UF2_MAX_DATA_SIZE : size % UF2_MAX_DATA_SIZE;
        size_t offset = block_num * UF2_MAX_DATA_SIZE;

        block.header.data_destination = LE32( address + offset );
        block.header.block_number     = LE32( block_num );
        block.header.total_blocks     = LE32( total_blocks );

        memcpy( block.data, &data[offset], data_size );
        memset( &block.data[data_size], 0, UF2_MAX_DATA_SIZE - data_size );

        if ( 1 != fwrite( &block, sizeof( uf2_block_t ), 1, file ) )
        {
            perror( "Error writing to the output file" );
            status = FAILURE;
            break;
        }

    }

    return status;
}