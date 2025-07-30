/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "mount" command
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
        bool drive;
        bool umount;
        bool ro;
        bool save;
    } flags;
    char *hostname;
    char *filename;
    int drive;
} mount_opt_t;

static status_t mount_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s [-h] \n", myname , command );
    fprintf( stderr, "       %s %s IPADDR\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR -s\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR -d DRIVE -f FILE [-r]\n", myname , command );
    fprintf( stderr, "       %s %s IPADDR -d DRIVE -u\n\n", myname , command );

    fputs( "Arguments:\n", stderr );
    fputs( "    IPADDR                   Board IP address\n\n", stderr );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help              Show this help message and exit\n", stderr );
    fputs( "    -s | --save              Save current mounts as default\n", stderr );
    fputs( "    -d | --drive   DRIVE     Drive to mount/umount (0 to 3)\n", stderr );
    fputs( "    -f | --file    FILE      Image file to mount\n", stderr );
    fputs( "    -u | --umount            Unmount drive\n", stderr );
    fputs( "    -r | --ro                Mount read-only\n\n", stderr );

    fputs( "Without options, lists current mounts.\n", stderr );

    exit( status );
}

static status_t mount_duplicate( char *myname, char *command, char opt )
{
    fprintf( stderr, "Duplicate option: -%c\n", opt );
    return mount_usage( myname, command, FAILURE );
}

static status_t mount_options( mount_opt_t *options, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    static const struct option long_opts[] = {
        {"drive",  required_argument, 0, 'd' },
        {"file",   required_argument, 0, 'f' },
        {"ro",     no_argument,       0, 'r' },
        {"umount", no_argument,       0, 'u' },
        {"save",   no_argument,       0, 's' },
        {0,        0,                 0,  0  }
    };

    memset( options, 0, sizeof( mount_opt_t ) );

    if ( argc < 3 )
    {
        fputs( "Invalid number of arguments\n", stderr );
        return mount_usage( myname, argv[1], FAILURE );
    }

    if ( !strcmp( argv[2], "-h" ) || !strcmp( argv[2], "--help" ) )
    {
        return mount_usage( myname, argv[1], SUCCESS );
    }

    if ( *argv[2] == '-' )
    {
        fputs( "Host name is mandatory\n", stderr );
        return mount_usage( myname, argv[1], FAILURE );
    }
    else
    {
        options->hostname = argv[2];
    }

    while (( opt = getopt_long( argc-2, &argv[2], "d:f:rus", long_opts, &opt_index)) != -1 )
    {

        switch( opt )
        {
            case 'd':
                if ( options->flags.drive++ )
                {
                    return mount_duplicate( myname, argv[1], opt );
                }
                options->drive = atoi( optarg );

                if ( options->drive < 0 || options->drive > 3 )
                {
                    fprintf( stderr, "Invalid drive number: %d\n", options->drive );
                    return mount_usage( myname, argv[1], FAILURE );
                }

                break;

            case 'f':
                if ( options->filename )
                {
                    return mount_duplicate( myname, argv[1], opt );
                }
                options->filename = optarg;
                break;

            case 'r':
                if ( options->flags.ro++ )
                {
                    return mount_duplicate( myname, argv[1], opt );
                }

                break;

            case 'u':
                if ( options->flags.umount++ )
                {
                    return mount_duplicate( myname, argv[1], opt );
                }

                break;

            case 's':
                if ( options->flags.save++ )
                {
                    return mount_duplicate( myname, argv[1], opt );
                }
                break;

            default:
                return mount_usage( myname, argv[1], FAILURE );
        }
    }

    if ( options->flags.save &&
        (options->flags.drive || options->flags.umount || options->filename || options->flags.ro) )
    {
        fputs( "Option '-s' is incompatible with any other\n", stderr );
        return mount_usage( myname, argv[1], FAILURE );
    }
 
    if ( !options->flags.drive && (options->filename || options->flags.umount) )
    {
        fputs( "Drive is mandatory\n", stderr );
        return mount_usage( myname, argv[1], FAILURE );
    }

    if ( options->flags.drive && !options->filename && !options->flags.umount )
    {
        fputs( "Missing mandatory option: '-f' or '-u'\n", stderr );
        return mount_usage( myname, argv[1], FAILURE );
    }

    if ( options->flags.umount && options->flags.ro )
    {
        fputs( "Incompatible options: '-r' and '-u'\n", stderr );
        return mount_usage( myname, argv[1], FAILURE );
    }

    return SUCCESS;
}

static status_t mount_save( http_t *http, const char *host )
{ 
    return http_send_request( http, POST, host, get_resource_path( RES_MNT_SAVE ), NULL, NULL, NULL, 0, NULL );
}

static status_t mount_print_mounts( http_t *http, const char *host, char *output )
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

    status = http_send_request( http, GET, host, get_resource_path( RES_MNT ), NULL, file, NULL, 0, http_write_callback );

    if ( file != stdout && fclose( file ) )
    {
        perror( "Error while closing the output file" );
        status = FAILURE;
    }

    return status;
}

static status_t mount_image( http_t *http, const char *host, int drive, char *filename, bool ro )
{
    const char query[] = "drive=%d&img=%s&ro=%d";
    char query_buf[ sizeof( query ) + 256 ];

    status_t status = FAILURE;

    sprintf( query_buf, query, drive, filename, (int)ro );

    status = http_send_request( http, POST, host, get_resource_path( RES_MNT ), query_buf, NULL, NULL, 0, NULL );

    return status;
}

static status_t mount_umount( http_t *http, const char *host, int drive )
{
    const char query[] = "drive=%d";
    char query_buf[ sizeof( query ) ];

    status_t status = FAILURE;

    sprintf( query_buf, query, drive );

    status = http_send_request( http, DELETE, host, get_resource_path( RES_MNT ), query_buf, NULL, NULL, 0, NULL );

    return status;
}

status_t mount_command( int argc, char **argv )
{
    mount_opt_t options;
    status_t status = SUCCESS;
    http_t *http;

    if ( FAILURE == mount_options( &options, argc, argv ) )
    {
        return FAILURE;
    }

    if ( NULL == ( http = http_init( options.hostname ) ) )
    {
        return FAILURE;
    }

    http->silent = true;

    if ( !options.flags.drive && !options.flags.save )
    {
        status = mount_print_mounts( http, options.hostname, NULL );
    }
    else if ( options.flags.umount )
    {
        status = mount_umount( http, options.hostname, options.drive );
    }
    else if ( options.filename )
    {
        status = mount_image( http, options.hostname, options.drive, options.filename, options.flags.ro );
    }
    else if ( options.flags.save )
    {
        status = mount_save( http, options.hostname );
    }

    if ( http->http_code != 200 )
    {
        http_error_msg( http );
    }

    http_cleanup( http );

    return status;
}
