/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Support functions for binary file formats
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
#include <string.h>
#include <stdlib.h>

#include "globals.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

status_t binfile_bin_read( FILE *file, uint8_t* data, size_t size, mem_block_t **blocks )
{
    mem_block_t *b = malloc( sizeof( mem_block_t) );

    if ( NULL == b )
    {
        perror( "Can't alloc memory for new bin block\n" );
        return FAILURE;
    }

    if ( fseek( file, 0L, SEEK_END ) )
    {
        perror( "Can't determine file size" );
        return FAILURE;
    }

    b->flags.start = false;
    b->count = ftell( file );
    b->next = NULL;

    rewind( file );

    if ( b->count > size || b->count == 0 )
    {
        fputs( "Invalid file size\n", stderr );
        return FAILURE;
    }

    *blocks = b;

    if ( fread( data, 1, b->count, file ) != b->count )
    {
        perror( "Error reading from binary file" );
        return FAILURE;
    }

    return SUCCESS;
}

status_t binfile_prg_read( FILE *file, uint8_t* data, size_t size, mem_block_t **blocks )
{
    status_t status = FAILURE;
    uint16_t header;

    mem_block_t *b = malloc( sizeof( mem_block_t) );

    if ( NULL == b )
    {
        perror( "Can't alloc memory for new prg block\n" );
        return FAILURE;
    }

    if ( fseek( file, 0L, SEEK_END ) )
    {
        perror( "Can't determine file size" );
        return FAILURE;
    }

    b->flags.start = true;
    b->count = ftell( file ) - 2;
    b->next = NULL;

    if ( b->count < 1 || b->count > size )
    {
        fputs( "Invalid file length\n", stderr );
        return FAILURE;
    }

    rewind( file );

    if ( 1 == fread( &header, 2, 1, file ) )
    {
        b->start = HOST16( header );
        *blocks = b;
        status = ( fread( data, 1, b->count, file ) != b->count ) ? FAILURE : SUCCESS;
    }
    else
    {
        status = FAILURE;
    }

    if ( FAILURE == status )
    {
        perror( "Error reading from binary file" );
    }

    return status;
}

status_t binfile_bin_write( FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    UNUSED( base_addr );

    status_t status = SUCCESS;
    int rc;
	
	for ( size_t i = 0; i < size / 2; ++i )
    {
        rc = fwrite( &data[i*2], 1, 1, file );
        if ( rc < 0 ) status = FAILURE;
    }

    return status;
}

status_t binfile_prg_write( FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    status_t status = SUCCESS;
    int rc;
    uint16_t header = LE16( (uint16_t)base_addr );

    rc = fwrite( &header, 2, 1, file );
    if ( rc < 0 ) status = FAILURE;

    if ( FAILURE == binfile_bin_write( file, data, size, base_addr ) )
    {
        status = FAILURE;
    }

    return status;
}

status_t binfile_raw_write( FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    UNUSED( base_addr );

    int rc = fwrite( data, 1, size, file );

    return ( rc < 0 ) ? FAILURE : SUCCESS;
}
