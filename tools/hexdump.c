/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Human readable hexadecimal dump
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
#include <ctype.h>

#include "globals.h"

#define COLUMNS 16
#define HALF_COLUMNS ( COLUMNS / 2 )

status_t hexdump( FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    status_t status = SUCCESS;
    int rc;
	static char ascii[COLUMNS + 1];
	
	for ( size_t i = 0; i < size / 2; ++i )
    {
        if ( ! ( i % COLUMNS ) )
        {
            rc = fprintf( file, "%010lX: ", base_addr + (unsigned) i );
            if ( rc < 0 ) status = FAILURE;
            memset( ascii, 0, sizeof( ascii) );
        }
        else if ( ! ( i % HALF_COLUMNS ) )
        {
            rc = fputs( " ", file );
            if ( rc < 0 ) status = FAILURE;
        }

		rc = fprintf( file, "%02X ", ((uint8_t *)data)[i*2] );
        if ( rc < 0 ) status = FAILURE;

		ascii[i % COLUMNS] = isprint( ((uint8_t *)data)[i*2] ) ? ((uint8_t *)data)[i*2] : '.';

		if ( (i+1) % COLUMNS == 0 || i+1 == size / 2 )
        {
            size_t j;
			for ( j = (i+1) % COLUMNS; j && j < COLUMNS; ++j )
            {
				rc = fputs( "   ", file );
                if ( rc < 0 ) status = FAILURE;
			}
            if ( j && (i+1) % COLUMNS <= HALF_COLUMNS )
            {
				rc = fputs( " ", file );
                if ( rc < 0 ) status = FAILURE;
		    }
		    rc = fprintf( file, " %s\n", ascii);
            if ( rc < 0 ) status = FAILURE;
		}
	}
    if ( FAILURE == status )
    {
        fputs( "Error writing to output file\n", stderr );
    }

    return status;
}
