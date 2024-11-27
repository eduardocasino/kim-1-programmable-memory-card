/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Parsing support functions
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "globals.h"
#include "scan.h"

enum uint_len_t { UINT8 = 2, UINT16 = 4, UINT32 = 8 };

static uint8_t hexvalue( char digit )
{
    uint8_t value;

    char c = toupper( digit );

    if ( isdigit( c ))
    {
        value = digit - '0';
    }
    else
    {
        value = digit - 'A' + 10;
    }

    return value;
}

static int get_hexnum( int len, const char *s )
{
    static const int base16[] = { 12, 8, 4, 0 };

    int result = 0;

    if ( strlen( s ) < len )
    {   
        return -1;
    }

    for ( int pos = 0; pos < len; ++pos )
    {
        int val;
        char c = s[pos];

        if ( !isxdigit( c ) )
        {
            return -1;
        }
        result |= (hexvalue( c ) << base16[4 - len + pos] ) & ( 0xF << base16[4 - len + pos] );
    }

    return result;
}

static inline int get_uint( const char *string, uint64_t *value, int len )
{
    *value = strtoul( string, NULL, 0 );

    if ( ( len == UINT16 && *value >  UINT16_MAX ) || *value > UINT32_MAX ) 
    {
        return EINVAL;
    }

    return 0;
    
}

int get_uint16( const char *string, uint16_t *value )
{
    int retcode;
    uint64_t retval;

    if ( 0 == ( retcode = get_uint( string, &retval, UINT16 ) ) )
    {
        *value = (uint16_t) retval;
    }

    return retcode;
}

int get_uint32( const char *string, uint32_t *value )
{
    int retcode;
    uint64_t retval;

    if ( 0 == ( retcode = get_uint( string, &retval, UINT32 ) ) )
    {
        *value = (uint32_t) retval;
    }

    return retcode;
}

int get_hexbyte( const char *s, uint8_t *byte )
{
    int result = get_hexnum( UINT8, s );

    if ( result < 0 )
    {
        return EINVAL;
    }

    *byte = (uint8_t) result;

    return 0;
}

int get_hexword( const char *s, uint16_t *word )
{
    int result = get_hexnum( UINT16, s );

    if ( result < 0 )
    {
        return EINVAL;
    }

    *word = (uint16_t) result;

    return 0;
}

int get_octbyte( const char *s, uint8_t *byte )
{
    static const int base8[] = { 6, 3, 0 };

    if ( strlen( s ) < 3 )
    {
        return EINVAL;
    }

    *byte = 0;

    for ( int pos = 0; pos < 3; ++pos )
    {
        int val = s[pos] - '0';

        if ( !isdigit( s[pos] ) || val > 7 )
        {
            return EINVAL;
        }
        *byte += val << base8[pos];
    }

    return 0;
}

int get_boolean( const char *string, bool *value )
{
    if ( !strcmp( string, "false" ) )
    {
        *value = 0;

        return 0;
    }

    if ( !strcmp( string, "true" ) )
    {
        *value = 1;

        return 0;
    }

    return EINVAL;
}

int get_country_code( const char *string, char *value )
{
    static const char *country_codes[] = {
        "US", "CA", "DZ", "DE", "NL", "IT", "PT", "LU", "NO", "FI", "DK", "CH", "CZ", "ES", "GB", "KR", 
        "CN", "FR", "HK", "SG", "TW", "BR", "IL", "SA", "LB", "AE", "ZA", "AR", "AU", "AT", "BO", "CL", 
        "GR", "IS", "IN", "IE", "KW", "LI", "LT", "MX", "MA", "NZ", "PL", "PR", "SK", "SI", "TH", "UY", 
        "PA", "RU", "KW", "LI", "LT", "MX", "MA", "NZ", "PL", "PR", "SK", "SI", "TH", "UY", "PA", "RU", 
        "EG", "TT", "TR", "CR", "EC", "HN", "KE", "UA", "VN", "BG", "CY", "EE", "MU", "RO", "CS", "ID", 
        "PE", "VE", "JM", "BH", "OM", "JO", "BM", "CO", "DO", "GT", "PH", "LK", "SV", "TN", "PK", "QA", 
        "JP3", NULL
    };

    for ( const char **c = country_codes; *c; c++ )
    {
        if ( !strcmp( string, *c ) )
        {
            strcpy( value, *c );
            return 0;
        }
    }

    return EINVAL;
}

int get_video_system( const char *string, uint16_t *value )
{
    if ( !strcmp( string, "ntsc" ) )
    {
        *value = NTSC;

        return 0;
    }

    if ( !strcmp( string, "pal" ) )
    {
        *value = PAL;

        return 0;
    }

    return EINVAL;
}

int get_video_output( const char *string, uint16_t *value )
{
    if ( !strcmp( string, "composite" ) )
    {
        *value = COMPOSITE;

        return 0;
    }

    if ( !strcmp( string, "vga" ) )
    {
        *value = VGA;

        return 0;
    }

    return EINVAL;
}

int get_memory_type( const char *string, bool *value )
{
    if ( !strcmp( string, "rom" ) )
    {
        *value = true;

        return 0;
    }

    if ( !strcmp( string, "ram" ) )
    {
        *value = false;

        return 0;
    }

    return EINVAL;
}

