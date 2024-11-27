/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Support functions for ascii hex file formats
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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "globals.h"
#include "scan.h"

#define LINE_BUF_LEN 1024
#define PAP_WRITE_BYTES_PER_LINE 24
#define INTEL_WRITE_BYTES_PER_LINE 32

typedef enum { HEX_PAP, HEX_INTEL } hexfile_format_t;

typedef struct {
    hexfile_format_t format;
    char start_char;
    uint8_t *buffer;
    size_t buffer_size;
    int32_t current_addr;
    uint16_t lines;
    uint16_t checksum;
    char *line_buffer;
    int line_index;
    mem_block_t *blocks;
    bool complete;
} hexfile_t;

static status_t get_byte( char *buffer, int *index, uint8_t *value )
{    
    if ( get_hexbyte( &buffer[*index], value ) )
    {
        return FAILURE;
    }

    *index += 2;

    return SUCCESS;
}

static status_t get_word( char *buffer, int *index, uint16_t *value )
{
    if ( get_hexword( &buffer[*index], value ) )
    {
        return FAILURE;
    }

    *index += 4;

    return SUCCESS;
}

void hexfile_free_blocks( mem_block_t *blocks )
{
    mem_block_t *b = blocks;

    while ( NULL != b )
    {
        blocks = b->next;
        free( b );
        b = blocks;
    }

    return;
}

static status_t hexfile_end( hexfile_t *hex )
{
    status_t status = SUCCESS;
    uint16_t word;
    uint16_t checksum;

    if ( HEX_INTEL == hex->format )
    {
        if ( ! strncmp( ":00000001FF", hex->line_buffer, 11 ) )
        {
            hex->complete = true;
        }
    }
    else
    {
        if ( FAILURE == get_word( hex->line_buffer, &hex->line_index, &word ) || word != hex->lines - 1 )
        {
            fprintf( stderr, "Invalid line count in hex file at line %d\n", hex->lines );
            status = FAILURE;
        }
        else
        {
            hex->checksum = ( ( word >> 8 ) & 0xFF ) + ( word & 0xFF );

            if ( FAILURE == get_word( hex->line_buffer, &hex->line_index, &word ) || word != hex->checksum )
            {
                fprintf( stderr, "Invalid checksum in hex file at line %d\n", hex->lines );
                status = FAILURE;
            }
            else
            {
                hex->complete = true;
            }
        }
    }

    return status;
}

static status_t hexfile_read_record( hexfile_t *hex )
{
    status_t status = SUCCESS;
    uint8_t line_bytes;
    uint16_t load_address;
    uint8_t record_type;
    uint8_t nbytes;
    uint16_t checksum;

    ++hex->lines;
    hex->line_index = 0;

    if ( hex->start_char != hex->line_buffer[hex->line_index++] )
    {
        fprintf( stderr, "Malformed hex file at line %d\n", hex->lines );
        return FAILURE;
    }

    if ( FAILURE == get_byte( hex->line_buffer, &hex->line_index, &line_bytes ) )
    {
        fprintf( stderr, "Invalid register length in hex file at line %d\n", hex->lines );
        return FAILURE;
    }

    // line length should be:
    //  1 for ':'
    //  2 for byte count
    //  4 for address
    //  2 for record type ( 0 for PAP )
    //  2 * byte count for data
    //  2 for checksum ( 4 for PAP)
    //  == 11 + ( 2 * byte count ) in any case
    // 
    if ( strlen( hex->line_buffer ) < 11 + 2 * line_bytes )
    {
        fprintf( stderr, "Malformed hex file: line %d is too short\n", hex->lines );
        return FAILURE;
    }

    if ( 0 == line_bytes )
    {
        return hexfile_end( hex );
    }

    if ( FAILURE == get_word( hex->line_buffer, &hex->line_index, &load_address ) )
    {
        fprintf( stderr, "Invalid load address in hex file at line %d\n", hex->lines );
        return FAILURE;
    }

    if ( HEX_INTEL == hex->format )
    {
        if ( FAILURE == get_byte( hex->line_buffer, &hex->line_index, &record_type )
            || record_type != 0 )
        {
            // Only data records are supported
            fprintf( stderr, "Invalid or unsupported record type in hex file at line %d\n", hex->lines );
            return FAILURE;
        }
    }

    if ( load_address != hex->current_addr )
    {
        // New block
        mem_block_t *newblock = malloc( sizeof( sizeof( mem_block_t ) ) );

        if ( NULL == newblock )
        {
            perror( "Can't alloc memory for new hex block\n" );
            return FAILURE;
        }

        newblock->start = load_address;
        newblock->flags.start = true;
        newblock->count = 0;
        newblock->next = hex->blocks;
        hex->blocks = newblock;
        hex->current_addr = load_address;
    }

    hex->checksum = line_bytes + ( ( load_address >> 8 ) & 0xFF ) + ( load_address & 0xFF );

    for ( nbytes = 0; nbytes < line_bytes; ++nbytes )
    {
        uint8_t byte;

        if ( FAILURE == get_byte( hex->line_buffer, &hex->line_index, &byte ) )
        {
            fprintf( stderr, "Malformed hex file: Invalid byte at line %d\n", hex->lines );
            return FAILURE;
        }
        hex->buffer[hex->current_addr++] = byte;
        hex->blocks->count++;
        hex->checksum += byte;
    }

    // Get checksum
    if ( HEX_INTEL == hex->format)
    {
        status = get_byte( hex->line_buffer, &hex->line_index, (uint8_t *)&checksum );
    }
    else
    {
        status = get_word( hex->line_buffer, &hex->line_index, &checksum );
    }
    if ( FAILURE == status )
    {
        fprintf( stderr, "Malformed hex file: can't get checksum at line %d\n", hex->lines );
        return FAILURE;
    }

    if (  ( HEX_INTEL == hex->format && checksum != (uint8_t)(~hex->checksum + 1 ) )
        || ( HEX_PAP == hex->format && checksum != hex->checksum ) )
    {
        fprintf( stderr, "Malformed hex file: bad checksum at line %d\n", hex->lines );
        printf( "Calculated: %4.4X, read: %4.4X\n", hex->checksum, checksum );
        return FAILURE;
    }

    return status;
}

static status_t hexfile_read( hexfile_format_t format, FILE *file, uint8_t* data, size_t size, mem_block_t **blocks )
{
    status_t status = SUCCESS;
    static char line_buf[LINE_BUF_LEN];
    hexfile_t hex = { 0 };

    hex.format = format;
    hex.start_char = ( HEX_INTEL == format ) ? ':' : ';';
    hex.buffer = data;
    hex.buffer_size = size;
    hex.line_buffer = line_buf;
    hex.current_addr = -1;

    while ( fgets( line_buf, sizeof( line_buf ), file ) > 0 )
    {
        if ( FAILURE == ( status = hexfile_read_record( &hex ) ) )
        {
            hexfile_free_blocks( hex.blocks );
            hex.blocks = NULL;
            break;
        }
    }

    if ( SUCCESS == status && ( !feof( file ) || ! hex.complete ) )
    {
        fputs( "Unexpected end of hex file\n", stderr );
        status = FAILURE;
    }

    *blocks = hex.blocks;

    return status;
}

static status_t hexfile_write( hexfile_format_t format, FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    status_t status = SUCCESS;
    int rc;
    int byte_num = 0;
    uint16_t checksum = 0;
    uint16_t lines = 0;
    char start_char = ( HEX_INTEL == format ) ? ':' : ';';
    uint8_t max_bytes_per_line = ( HEX_INTEL == format ) ? INTEL_WRITE_BYTES_PER_LINE : PAP_WRITE_BYTES_PER_LINE;

    while ( byte_num < size / 2 )
    {
        if ( ! (byte_num % max_bytes_per_line) )
        {
            ++lines;

            // New line
            if ( byte_num )
            {
                // Print checksum
                if ( HEX_PAP == format )
                {
                    rc = fprintf( file, "%4.4X\n", checksum );
                }
                else
                {
                    rc = fprintf( file, "%2.2X\n", (uint8_t)(~checksum + 1 ) );
                }
                if ( rc < 0 ) status = FAILURE;
            }
            uint8_t bytes_in_line = size/2 - byte_num > max_bytes_per_line ? max_bytes_per_line : size/2 - byte_num;
            checksum = bytes_in_line + ( ( base_addr >> 8 ) & 0xFF ) + ( base_addr & 0xFF );
            rc = fprintf( file, "%c%2.2X%4.4X", start_char, bytes_in_line, (uint16_t) base_addr );
            if ( HEX_INTEL == format )
            {
                rc = fputs( "00", file );
            }
            if ( rc < 0 ) status = FAILURE;
        }
        rc = fprintf( file, "%2.2X", data[byte_num*2] );
        if ( rc < 0 ) status = FAILURE;

        checksum += data[byte_num*2];
        ++byte_num; ++base_addr;
    }

    if ( HEX_PAP == format )
    {
        if ( fprintf( file, "%4.4X\n", checksum ) < 0
            || fprintf( file, ";00%4.4X%4.4X\n", lines, ( ( lines >> 8 ) & 0xFF ) + ( lines & 0xFF ) ) < 0 )
        {
            status = FAILURE;
        } 
    }
    else
    {
        if ( fprintf( file, "%2.2X\n", (uint8_t)(~checksum + 1 ) ) < 0
            || fputs( ":00000001FF\n", file ) < 0 )
        {
            status = FAILURE;
        }
    }    

    return status;
}

status_t hexfile_pap_write( FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    return hexfile_write( HEX_PAP, file, data, size, base_addr );
}

status_t hexfile_intel_write( FILE *file, uint8_t* data, size_t size, uint64_t base_addr )
{
    return hexfile_write( HEX_INTEL, file, data, size, base_addr );
}

status_t hexfile_pap_read( FILE *file, uint8_t* data, size_t size, mem_block_t **blocks )
{
    return hexfile_read( HEX_PAP, file, data, size, blocks );
}

status_t hexfile_intel_read( FILE *file, uint8_t* data, size_t size, mem_block_t **blocks )
{
    return hexfile_read( HEX_INTEL, file, data, size, blocks );
}


