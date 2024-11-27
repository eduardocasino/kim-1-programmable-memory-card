/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "config" command
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
#include "yaml.h"
#include "scan.h"

typedef struct range_s {
    uint16_t start;
    uint16_t end;
} range_t;

typedef struct {
    range_t *ranges;
    size_t n_ranges;
} range_array_t;

typedef struct {
    struct {
        bool video_address;
    } flags;
    char *hostname;
    range_array_t disable;
    range_array_t enable;
    range_array_t ro;
    range_array_t rw;
    uint16_t video_address;
    char *input_filename;
    char *output_filename;
} config_opt_t;

static status_t get_range( char *optarg, range_t *range )
{
    uint64_t number;
    char *endc;

    number = strtoul( optarg, &endc, 0 );

    if ( *endc != '-' || isblank( *++endc ) || ! *endc || *endc == '-' || *endc == '+' )
    {
        fprintf( stderr, "Invalid range: '%s'\n", optarg );
        return FAILURE;
    }
    if ( number > 0xffff )
    {
        fprintf( stderr, "Range start too large: %s'\n", optarg );
        return FAILURE;
    }

    range->start = (uint16_t) number;

    number = strtoul( endc, &endc, 0 );

    if ( number > 0xffff || number < range->start )
    {
        fprintf( stderr, "Range end too large or smaller than start: %s'\n", optarg );
        return FAILURE;
    }

    range->end = (uint32_t) number;

    return SUCCESS;
}

static status_t realloc_range( range_t **range, size_t new_size )
{
    range_t *new_range;
    status_t status = SUCCESS;


    if ( NULL == ( new_range = realloc( *range, new_size ) ) )
    {
        perror( "Can't allocate memory" );
        free( *range );
        status = FAILURE;
    }

    *range = new_range;
  
    return status;
}

static status_t config_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s [-h] \n", myname , command );
    fprintf( stderr, "       %s %s IPADDR [-o FILE]\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR [-d RANGE [-d RANGE ...]] [-e RANGE [-e RANGE ...]] \\\n", myname , command );
    fputs( "\t\t\t[-r RANGE [-r RANGE ...]] [-w RANGE [-w RANGE ...]] \\\n", stderr );
    fputs( "\t\t\t[-v OFFSET] [-i FILE]\n\n", stderr );

    fputs( "Arguments:\n", stderr );
    fputs( "    IPADDR                   Board IP address\n\n", stderr );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help              Show this help message and exit\n", stderr );
    fputs( "    -d | --disable  RANGE    Disable the address range\n", stderr );
    fputs( "    -e | --enable   RANGE    Enable the address range\n", stderr );
    fputs( "    -r | --readonly RANGE    Make the address range read-only (ROM)\n", stderr );
    fputs( "    -w | --writable RANGE    Make the address range writable (RAM)\n", stderr );
    fputs( "    -v | --video    OFFSET   Video memory start address\n", stderr );
    fputs( "    -i | --input    FILE     File to read the config from\n", stderr );
    fputs( "    -o | --output   FILE     File to save the config to\n", stderr );

    exit( status );
}

static status_t config_duplicate( char *myname, char *command, char opt )
{
    fprintf( stderr, "Duplicate option: -%c\n", opt );
    return config_usage( myname, command, FAILURE );
}

static status_t config_options( config_opt_t *options, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    static const struct option long_opts[] = {
        {"disable",  required_argument, 0, 'd' },
        {"enable",   required_argument, 0, 'e' },
        {"readonly", required_argument, 0, 'r' },
        {"writable", required_argument, 0, 'w' },
        {"video",    required_argument, 0, 'v' },
        {"input",    required_argument, 0, 'i' },
        {"output",   required_argument, 0, 'o' },
        {0,          0,                 0,  0  }
    };

    memset( options, 0, sizeof( config_opt_t ) );

    if ( argc < 3 )
    {
        fputs( "Invalid number of arguments\n", stderr );
        return config_usage( myname, argv[1], FAILURE );
    }

    if ( !strcmp( argv[2], "-h" ) || !strcmp( argv[2], "-help" ) )
    {
        return config_usage( myname, argv[1], SUCCESS );
    }

    if ( *argv[2] == '-' )
    {
        fputs( "Host name is mandatory\n", stderr );
        return config_usage( myname, argv[1], FAILURE );
    }
    else
    {
        options->hostname = argv[2];
    }

    while (( opt = getopt_long( argc-2, &argv[2], "d:e:r:w:v:i:o:", long_opts, &opt_index)) != -1 )
    {
        range_t range, *newp;

        switch( opt )
        {
            case 'd':
                if ( FAILURE == realloc_range( &options->disable.ranges, (options->disable.n_ranges+1) * sizeof( range_t ) ) )
                {
                    return FAILURE;
                }
                if ( FAILURE == get_range( optarg, &options->disable.ranges[options->disable.n_ranges++] ) )
                {
                    return config_usage( myname, argv[1], FAILURE );
                }
                break;
            
            case 'e':
                if ( FAILURE == realloc_range( &options->enable.ranges, (options->enable.n_ranges+1) * sizeof( range_t ) ) )
                {
                    return FAILURE;
                }
                if ( FAILURE == get_range( optarg, &options->enable.ranges[options->enable.n_ranges++] ) )
                {
                    return config_usage( myname, argv[1], FAILURE );
                }
                break;
            
            case 'r':
                if ( FAILURE == realloc_range( &options->ro.ranges, (options->ro.n_ranges+1) * sizeof( range_t ) ) )
                {
                    return FAILURE;
                }
                if ( FAILURE == get_range( optarg, &options->ro.ranges[options->ro.n_ranges++] ) )
                {
                    return config_usage( myname, argv[1], FAILURE );
                }
                break;

            case 'w':
                if ( FAILURE == realloc_range( &options->rw.ranges, (options->rw.n_ranges+1) * sizeof( range_t ) ) )
                {
                    return FAILURE;
                }
                if ( FAILURE == get_range( optarg, &options->rw.ranges[options->rw.n_ranges++] ) )
                {
                    return config_usage( myname, argv[1], FAILURE );
                }
                break;
            
            case 'v':
                if ( options->flags.video_address++ )
                {
                    return config_duplicate( myname, argv[1], opt );
                }
                if ( 0 != get_uint16( optarg, &options->video_address ) )
                {
                    fprintf( stderr, "Invalid address: %s'\n", optarg );
                    return config_usage( myname, argv[1], FAILURE );
                }
                if ( options->video_address < 0x2000 || options->video_address > 0xDFFF || options->video_address % 0x2000 )
                {
                    fprintf( stderr, "Invalid K-1008 address: 0x%4.4X\n", options->video_address );
                    return config_usage( myname, argv[1], FAILURE );
                }
                break;

            case 'i':
                if ( options->input_filename )
                {
                    return config_duplicate( myname, argv[1], opt );

                }
                options->input_filename = optarg;
                break;
                        
            case 'o':
                if ( options->output_filename )
                {
                    return config_duplicate( myname, argv[1], opt );
                }
                options->output_filename = optarg;
                break;
                        
            default:
                return config_usage( myname, argv[1], FAILURE );
        }
    }

    return SUCCESS;
}

static status_t config_print_section( FILE *file, uint16_t start, uint16_t end, bool enabled, bool readonly )
{
    if ( EOF != fputs( "---\n", file ) &&
         EOF != fprintf( file, "start: 0x%4.4x\n", start ) &&
         EOF != fprintf( file, "end: 0x%4.4x\n", end ) &&
         EOF != fprintf( file, "enabled: %s\n", enabled ? "true" : "false" ) &&
         EOF != fprintf( file, "type: %s\n", readonly ? "rom" : "ram" ) )
    {
        return SUCCESS;
    }
    
    return FAILURE;
}

static status_t config_print_config( char *output, http_t *http, const char *host, uint8_t *buffer, size_t buffer_size )
{
    FILE *file;
    status_t status = FAILURE;

    if ( !output )
    {
        file = stdout;
    }
    else
    {
        if ( NULL == ( file = fopen( output, "w" ) ) )
        {
            perror( "Can't open output file" );
            return FAILURE;
        }
    }
    
    if ( SUCCESS == ( status = http_send_request( http, GET, host, get_resource_path( RES_VIDEO ), NULL, buffer, buffer_size, http_write_callback ) ) )
    {
        fprintf( file, "#\n# K-1008 memory at 0x%s\n#\n", buffer );

        if ( SUCCESS == ( status = http_send_request( http, GET, host, get_resource_path( RES_RANGE ), "start=0&count=10000", buffer, buffer_size, http_write_callback ) ) )
        {
            uint8_t attr_mask = MEM_ATTR_CE_MASK | MEM_ATTR_RW_MASK;
            size_t section = 0;
            uint8_t last = 0xff;
            uint16_t start;
            uint16_t end;
            bool enabled;
            bool readonly;

            for ( size_t i = 1; i < MEMORY_SIZE * 2; i += 2 )
            {
                if ( ( buffer[i] & attr_mask ) != last )
                {
                    if ( section )
                    {
                        config_print_section( file, start, end, enabled, readonly );
                    }

                    // New section
                    //
                    ++section;
                    last = buffer[i] & attr_mask;
                    start = i / 2;
                    end = start;
                    enabled = ! ( buffer[i] & MEM_ATTR_CE_MASK );
                    readonly = ! ( buffer[i] & MEM_ATTR_RW_MASK );
                }
                else {
                    ++end;
                }
            }
            config_print_section( file, start, end, enabled, readonly );
        }
    }

    if ( file != stdout && fclose( file ) )
    {
        perror( "Error while closing the output file" );
        status = FAILURE;
    }

    return status;
}

static status_t config_from_file( char *filename, http_t *http, char *hostname, uint8_t *buffer )
{
    const char query[] = "start=%4.4X&count=%4.4X";
    char query_buf[ sizeof( query ) ];

    status_t status = SUCCESS;
    memmap_doc_t *memmap;

    if ( SUCCESS == ( status = parse_memmap( filename, &memmap ) ) )
    {
        memmap_doc_t *m = memmap;

        while ( m )
        {
            const char *resource;

            sprintf( query_buf, query, m->start, m->count );

            if ( !m->flags.fill && !m->data.length && !m->file )
            {
                // There is no data specified in the section, either fill or file or data
                // So, we use the enable/disable/setram/setrom endpoints, which are
                // more efficient

                resource = get_resource_path( (m->flags.enabled && m->enabled) ? RES_RANGE_ENABLE : RES_RANGE_DISABLE );

                if ( SUCCESS != ( status = http_send_request( http, PATCH, hostname, resource, query_buf, NULL, 0, NULL ) ) )
                {
                    break;
                }

                resource = get_resource_path( (m->flags.ro && !m->ro) ? RES_RANGE_SETRAM : RES_RANGE_SETROM ); 

                if ( SUCCESS != ( status = http_send_request( http, PATCH, hostname, resource, query_buf, NULL, 0, NULL ) ) )
                {
                    break;
                }
            }
            else
            {
                // There is data specified in the section, either fill or file or data
                // So, we have to create a memory map file
                //
                // By default, disabled and read only

                uint8_t attributes = MEM_ATTR_CE_MASK;

                if ( m->flags.enabled && m->enabled )
                {
                    attributes &= ~MEM_ATTR_CE_MASK;                    
                }
                if ( m->flags.ro && ! m->ro )
                {
                    attributes = MEM_ATTR_RW_MASK;
                }

                for ( size_t i=0; i < m->count * 2; i += 2 )
                {
                    if ( m->flags.fill )
                    {
                        buffer[i] = m->fill;
                    }
                    buffer[i+1] = attributes;
                }

                if ( m->file )
                {
                    FILE *file = fopen( m->file, "rb" );
                    size_t bytes_read = 0;

                    if ( NULL == file )
                    {
                        fprintf( stderr, "Can't open data file %s: %s\n", m->file, strerror( errno ) );
                        status = FAILURE;
                        break;
                    }

                    while ( fread( &buffer[bytes_read * 2], 1, 1, file ) )
                    {
                        ++bytes_read;
                    }

                    if ( ferror( file ) )
                    {
                        fprintf( stderr, "Error reading data file %s: %s\n", m->file, strerror( errno ) );
                        status = FAILURE;
                        fclose( file );
                        break;                            
                    }
                    fclose( file );

                    if ( (uint32_t) bytes_read > m->count )
                    {
                        // Should not occur!!
                        fprintf( stderr, "FATAL: number of bytes read from %s (%ld) is bigger than section size (%d)\n",
                                        m->file, bytes_read, m->count );
                        status = FAILURE;
                        break;
                    }

                }
                if ( m->data.length )
                {
                    for ( int i = 0; i < m->data.length; ++i )
                    {
                        buffer[i*2] = m->data.value[i];
                    }
                }

                http->data_size = (size_t) m->count * 2;

                if ( SUCCESS != ( status = http_send_request( http, PATCH, hostname, get_resource_path( RES_RANGE ), query_buf, buffer, BUFFER_SIZE, http_read_callback ) ) )
                {
                    break;
                }
            }

            m = m->next;
        }

        free_memmap ( memmap );
    }

    return status;
}

static status_t config_set_range( range_array_t *range, http_t *http, const char *host, const char *resource )
{
    const char query[] = "start=%4.4X&count=%4.4X";
    char query_buf[ sizeof( query ) ];

    status_t status;
    const char *r = resource;

    for ( size_t n = 0; n < range->n_ranges; ++n )
    {
        uint32_t count = range->ranges[n].end - range->ranges[n].start + 1;
            
        sprintf( query_buf, query, range->ranges[n].start, count );

        if ( SUCCESS == ( status = http_send_request( http, PATCH, host, r, query_buf, NULL, 0, NULL ) ) )
        {
            r = NULL;    // Nullify so http_construct_request() does not reset it again as it is unchanged
        }
        else
        {
            break;
        }
    }

    return status;
}

static status_t config_set_video( uint16_t address, http_t *http, const char *host )
{
    const char query[] = "address=%4.4X";
    char query_buf[ sizeof( query ) ];
 
    sprintf( query_buf, query, address );

    return http_send_request( http, PUT, host, get_resource_path( RES_VIDEO ), query_buf, NULL, 0, NULL );
}

status_t config_command( int argc, char **argv )
{
    config_opt_t options;
    status_t status = SUCCESS;
    http_t *http;
    uint8_t *buffer;

    if ( FAILURE == config_options( &options, argc, argv ) )
    {
        return FAILURE;
    }

    if ( NULL == ( http = http_init( options.hostname ) ) )
    {
        return FAILURE;
    }

    buffer = malloc( MEMORY_SIZE * 2 );

    if ( NULL == buffer )
    {
        perror( "Can't allocate memory for config buffer" );
        return FAILURE;
    }

    if ( !options.disable.n_ranges && !options.enable.n_ranges && !options.ro.n_ranges &&
         !options.rw.n_ranges && !options.flags.video_address && !options.input_filename )
    {
        status = config_print_config( options.output_filename, http, options.hostname, buffer, BUFFER_SIZE );
    }
    else
    {
        if ( options.output_filename )
        {
            fprintf( stderr, "Warning: '-o %s' ignored\n", options.output_filename );
        }
    
        if ( options.input_filename )
        {
            status = config_from_file( options.input_filename, http, options.hostname, buffer );
        }

        if ( SUCCESS == status && options.disable.n_ranges )
        {     
            status = config_set_range( &options.disable, http, options.hostname, get_resource_path( RES_RANGE_DISABLE ) );
        }

        if ( SUCCESS == status && options.enable.n_ranges )
        {        
            status = config_set_range( &options.enable, http, options.hostname, get_resource_path( RES_RANGE_ENABLE ) );
        }

        if ( SUCCESS == status && options.ro.n_ranges )
        {        
            status = config_set_range( &options.ro, http, options.hostname, get_resource_path( RES_RANGE_SETROM ) );
        }

        if ( SUCCESS == status && options.rw.n_ranges )
        {        
            status = config_set_range( &options.rw, http, options.hostname, get_resource_path( RES_RANGE_SETRAM ) );
        }

        if ( SUCCESS == status && options.flags.video_address )
        {
            status = config_set_video( options.flags.video_address, http, options.hostname );
        }

    }

    http_cleanup( http );
    free( buffer );

    return status;
}