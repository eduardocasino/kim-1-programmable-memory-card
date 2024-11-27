/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Binary string support
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
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "globals.h"
#include "scan.h"

#define STR_BUFFER_LENGTH 1024

status_t str_process( const char *str, uint8_t **buffer, size_t *size )
{
    size_t i = 0;
    int skip;
    uint8_t *b = malloc( STR_BUFFER_LENGTH );

    status_t status = SUCCESS;

    if ( NULL == b )
    {
        return FAILURE;
    }

    while ( *str != '\0' )
    {
        // Check for escape
        if ( *str == '\\' )
        {
            switch ( str[1] )
            {
                case '\\':
                    b[i++] = '\\';
                    skip = 2;
                    break;

                case '"':
                    b[i++] = '"';
                    skip = 2;
                    break;

                case '0':
                case '1':
                case '2':
                case '3':
                    ++str;
                    skip = get_octbyte( str, &b[i++] ) ? -1 : 3;
                    break;

                case 'x':
                    str += 2;
                    skip = get_hexbyte( str, &b[i++] ) ? -1 : 2;
                    break;

                default:
                    skip = -1;
            }

            if ( skip < 0 )
            {
                fputs( "Invalid escape sequence\n", stderr );
                status = FAILURE;
                break;
            }

            str += skip;
        }
        else
        {
            b[i++] = *(str++);
        }

    }

    if ( SUCCESS == status )
    {
        *buffer = b;
        *size = i;
    }

    return status;
} 