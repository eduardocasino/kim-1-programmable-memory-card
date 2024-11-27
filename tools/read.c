/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "read" command
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
#include "hexdump.h"
#include "hexfile.h"
#include "binfile.h"

typedef status_t (*print_fn_t)( FILE *output, const uint8_t *buffer, size_t buffer_size, uint64_t base_addr );

typedef enum { HEXDUMP = 0, BIN = 1, IHEX = 2, PAP = 3, PRG = 4, RAW = 5 } format_t;
typedef enum { ASCII, BINARY } format_type_t;

typedef struct {
    const char *format_string;
    format_t format;
    format_type_t type;
    print_fn_t print_fn;
} format_st_t;

static const format_st_t formats[] = {
    { "hexdump", HEXDUMP, ASCII,  hexdump },
    { "bin",     BIN,     BINARY, binfile_bin_write },
    { "ihex",    IHEX,    ASCII,  hexfile_intel_write },
    { "pap",     PAP,     ASCII,  hexfile_pap_write },
    { "prg",     PRG,     BINARY, binfile_prg_write },
    { "raw",     RAW,     BINARY, binfile_raw_write },
    { NULL }
};

typedef struct {
    struct {
        bool start;
        bool count;
        bool format;
    } flags;
    char *hostname;
    uint16_t start;
    uint32_t count;
    const format_st_t *format;
    char *output_filename;
} read_opt_t;

static status_t read_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s -h \n", myname , command );
    fprintf( stderr, "       %s %s IPADDR -s OFFSET [-c COUNT ][-f {hexdump,bin,ihex,pap,prg,raw}] [-o FILE]\n\n", myname , command );

    fputs( "Arguments:\n", stderr );
    fputs( "    IPADDR                   Board IP address\n\n", stderr );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help              Show this help message and exit\n", stderr );
    fputs( "    -s | --start    OFFSET   Offset from where read the data\n", stderr );
    fputs( "    -c | --count    COUNT    Number of bytes to read (default: 256)\n", stderr );
    fputs( "    -f | --format   FORMAT   Format of the dumped data (default: hexdump):\n", stderr );
    fputs( "                    hexdump  ASCII hex dump in human readable format\n", stderr );
    fputs( "                    bin      Binary data\n", stderr );
    fputs( "                    ihex     Intel HEX format\n", stderr );
    fputs( "                    pap      MOS papertape format\n", stderr );
    fputs( "                    prg      Commodore PRG format\n", stderr );
    fputs( "                    raw      Internal format (for debugging)\n", stderr );
    fputs( "    -o | --output   FILE     File to save the data to (default: stdout)\n", stderr );

    exit( status );
}

static status_t read_duplicate( char *myname, char *command, char opt )
{
    fprintf( stderr, "Duplicate option: -%c\n", opt );
    return read_usage( myname, command, FAILURE );
}

static status_t read_options( read_opt_t *options, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    static const struct option long_opts[] = {
        {"start",  required_argument, 0, 's' },
        {"count",  required_argument, 0, 'c' },
        {"format", required_argument, 0, 'f' },
        {"output", required_argument, 0, 'o' },
        {0,        0,                 0,  0  }
    };

    memset( options, 0, sizeof( read_opt_t ) );

    if ( argc < 3 )
    {
        fputs( "Invalid number of arguments\n", stderr );
        return read_usage( myname, argv[1], FAILURE );
    }

    if ( !strcmp( argv[2], "-h" ) || !strcmp( argv[2], "-help" ) )
    {
        return read_usage( myname, argv[1], SUCCESS );
    }

    if ( *argv[2] == '-' )
    {
        fputs( "Host name is mandatory\n", stderr );
        return read_usage( myname, argv[1], FAILURE );
    }
    else
    {
        options->hostname = argv[2];
    }
    
    while (( opt = getopt_long( argc-2, &argv[2], "s:c:f:o:", long_opts, &opt_index)) != -1 )
    {
        uint32_t num;
        int f_index = 0;

        switch( opt )
        {
            case 's':
                if ( options->flags.start++ )
                {
                    return read_duplicate( myname, argv[1], opt );
                }
                if ( 0 != get_uint16( optarg, (uint16_t *)&num ) )
                {
                    fprintf( stderr, "Invalid address: %s\n", optarg );
                    return read_usage( myname, argv[1], FAILURE );
                }
                options->start = (uint16_t) num;
                break;
            
            case 'c':
                if ( options->flags.count++ )
                {
                    return read_duplicate( myname, argv[1], opt );
                }
                if ( 0 != get_uint32( optarg, &num ) || num > 0x10000 )
                {
                    fprintf( stderr, "Invalid count: %s\n", optarg );
                    return read_usage( myname, argv[1], FAILURE );
                }
                options->count = num;
                break;
            
            case 'f':
                if ( options->flags.format++ )
                {
                    return read_duplicate( myname, argv[1], opt );
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
                    return read_usage( myname, argv[1], FAILURE );
                }
                break;
            
            case 'o':
                if ( options->output_filename )
                {
                    return read_duplicate( myname, argv[1], opt );
                }
                options->output_filename = optarg;
                break;
                        
            default:
                return read_usage( myname, argv[1], FAILURE );
        }
    }

    if ( ! options->flags.start )
    {
        fputs( "Missing mandatory option: -s | --start\n", stderr );
        return read_usage( myname, argv[1], FAILURE );
    }

    if ( ! options->flags.format )
    {
        options->format = &formats[0];
    }

    if ( ! options->flags.count )
    {
        options->count = 0x100;
    }

    if ( options->count > 0x10000 - options->start )
    {
        options->count = (uint16_t)( 0x10000-options->start );
    }

    if ( ( options->format->format == BIN ||
           options->format->format == RAW ||
           options->format->format == PRG
         ) && ! options->output_filename )
    {
        fprintf( stderr, "Option '-o | --output' is required for '%s' format\n", options->format->format_string );
        return read_usage( myname, argv[1], FAILURE );
    }

    return SUCCESS;
}

status_t read_command( int argc, char **argv )
{
    const char query[] = "start=%4.4X&count=%4.4X";
    char query_buf[ sizeof( query ) ];

    read_opt_t options;
    status_t status = SUCCESS;
    http_t *http;
    FILE *output;

    if ( FAILURE == read_options( &options, argc, argv ) )
    {
        return FAILURE;
    }
    
    sprintf( query_buf, query, options.start, options.count );

    if ( NULL == ( http = http_init( options.hostname ) ) )
    {
        return FAILURE;
    }

    if ( ! options.output_filename )
    {
        output = stdout;
    }
    else
    {
        char *f_flags = ( options.format->type == ASCII ) ? "w" : "wb";

        if ( NULL == ( output = fopen( options.output_filename, f_flags ) ) )
        {
            perror( "Can't open output file" );
            status = FAILURE;
        }
    }

    if ( SUCCESS == status )
    {
        uint8_t *buffer = malloc( MEMORY_SIZE * 2 );

        if ( NULL == buffer )
        {
            perror( "Can't allocate memory for read buffer" );
            status = FAILURE;
        }

        if ( SUCCESS == ( status = http_send_request( http, GET, options.hostname, get_resource_path( RES_RANGE ), query_buf, buffer, MEMORY_SIZE * 2, http_write_callback ) ) )
        {
            status = options.format->print_fn( output, http->buffer, http->transferred_bytes, (uint64_t) options.start );
        }

        free( buffer );

        if ( output != stdout && fclose( output ) )
        {
            perror( "Error while closing the output file" );
            status = FAILURE;
        }
    }
    
    http_cleanup( http );

    return status;
}