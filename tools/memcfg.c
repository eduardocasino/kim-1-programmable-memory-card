/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
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
#include <stdbool.h>
#include <libgen.h>
#include <string.h>

#include <assert.h>

#include "globals.h"
#include "config.h"
#include "restore.h"
#include "read.h"
#include "write.h"
#include "setup.h"

typedef status_t (*command_fn_t)( int argc, char **argv );

typedef struct {
    const char *command_string;
    command_fn_t command_fn;
} command_t;

static const command_t commands[] = {
    { "read",    read_command } ,
    { "write",   write_command },
    { "config",  config_command },
    { "restore", restore_command },
    { "setup",   setup_command },
    { NULL }
};

void main_usage( char *myname, const command_t *cmds )
{
    fprintf( stderr, "\nUsage: %s -h\n", myname );
    fprintf( stderr, "       %s {", myname );

    for ( int c = 0; cmds[c].command_string != NULL; ++c )
    {
        fprintf( stderr, "%s%c", cmds[c].command_string, cmds[c+1].command_string ? ',' : '}' );
    }

    fputs( " ...\n", stderr );
}

command_fn_t parse_command( int argc, char **argv )
{
    char *myname = basename( argv[0] );
    if ( argc > 1 )
    {
        if ( !strcmp( argv[1], "-h" ) || !strcmp( argv[1], "--help") )
        {
            main_usage( myname, commands );
            return NULL;
        }

        for ( int c = 0; commands[c].command_string != NULL; ++c )
        {
            if ( !strcmp( argv[1], commands[c].command_string ) )
            {
                assert( commands[c].command_fn );
                
                return commands[c].command_fn;

            }
        }
    }

    // If it reaches here, no command match
    //
    fputs( "Error: Missing or unknown command.\n\n", stderr );

    main_usage( myname, commands );
    
    return NULL;
}

void main( int argc, char **argv )
{
    command_fn_t command_fn;

    if ( NULL == ( command_fn = parse_command( argc, argv ) ) )
    {
        exit( EXIT_FAILURE );
    }

    if ( FAILURE == command_fn( argc, argv ) )
    {
        exit( EXIT_FAILURE );
    }

    exit( EXIT_SUCCESS );
}