/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "write" command
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

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <getopt.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "globals.h"
#include "http.h"
#include "resources.h"
#include "scan.h"
#include "hexfile.h"
#include "binfile.h"
#include "str.h"

typedef status_t (*read_fn_t)( FILE *input, uint8_t *buffer, size_t buffer_size, mem_block_t **blocks );

typedef enum { BIN = 0, IHEX = 1, PAP = 2, PRG = 3, RAW = 4 } format_t;
typedef enum { ASCII, BINARY } format_type_t;

typedef struct {
    const char *format_string;
    format_t format;
    format_type_t type;
    read_fn_t read_fn;
} format_st_t;

static const format_st_t formats[] = {
    { "bin",     BIN,     BINARY, binfile_bin_read },
    { "ihex",    IHEX,    ASCII,  hexfile_intel_read },
    { "pap",     PAP,     ASCII,  hexfile_pap_read },
    { "prg",     PRG,     BINARY, binfile_prg_read },
    { "raw",     RAW,     BINARY, binfile_bin_read },
    { NULL }
};

typedef struct {
    struct {
        bool start;
        bool format;
        bool enable;
    } flags;
    char *hostname;
    uint16_t start;
    const format_st_t *format;
    uint8_t *data;
    char *input;
    bool enable;
} write_opt_t;

static status_t write_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s -h \n", myname , command );
    fprintf( stderr, "       %s %s IPADDR [-s OFFSET] -f {bin,ihex,pap,prg,raw} [-i FILE]|[-d STRING] [-e]\n\n", myname , command );

    fputs( "Arguments:\n", stderr );
    fputs( "    IPADDR                   Board IP address\n\n", stderr );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help              Show this help message and exit\n", stderr );
    fputs( "    -s | --start    OFFSET   Offset to write the data to\n", stderr );
    fputs( "    -f | --format   FORMAT   Input format (default: None):\n", stderr );
    fputs( "                    bin      Binary data\n", stderr );
    fputs( "                    ihex     Intel HEX format\n", stderr );
    fputs( "                    pap      MOS papertape format\n", stderr );
    fputs( "                    prg      Commodore PRG format\n", stderr );
    fputs( "                    raw      Internal format (for debugging)\n", stderr );
    fputs( "    -i | --input    FILE     File to read the data from\n", stderr );
    fputs( "    -d | --data     STRING   Binary string\n", stderr );
    fputs( "    -e | --enable            Also enable modified addresses\n", stderr );

    exit( status );
}

static status_t write_duplicate( char *myname, char *command, char opt )
{
    fprintf( stderr, "Duplicate option: -%c\n", opt );
    return write_usage( myname, command, FAILURE );
}

static status_t write_options( write_opt_t *options, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    static const struct option long_opts[] = {
        {"start",  required_argument, 0, 's' },
        {"format", required_argument, 0, 'f' },
        {"input",  required_argument, 0, 'i' },
        {"data",   required_argument, 0, 'd' },
        {"enable", no_argument,       0, 'e' },
        {0,        0,                 0,  0  }
    };

    memset( options, 0, sizeof( write_opt_t ) );

    if ( argc < 3 )
    {
        fputs( "Invalid number of arguments\n", stderr );
        return write_usage( myname, argv[1], FAILURE );
    }

    if ( !strcmp( argv[2], "-h" ) || !strcmp( argv[2], "-help" ) )
    {
        return write_usage( myname, argv[1], SUCCESS );

    }

    if ( *argv[2] == '-' )
    {
        fputs( "Host name is mandatory\n", stderr );
        return write_usage( myname, argv[1], FAILURE );

    }
    else
    {
        options->hostname = argv[2];
    }
    
    while (( opt = getopt_long( argc-2, &argv[2], "s:f:i:d:e", long_opts, &opt_index)) != -1 )
    {
        uint16_t num;
        int f_index = 0;

        switch( opt )
        {
            case 's':
                if ( options->flags.start++ )
                {
                    return write_duplicate( myname, argv[1], opt );
                }
                if ( 0 != get_uint16( optarg, &num ) )
                {
                    fprintf( stderr, "Invalid address: %s\n", optarg );
                    return write_usage( myname, argv[1], FAILURE );
                }
                options->start = num;
                break;
            
            case 'f':
                if ( options->flags.format++ )
                {
                    return write_duplicate( myname, argv[1], opt );
                }
                while ( NULL != formats[f_index].format_string )
                {
                    if ( ! strcmp( formats[f_index].format_string, optarg ) )
                    {
                        options->format = &formats[f_index];
                        break;
                    }
                    ++f_index;
                }
                if ( f_index > RAW )
                {
                    fprintf( stderr, "Invalid format: %s\n", optarg );
                    return write_usage( myname, argv[1], FAILURE );
                }
                break;
            
            case 'i':
                if ( options->input )
                {
                    return write_duplicate( myname, argv[1], opt );
                }
                options->input = optarg;
                break;
                        
            case 'd':
                if ( options->data )
                {
                    return write_duplicate( myname, argv[1], opt );
                }
                options->data = optarg;
                break;

            case 'e':
                if ( options->flags.enable++ )
                {
                    return write_duplicate( myname, argv[1], opt );
                }
                options->enable = true;
                break;

            default:
                return write_usage( myname, argv[1], FAILURE );
        }
    }

    if ( ! options->flags.format )
    {
        fputs( "Missing mandatory option: -f | --format\n", stderr );
        return write_usage( myname, argv[1], FAILURE );
    }

    if ( ! options->input  && ! options->data )
    {
        fputs( "Either '-i | --input' or '-d | --data' must be specified\n", stderr );
        return write_usage( myname, argv[1], FAILURE );
    }

    if ( options->input && options->data )
    {
        fputs( "Options '-i | --input' and '-f | --data' are mutually exclusive\n", stderr );
        return write_usage( myname, argv[1], FAILURE );
    }

    if ( options->format->format == IHEX || options->format->format == PAP || options->format->format == PRG )
    {
        if ( options->data )
        {
            fprintf( stderr, "Option '-d | --data' is incompatible with '%s' format\n", options->format->format_string );
            return write_usage( myname, argv[1], FAILURE );
        }
        if ( options->flags.start )
        {
            fprintf( stderr, "Option: -s | --start is incompatible with '%s' format\n", options->format->format_string );
            return write_usage( myname, argv[1], FAILURE );
        }
    }

    if ( ( options->format->format == BIN || options->format->format == RAW ) && ! options->flags.start )
    {
        fprintf( stderr, "Option '-s | --start' is mandatory for '%s' format\n", options->format->format_string );
        return write_usage( myname, argv[1], FAILURE );
    }

    return SUCCESS;
}

static status_t write_from_file( write_opt_t *options, http_t *http )
{
    const char query[] = "start=%4.4X&count=%4.4X";
    char query_buf[ sizeof( query ) ];

    char *f_flags;
    FILE *input;
    mem_block_t *blocks = NULL;

    status_t status = SUCCESS;

    f_flags = ( options->format->type == ASCII ) ? "r" : "rb";

    if ( NULL == ( input = fopen( options->input, f_flags ) ) )
    {
        perror( "Can't open input file" );
        status = FAILURE;
    }

    if ( SUCCESS == status )
    {
        uint8_t *buffer = malloc( MEMORY_SIZE * 2 );

        if ( NULL == buffer )
        {
            perror( "Can't allocate memory for write buffer" );
            status = FAILURE;
        }

        if ( SUCCESS == ( status = options->format->read_fn( input, buffer, BUFFER_SIZE, &blocks ) ) )
        {
            while ( NULL != blocks )
            {
                uint16_t start = blocks->flags.start ? blocks->start : options->start;
                uint8_t *b = blocks->flags.start ? &buffer[start] : buffer;
                size_t blen = blocks->flags.start ? BUFFER_SIZE - blocks->start : BUFFER_SIZE;
                const char *resource = ( options->format->format == RAW ) ? get_resource_path( RES_RANGE ) : get_resource_path( RES_RANGE_DATA );

                sprintf( query_buf, query, start, (uint32_t)blocks->count );
                http->data_size = blocks->count;

                if ( SUCCESS == ( status = http_send_request( http, PATCH, options->hostname, resource, query_buf, b, blen, http_read_callback ) ) )
                {
                    if ( options->flags.enable && options->enable )
                    {
                        http->data_size = 0;

                        if ( SUCCESS != ( status = http_send_request( http, PATCH, options->hostname, get_resource_path( RES_RANGE_ENABLE ), query_buf, NULL, 0, NULL ) ) )
                        {
                            break;
                        }
                    }
                }
                else
                {
                    break;
                }
                blocks = blocks->next;
            }
            free( buffer );
        }
    }

    fclose( input );
    hexfile_free_blocks( blocks );

    return status;
}

status_t write_from_string( write_opt_t *options, http_t *http )
{
    const char query[] = "start=%4.4X&count=%4.4X";
    char query_buf[ sizeof( query ) ];

    uint8_t *buffer;
    size_t size;

    status_t status = SUCCESS;

    if ( SUCCESS == ( status = str_process( options->data, &buffer, &size ) ) )
    {
        const char *resource = ( options->format->format == RAW ) ? get_resource_path( RES_RANGE ) : get_resource_path( RES_RANGE_DATA );

        sprintf( query_buf, query, options->start, (uint16_t) size );
        http->data_size = size;

        if ( SUCCESS != ( status = http_send_request( http, PATCH, options->hostname, resource, query_buf, buffer, size, http_read_callback ) ) )
        {
            if ( options->flags.enable && options->enable )
            {
                http->data_size = 0;

                status = http_send_request( http, PATCH, options->hostname, get_resource_path( RES_RANGE_ENABLE ), query_buf, NULL, 0, NULL );
            }
        }
        free( buffer );
    }

    return status;
}

status_t write_command( int argc, char **argv )
{
    write_opt_t options;
    http_t *http;

    status_t status = SUCCESS;

    if ( FAILURE == write_options( &options, argc, argv ) )
    {
        return FAILURE;
    }
    
    if ( NULL == ( http = http_init( options.hostname ) ) )
    {
        return FAILURE;
    }

    if ( options.input )
    {
        status = write_from_file( &options, http );
    }
    else
    {
        status = write_from_string( &options, http );
    }

    http_cleanup( http );

    return status;
}
