/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "file" command
 * 
 *  Copyright (C) 2025 Eduardo Casino
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
#include <libgen.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "globals.h"
#include "http.h"
#include "resources.h"

typedef struct {
    struct {
        bool send;
        bool receive;
        bool copy_or_move;
        bool ismove;
        bool delete;
        bool force;
    } flags;
    char *hostname;
    char *remote_filename;
    char *local_filename;
    char *new_filename;
} file_opt_t;

static status_t file_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s [-h] \n", myname , command );
    fprintf( stderr, "       %s %s IPADDR\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR [-r|-s] FILE [-l LFILE] [-f]\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR -d FILE\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR [-c|-m] FILE NFILE [-f]\n\n", myname , command );

    fputs( "Arguments:\n", stderr );
    fputs( "    IPADDR                   Board IP address\n\n", stderr );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help                Show this help message and exit\n", stderr );
    fputs( "    -r | --receive FILE        Get FILE from SD card\n", stderr );
    fputs( "    -s | --send    FILE        Send FILE to SD card\n", stderr );
    fputs( "    -l | --local   LFILE       Local filename (defaults to FILE)\n", stderr );
    fputs( "    -f | --force               Force file overwrite (use with care)\n", stderr );
    fputs( "    -d | --delete  FILE        Remove FILE from SD card\n", stderr );
    fputs( "    -c | --copy    FILE NFILE  Copy FILE to NFILE on the SD card\n", stderr );
    fputs( "    -m | --move    FILE NFILE  Rename FILE to NFILE on the SD card\n\n", stderr );


    fputs( "Without options, lists files on the SD card.\n", stderr );

    exit( status );
}

static status_t file_duplicate( char *myname, char *command, char opt )
{
    fprintf( stderr, "Duplicate option: -%c\n", opt );
    return file_usage( myname, command, FAILURE );
}

static status_t missing_nfile( char *myname, char *command, char opt )
{
    fprintf( stderr, "Missing NFILE argument for '-%c'\n", opt );
    return file_usage( myname, command, FAILURE );
}

static status_t file_options( file_opt_t *options, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    static const struct option long_opts[] = {
        {"receive", required_argument, 0, 'r' },
        {"send",    required_argument, 0, 's' },
        {"delete",  required_argument, 0, 'd' },
        {"copy",    required_argument, 0, 'c' },
        {"move",    required_argument, 0, 'm' },
        {"local",   required_argument, 0, 'l' },
        {"force",   no_argument,       0, 'f' },
        {0,         0,                 0,  0  }
    };

    memset( options, 0, sizeof( file_opt_t ) );

    if ( argc < 3 )
    {
        fputs( "Invalid number of arguments\n", stderr );
        return file_usage( myname, argv[1], FAILURE );
    }

    if ( !strcmp( argv[2], "-h" ) || !strcmp( argv[2], "--help" ) )
    {
        return file_usage( myname, argv[1], SUCCESS );
    }

    if ( *argv[2] == '-' )
    {
        fputs( "Host name is mandatory\n", stderr );
        return file_usage( myname, argv[1], FAILURE );
    }
    else
    {
        options->hostname = argv[2];
    }

    while (( opt = getopt_long( argc-2, &argv[2], "r:s:d:c:m:l:f", long_opts, &opt_index)) != -1 )
    {

        switch( opt )
        {
            case 'f':
                if ( options->flags.force++ )
                {
                    return file_duplicate( myname, argv[1], opt );
                }

                break;

            case 'r':
                if ( options->flags.receive++ )
                {
                    return file_duplicate( myname, argv[1], opt );
                }
                options->remote_filename = optarg;

                break;

            case 's':
                if ( options->flags.send++ )
                {
                    return file_duplicate( myname, argv[1], opt );
                }
                options->remote_filename = optarg;

                break;

            case 'd':
                if ( options->flags.delete++ )
                {
                    return file_duplicate( myname, argv[1], opt );
                }
                options->remote_filename = optarg;

                break;

            case 'c':
            case 'm':
                if ( options->flags.copy_or_move++ )
                {
                    return file_duplicate( myname, argv[1], opt );
                }
                options->remote_filename = optarg;

                if ( optind < argc - 2 && *argv[optind+2] != '-' )
                {                    
                    options->new_filename = argv[optind+2];
                    ++optind;
                }
                else
                {
                    return missing_nfile( myname, argv[1], opt );
                }

                if ( opt == 'm' )
                {
                    ++options->flags.ismove;
                }

                break;

            case 'l':
                if ( options->local_filename )
                {
                    return file_duplicate( myname, argv[1], opt );
                }
                options->local_filename = optarg;
                break;

            default:
                return file_usage( myname, argv[1], FAILURE );
        }
    }

    if ( options->local_filename && !(options->flags.receive || options->flags.send ) )
    {
        fputs( "Missing '--receive' or '--send' options\n", stderr );
        return file_usage( myname, argv[1], FAILURE );
    }

    if ( options->flags.force &&
         (options->flags.delete || options->flags.ismove ) )
    {
        fputs( "Error: '--force' not valid with '--delete' or '--move'\n", stderr );
        return file_usage( myname, argv[1], FAILURE );
    }

    if ( options->flags.send && options->flags.receive && options->flags.delete )
    {
        fputs( "'Error: incompatible options\n", stderr );
        return file_usage( myname, argv[1], FAILURE );
    }

    return SUCCESS;
}

static status_t file_print_files( http_t *http, const char *host, char *output )
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

    status = http_send_request( http, GET, host, get_resource_path( RES_SD_DIR ), NULL, file, NULL, 0, http_write_callback );

    if ( file != stdout && fclose( file ) )
    {
        perror( "Error while closing the output file" );
        status = FAILURE;
    }

    return status;
}

static status_t file_receive( http_t *http, const char *host, char *filename, char *output, bool force )
{
    const char query[] = "fname=%s";
    char query_buf[ sizeof( query ) + 256 ];

    FILE *file;
    status_t status = FAILURE;

    if ( !output )
    {
        output = filename;
    }

    if ( access( output, F_OK ) == 0 && ! force )
    {
        fprintf( stderr, "Error: File '%s' exists. Use '--force' to overwrite\n", output );
        return FAILURE;
    }

    if ( NULL == ( file = fopen( output, "w" ) ) )
    {
        perror( "Can't open output file" );
        return FAILURE;
    }
    
    sprintf( query_buf, query, filename );

    status = http_send_request( http, GET, host, get_resource_path( RES_SD_FILE ), query_buf, file, NULL, 0, http_write_callback );

    if ( fclose( file ) )
    {
        perror( "Error while closing the output file" );
        status = FAILURE;
    }

    return status;
}

static status_t file_send( http_t *http, const char *host, char *filename, char *input, bool force )
{
    const char query[] = "fname=%s&owrite=%d";
    char query_buf[ sizeof( query ) + 256 ];

    struct stat st;
    FILE *file;
    status_t status = FAILURE;

    if ( !input )
    {
        input = filename;
    }

    if ( stat( input, &st ) != 0)
    {
        perror( "Can't get file size" );
        return FAILURE;
    }

    if ( NULL == ( file = fopen( input, "r" ) ) )
    {
        perror( "Can't open input file" );
        return FAILURE;
    }
    
    sprintf( query_buf, query, filename, (int)force );

    http->data_size = st.st_size;

    status = http_send_request( http, POST, host, get_resource_path( RES_SD_FILE ), query_buf, file, NULL, 0, http_read_callback );

    if ( fclose( file ) )
    {
        perror( "Error while closing the output file" );
        status = FAILURE;
    }

    return status;
}

static status_t file_delete( http_t *http, const char *host, char *filename )
{
    const char query[] = "fname=%s";
    char query_buf[ sizeof( query ) + 256 ];

    status_t status = FAILURE;

    sprintf( query_buf, query, filename );

    status = http_send_request( http, DELETE, host, get_resource_path( RES_SD_FILE ), query_buf, NULL, NULL, 0, NULL );

    return status;
}

static status_t file_copy( http_t *http, const char *host, char *filename, char *dest, bool force )
{
    const char query[] = "fname=%s&nfname=%s&owrite=%d";
    char query_buf[ sizeof( query ) + 512 ];

    status_t status = FAILURE;

    sprintf( query_buf, query, filename, dest, (int)force );

    status = http_send_request( http, POST, host, get_resource_path( RES_SD_FILE ), query_buf, NULL, NULL, 0, NULL );

    return status;
}

static status_t file_rename( http_t *http, const char *host, char *filename, char *dest )
{
    const char query[] = "fname=%s&nfname=%s";
    char query_buf[ sizeof( query ) + 512 ];

    status_t status = FAILURE;

    sprintf( query_buf, query, filename, dest );

    status = http_send_request( http, PATCH, host, get_resource_path( RES_SD_FILE ), query_buf, NULL, NULL, 0, NULL );

    return status;
}

status_t file_command( int argc, char **argv )
{
    file_opt_t options;
    status_t status = SUCCESS;
    http_t *http;

    if ( FAILURE == file_options( &options, argc, argv ) )
    {
        return FAILURE;
    }

    if ( NULL == ( http = http_init( options.hostname ) ) )
    {
        return FAILURE;
    }

    http->silent = true;

    if ( !options.flags.send && !options.flags.receive && !options.flags.copy_or_move &&
         !options.flags.delete )
    {
        status = file_print_files( http, options.hostname, NULL );
    }
    else if ( options.flags.receive )
    {
        status = file_receive( http, options.hostname, options.remote_filename, options.local_filename, options.flags.force );
    }
    else if ( options.flags.send )
    {
        status = file_send( http, options.hostname, options.remote_filename, options.local_filename, options.flags.force );
    }
    else if ( options.flags.delete )
    {
        status = file_delete( http, options.hostname, options.remote_filename );
    }
    else if ( options.flags.copy_or_move )
    {
        if ( options.flags.ismove )
        {
            status = file_rename( http, options.hostname, options.remote_filename, options.new_filename );
        }
        else
        {
            status = file_copy( http, options.hostname, options.remote_filename, options.new_filename, options.flags.force );
        }
    }

    if ( http->http_code != 200 )
    {
        http_error_msg( http );
    }

    http_cleanup( http );

    return status;
}
