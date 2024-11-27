/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * "restore" command
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
#include <libgen.h>

#include "globals.h"
#include "http.h"
#include "resources.h"

static status_t restore_usage( char *myname, char *command, status_t status )
{
    fprintf( stderr, "\nUsage: %s %s [-h] \n", myname , command );
    fprintf( stderr, "       %s %s IPADDR\n\n", myname , command );

    fputs( "Arguments:\n", stderr );
    fputs( "    IPADDR                   Board IP address\n\n", stderr );

    fputs( "Options:\n", stderr );
    fputs( "    -h | --help              Show this help message and exit\n", stderr );

    exit( status );
}

static status_t restore_get_hostname( char **hostname, int argc, char **argv )
{
    int opt, opt_index = 0;
    char *myname = basename( argv[0] );

    if ( argc != 3 )
    {
        fputs( "Invalid number of arguments\n", stderr );
        return restore_usage( myname, argv[1], FAILURE );
    }

    if  ( !strcmp( argv[2], "-h" ) || !strcmp( argv[2], "-help" ) )
    {
        return restore_usage( myname, argv[1], SUCCESS );
    }

    if ( *argv[2] == '-' )
    {
        fprintf( stderr, "Invalid argument: %s\n", argv[2] );
        return restore_usage( myname, argv[1], FAILURE );
    }
    else
    {
        *hostname = argv[2];
    }

    return SUCCESS;
}

status_t restore_command( int argc, char **argv )
{
    char *host;
 
    status_t status = SUCCESS;
    http_t *http;

    if ( FAILURE == restore_get_hostname( &host, argc, argv ) )
    {
        return FAILURE;
    }

    if ( NULL == ( http = http_init( host ) ) )
    {
        return FAILURE;
    }

    status = http_send_request( http, PUT, host, get_resource_path( RES_RESTORE ), NULL, NULL, 0, NULL );

    http_cleanup( http );

    return status;
}