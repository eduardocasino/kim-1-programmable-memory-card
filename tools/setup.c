/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "setup" command
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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>
#include <yaml.h>
#include <errno.h>

#include "globals.h"
#include "yaml.h"
#include "uf2.h"

#define MEMMAP_START_ADDR   0x101C0000
#define CONFIG_START_ADDR   0x101E0000

typedef struct {
    char *config_filename;
    char *memmap_filename;
    char *output_filename;
} setup_opt_t;

static status_t setup_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s [-h]\n", myname, command );
    fprintf( stderr, "       %s %s [-s FILE] [-m FILE] -o FILE\n\n", myname, command );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help             Show this help message and exit\n", stderr );
    fputs( "    -s | --setup  FILE      Setup configuration file\n", stderr );
    fputs( "    -m | --memmap FILE      Default memory map file\n", stderr );
    fputs( "    -o | --output FILE      Generated UF2 file\n\n", stderr );

    fputs( "Note: At least one of '-m' or '-s' files must be specified.\n", stderr );

    exit( status );
}

static status_t setup_duplicate( char *myname, char *command, char opt )
{
    fprintf( stderr, "Duplicate option: -%c\n", opt );
    return setup_usage( myname, command, FAILURE );
}

static status_t setup_options( setup_opt_t *options, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    static const struct option long_opts[] = {
        {"help",   no_argument,       0, 'h' },
        {"setup",  required_argument, 0, 's' },
        {"memmap", required_argument, 0, 'm' },
        {"output", required_argument, 0, 'o' },
        {0,        0,                 0,  0  }
    };

    memset( options, 0, sizeof( setup_opt_t ) );

    while (( opt = getopt_long( argc-1, &argv[1], "s:m:o:h", long_opts, &opt_index)) != -1 )
    {
        switch( opt )
        {
            case 's':
                if ( options->config_filename )
                {
                    return setup_duplicate( myname, argv[1], opt );
                }
                options->config_filename = optarg;
                break;

            case 'o':
                if ( options->output_filename )
                {
                    return setup_duplicate( myname, argv[1], opt );
                }
                options->output_filename = optarg;
                break;

            case 'm':
                if ( options->memmap_filename )
                {
                    return setup_duplicate( myname, argv[1], opt );
                }
                options->memmap_filename = optarg;
                break;
            
            case 'h':
            default:
                return setup_usage( myname, argv[1], opt == 'h' ? SUCCESS : FAILURE);
        }
    }

    if ( NULL == options->output_filename )
    {
        fputs( "Error: Missing output file name.\n\n", stderr );
        return setup_usage( myname, argv[1], FAILURE );
    }

    if ( NULL == options->config_filename && NULL == options->memmap_filename )
    {
        fputs( "Error: At least one of memory map or setup file names must me specified.\n\n", stderr );
        return setup_usage( myname, argv[1], FAILURE );
    }

    return SUCCESS;
}

static status_t setup_generate_memmap( char *file_name, uint8_t *data )
{
    memmap_doc_t *memmap;
    status_t status = SUCCESS;

    if ( SUCCESS == parse_memmap( file_name, &memmap ) )
    {
        memmap_doc_t *m = memmap;

        // By default, memory is rom and disabled, fill char is 0x00
        //
        m->enabled = m->flags.enabled ? m->enabled : false;
        m->ro      = m->flags.ro ? m->ro : true;
        m->fill    = m->flags.fill ? m->fill : 0x00;

        for ( uint32_t i = 0; i < MEMORY_SIZE * 2; i += 2 )
        {
            data[i] = m->fill;
            data[i+1] = MEM_ATTR_RO | MEM_ATTR_DISABLED;
        }

        while ( m )
        {
            // First, process 'fill', if present
            //
            if ( m->flags.fill )
            {
                for ( uint32_t i = m->start * 2; i <= m->end * 2; i += 2 )
                {
                    data[i] = m->fill;
                }
            }

            // Process 'data', if present
            //
            for ( uint32_t i = 0; i < m->data.length; ++i )
            {
                data[(m->start+i) * 2] = m->data.value[i];
            }

            // Process 'file', if present
            //
            if ( m->file )
            {
                size_t findex = 0;
                FILE *file;

                if ( NULL == ( file = fopen( m->file, "rb" ) ) )
                {
                    fprintf( stderr, "Can't open file '%s': %s\n", m->file, strerror( errno ) );
                    status = FAILURE;
                    break;
                }

                while ( fread( &data[(m->start+findex) * 2], 1, 1, file ) )
                {
                    ++findex;
                }

                if ( ferror( file ) )
                {
                    fprintf( stderr, "Error reading from file '%s'\n", m->file );
                    status = FAILURE;
                    break;
                }
            }

            // Finally, set memory attributes
            //
            for ( uint32_t i = m->start * 2; i < ( m->start + m->count ) * 2; i += 2 )
            {
                data[i+1]  = ( m->enabled ) ? MEM_ATTR_ENABLED  : MEM_ATTR_DISABLED;
                data[i+1] |= ( m->ro )      ? MEM_ATTR_RO       : MEM_ATTR_RW;
            }            

            m = m->next;
        }        

        free_memmap( memmap );

    }

    return status;
}

status_t setup_command( int argc, char **argv )
{
    FILE *output_file;

    setup_opt_t options;
    config_doc_t *config;
    uint8_t *data = NULL;
    size_t data_size = 0;
    uint32_t start_addr = CONFIG_START_ADDR;
    status_t status = SUCCESS;

    if ( FAILURE == setup_options( &options, argc, argv ) )
    {
        return FAILURE;
    }

    if ( NULL == ( output_file = fopen( options.output_filename, "w" ) ) )
    {
        perror( "Can't open output file" );
        return FAILURE;
    }

    if ( NULL != options.memmap_filename )
    {
        start_addr = MEMMAP_START_ADDR;
        data_size = MEMORY_SIZE * 2;

        if ( NULL ==  ( data = malloc( data_size ) ) )
        {
            perror( "Can't allocate memory for memory map" );
            return FAILURE;
        }

        if ( FAILURE == setup_generate_memmap( options.memmap_filename, data ) )
        {
            free( data );
            return FAILURE;
        }
    }

    if ( NULL != options.config_filename )
    {
        config_doc_t *config;

        if ( NULL == ( data = realloc( data, data_size + sizeof( config_doc_t ) ) ) )
        {
            perror( "Can't allocate memory for config document" );
            return FAILURE;
        }

        config = (config_doc_t *)&data[data_size];
        data_size += sizeof( config_doc_t );

        if ( FAILURE == parse_config( options.config_filename, config ) )
        {
            free( data );
            return FAILURE;
        }
    }

    status = uf2_write( output_file, data, data_size, start_addr );

    if ( fclose( output_file ) )
    {
        perror( "Error while closing the output file" );
        status = FAILURE;
    }

    free( data );

    return status;
}

