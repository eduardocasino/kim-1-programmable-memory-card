/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * YAML parsing functions
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
#include <errno.h>
#include <yaml.h>
#include <ctype.h>
#include <limits.h>

#include "globals.h"
#include "yaml.h"
#include "scan.h"

typedef enum config_state_e {
    C_STATE_START,
    C_STATE_STREAM,
    C_STATE_DOCUMENT,
    C_STATE_SECTION,

    C_STATE_WIFI_SECTION,
    C_STATE_WIFI_DATA,
    C_STATE_WCOUNTRY,
    C_STATE_WSSID,
    C_STATE_WPASSWD,

    C_STATE_VIDEO_SECTION,
    C_STATE_VIDEO_DATA,
    C_STATE_VSYSTEM,
    C_STATE_VADDRESS,
    // C_STATE_VOUTPUT,

    C_STATE_FDC_SECTION,    
    C_STATE_FDC_DATA,
    C_STATE_FENABLED,
    C_STATE_FUSRRAM,
    C_STATE_FSYSRAM,
    C_STATE_FOPT,
    C_STATE_FDISK0_SECTION,
    C_STATE_FDISK0_DATA,
    C_STATE_FD0_FILE,
    C_STATE_FD0_RO,
    C_STATE_FDISK1_SECTION,
    C_STATE_FDISK1_DATA,
    C_STATE_FD1_FILE,
    C_STATE_FD1_RO,
    C_STATE_FDISK2_SECTION,
    C_STATE_FDISK2_DATA,
    C_STATE_FD2_FILE,
    C_STATE_FD2_RO,
    C_STATE_FDISK3_SECTION,
    C_STATE_FDISK3_DATA,
    C_STATE_FD3_FILE,
    C_STATE_FD3_RO,

    C_STATE_STOP      /* end state */
} config_state_t;

typedef enum memory_state_e {
    M_STATE_START,

    M_STATE_STREAM,
    M_STATE_DOCUMENT,
    M_STATE_MEM_DATA,

    M_STATE_DTYPE,
    M_STATE_DENABLED,
    M_STATE_DSTART,
    M_STATE_DEND,
    M_STATE_DCOUNT,
    M_STATE_DFILL,
    M_STATE_DDATA,
    M_STATE_DFILE,

    M_STATE_STOP      /* end state */
} memory_state_t;

/*
 *    <stream>        ::= STREAM-START <document>* STREAM-END
 *    <document>      ::= DOCUMENT-START MAPPING-START <mem-data>* MAPPING-END DOCUMENT-END
 *    <mem-data>      ::= "type" ("ram"|"rom") |
 *                        "enabled" (false|true) |
 *                        "start" <uint16_t> |
 *                        "end" <uint16_t> |
 *                        "fill" <uint8_t> |
 *                        "data" <binary string> |
 *                        "file" <string>
 */
static status_t consume_memory_event( memory_state_t *state, yaml_event_t *event, memmap_doc_t **memmap )
{
    char *value;
    static memmap_doc_t *document = NULL;

    switch( *state )
    {
        case M_STATE_START:
            switch ( event->type )
            {
                case YAML_STREAM_START_EVENT:
                    *state = M_STATE_STREAM;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_STREAM:
            switch ( event->type )
            {
                case YAML_DOCUMENT_START_EVENT:
                    if ( NULL == document )
                    {
                        document = malloc( sizeof( memmap_doc_t ) );
                        *memmap = document;
                    }
                    else
                    {
                        document->next = malloc( sizeof( memmap_doc_t ) );
                        document = document->next;
                    }
                    if ( NULL == document )
                    {
                        perror( "Can't allocate memory for config document" );
                        return FAILURE;
                    }
                    memset( document, 0, sizeof( *document ) );
                    *state = M_STATE_DOCUMENT;
                    break;
                case YAML_STREAM_END_EVENT:
                    *state = M_STATE_STOP;  /* All done. */
                    break;
                default:
                    fputs( "Bad memory file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DOCUMENT:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = M_STATE_MEM_DATA;
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    if ( ! document->flags.start )
                    {
                        fputs( "Bad memory file: 'start' is mandatory.\n", stderr );
                        return FAILURE;
                    }
                    if ( document->flags.end && document->end < document->start )
                    {
                        fputs( "Bad memory file: 'end' smaller than 'start'.\n", stderr );
                        return FAILURE;
                    }
                    if ( ! document->flags.end && ! document->data.value && ! document->file )
                    {
                        fputs( "Bad memory file: At least one of 'end', 'data' or 'file' must be present.\n", stderr );
                        return FAILURE;
                    }
                    if ( document->data.value && document->file )
                    {
                        fputs( "Bad memory file: 'data' and 'file' are mutually exclusive.\n", stderr );
                        return FAILURE;
                    }
                    if ( document->flags.fill && ! document->flags.end )
                    {
                        fputs( "Bad memory file: 'fill' needs an 'end'.\n", stderr );
                        return FAILURE;
                    }
                    if ( ! document->data.value && ! document->file && ! document->flags.fill && ! document->flags.enabled && ! document->flags.ro )
                    {
                        fprintf( stderr, "No action for section starting at 0x%4.4X.\n", document->start );
                        return FAILURE;
                    }
                    if ( document->data.length )
                    {
                        document->count = (uint32_t) document->data.length;
                    }
                    else if ( document->file )
                    {
                        struct stat st;
                        if ( stat( document->file, &st) )
                        {
                            fprintf( stderr, "Can't get file '%s' size: %s\n", document->file, strerror( errno ) );
                            return FAILURE;
                        }
                        document->count = (uint32_t) st.st_size;
                    }
                    else
                    {
                        document->count = document->end - document->start + 1;
                    }
                    if ( document->count + document->start > 0x10000 ||
                        ( document->flags.end && ( document->end - document->start + 1 < document->count ) ) )
                    {
                        fprintf( stderr, "Data length/file size too big in segment starting at 0x%4.4X\n", document->start );
                        return FAILURE;
                    }

                    *state = M_STATE_STREAM;
                    break;
                default:
                    fputs( "Bad memory file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_MEM_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "type" ) )
                    {
                        if ( document->flags.ro == true )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DTYPE;
                    }
                    else if ( !strcmp( value, "enabled" ) )
                    {
                        if ( document->flags.enabled == true )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DENABLED;
                    }
                    else if ( !strcmp( value, "start" ) )
                    {
                        if ( document->flags.start == true )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DSTART;
                    }
                    else if ( !strcmp( value, "end" ) )
                    {
                        if ( document->flags.end == true )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DEND;
                    }
                    else if ( !strcmp( value, "fill" ) )
                    {
                        if ( document->flags.fill == true )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DFILL;
                    }
                    else if ( !strcmp( value, "data" ) )
                    {
                        if ( document->data.value )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DDATA;
                    }
                    else if ( !strcmp( value, "file" ) )
                    {
                        if ( document->file )
                        {
                            fprintf( stderr, "Duplicated key: %s\n", value );
                            return FAILURE;
                        }
                        *state = M_STATE_DFILE;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected memory parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = M_STATE_DOCUMENT;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DTYPE:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( get_memory_type( (char *)event->data.scalar.value, &document->ro ) )
                    {
                        fprintf(stderr, "Invalid memory type: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    document->flags.ro = true;
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad memory map file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DENABLED:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( get_boolean( (char *)event->data.scalar.value, &document->enabled ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'enabled': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    document->flags.enabled = true;
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad memory map file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DSTART:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( get_uint16( (char *)event->data.scalar.value, &document->start ) )
                    {
                        fprintf(stderr, "Invalid start memory address: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    document->flags.start = true;
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DEND:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( get_uint16( (char *)event->data.scalar.value, &document->end ) )
                    {
                        fprintf(stderr, "Invalid end memory address: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    document->flags.end = true;
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DFILL:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    uint16_t fill;
                    if ( get_uint16( (char *)event->data.scalar.value, &fill ) || fill > 0xFF )
                    {
                        fprintf( stderr, "Invalid 'fill' byte: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    document->fill = (uint8_t) fill;
                    document->flags.fill = true;
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DDATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    size_t length;

                    if ( !event->data.scalar.length )
                    {
                        fputs( "'data' must not be blank\n", stderr );
                        return FAILURE;
                    }

                    if ( NULL == ( document->data.value = malloc( event->data.scalar.length )))
                    {
                        perror( "Can't allocate memory for 'data' key" );
                        return FAILURE;
                    }
                    memcpy( document->data.value, event->data.scalar.value, event->data.scalar.length );                   
                    document->data.length = event->data.scalar.length;
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case M_STATE_DFILE:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( !event->data.scalar.length )
                    {
                        fputs( "'file' must not be blank\n", stderr );
                        return FAILURE;
                    }
                    if ( NULL == ( document->file = strdup( (char *)event->data.scalar.value )))
                    {
                        perror( "Can't allocate memory for 'file' key" );
                        return FAILURE;
                    }
                    *state = M_STATE_MEM_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        default:
            fputs( "consume_memory_event(): Should not reach here!\n", stderr );
            return FAILURE;
    }
    return SUCCESS;

}

/*
 *    <stream>        ::= STREAM-START <document> STREAM-END
 *    <document>      ::= DOCUMENT-START MAPPING-START <section>* MAPPING-END DOCUMENT-END
 *    <section>       ::= "wifi" <wifi-section> |
 *                        "video" <video-section> |
 *                        "fdc" <fdc-section>
 *    <wifi-section>  ::= MAPPING-START <wifi-data>* MAPPING-END
 *    <wifi-data >    ::= "country" <country-code> |
 *                        "ssid" <string> |
 *                        "password" <string>
 *    <country-code>  ::= "US" | "CA" | 'JP3', "DE" | "NL" | "IT" | "PT" | "LU" | "NO" | "FI" | "DK" | "CH" | "CZ" | "ES" |
 *                        "GB" | "KR" | "CN" | "FR" | "HK" | "SG" | "TW" | "BR" | "IL" | "SA" | "LB" | "AE" | "ZA" | "AR" |
 *                        "AU" | "AT" | "BO" | "CL" | "GR" | "IS" | "IN" | "IE" | "KW" | "LI" | "LT" | "MX" | "MA" | "NZ" |
 *                        "PL" | "PR" | "SK" | "SI" | "TH" | "UY" | "PA" | "RU" | "KW" | "LI" | "LT" | "MX" | "MA" | "NZ" |
 *                        "PL" | "PR" | "SK" | "SI" | "TH" | "UY" | "PA" | "RU" | "EG" | "TT" | "TR" | "CR" | "EC" | "HN" |
 *                        "KE" | "UA" | "VN" | "BG" | "CY" | "EE" | "MU" | "RO" | "CS" | "ID" | "PE" | "VE" | "JM" | "BH" |
 *                        "OM" | "JO" | "BM" | "CO" | "DO" | "GT" | "PH" | "LK" | "SV" | "TN" | "PK" | "QA" | "DZ" 
 *    <video-section> ::= MAPPING-START <video-data>* MAPPING-END
 *    <video-data>    ::= "system" ("ntsc"|"pal") |
 *                        "address" <uint16_t> |
 *                        "output" ("composite"|"vga")
 *    <fdc-section>   ::= MAPPING-START <fdc-data>* MAPPING-END
 *    <fdc-data>      ::= "enabled" (false|true) |
 *                        "usrram" <uint16_t> |
 *                        "sysram" <uint16_t> |
 *                        "optswitch" (false|true) |
 *                        "disk0" <disk-section> |
 *                        "disk1" <disk-section> |
 *                        "disk2" <disk-section> |
 *                        "disk3" <disk-section>
 *    <disk-section>  ::= MAPPING-START <disk-data>* MAPPING-END
 *    <disk-data>     ::= "file" <string> |
 *                        "ro" (false|true)
 */
static status_t consume_config_event( config_state_t *state, yaml_event_t *event, config_doc_t *config )
{
    char *value;

    switch( *state )
    {
        case C_STATE_START:
            switch ( event->type )
            {
                case YAML_STREAM_START_EVENT:
                    *state = C_STATE_STREAM;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_STREAM:
            switch ( event->type )
            {
                case YAML_DOCUMENT_START_EVENT:
                    *state = C_STATE_DOCUMENT;
                    break;
                case YAML_STREAM_END_EVENT:
                    *state = C_STATE_STOP;  /* All done. */
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_DOCUMENT:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_SECTION;
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    *state = C_STATE_STREAM;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_SECTION:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "wifi" ) )
                    {
                        *state = C_STATE_WIFI_SECTION;
                    }
                    else if ( !strcmp( value, "video" ) )
                    {
                        *state = C_STATE_VIDEO_SECTION;
                    }
                    else if ( !strcmp( value, "fdc" ) )
                    {
                        *state = C_STATE_FDC_SECTION;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected section: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_DOCUMENT;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_WIFI_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_WIFI_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_VIDEO_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_VIDEO_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FDC_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_WIFI_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "country" ) )
                    {
                        *state = C_STATE_WCOUNTRY;
                    }
                    else if ( !strcmp( value, "ssid" ) )
                    {
                        *state = C_STATE_WSSID;
                    }
                    else if ( !strcmp( value, "password" ) )
                    {
                        *state = C_STATE_WPASSWD;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected wifi parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_SECTION;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_WCOUNTRY:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( get_country_code( (char *)event->data.scalar.value, config->wifi.country ) )
                    {
                        fprintf(stderr, "Invalid country code: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    *state = C_STATE_WIFI_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_WSSID:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    if ( !event->data.scalar.length )
                    {
                        fputs( "SSID must not be blank\n", stderr );
                        return FAILURE;
                    }
                    strncpy( config->wifi.ssid, (char *)event->data.scalar.value, sizeof( config->wifi.ssid ) );
                    config->wifi.ssid[sizeof( config->wifi.ssid ) - 1] = '\0';
                    *state = C_STATE_WIFI_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_WPASSWD:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    strncpy( config->wifi.password, (char *)event->data.scalar.value, sizeof( config->wifi.password ) );
                    config->wifi.password[sizeof( config->wifi.password ) - 1] = '\0';
                    *state = C_STATE_WIFI_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_VIDEO_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "system" ) )
                    {
                        *state = C_STATE_VSYSTEM;
                    }
                    else if ( !strcmp( value, "address" ) )
                    {
                        *state = C_STATE_VADDRESS;
                    }
                    /*
                    else if ( !strcmp( value, "output" ) )
                    {
                        *state = C_STATE_VOUTPUT;
                    }
                    */
                    else
                    {
                        fprintf(stderr, "Unexpected video parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_SECTION;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_VSYSTEM:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    uint16_t system;
                    if ( get_video_system( (char *)event->data.scalar.value, &system ) )
                    {
                        fprintf(stderr, "Invalid video system: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->video.system = LE16( system );
                    *state = C_STATE_VIDEO_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_VADDRESS:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    uint16_t address;
                    if ( get_uint16( (char *)event->data.scalar.value, &address ) )
                    {
                        fprintf(stderr, "Invalid video memory address: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->video.address = LE16( address );
                    *state = C_STATE_VIDEO_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FDC_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "enabled" ) )
                    {
                        *state = C_STATE_FENABLED;
                    }
                    else if ( !strcmp( value, "usrram" ) )
                    {
                        *state = C_STATE_FUSRRAM;
                    }
                    else if ( !strcmp( value, "sysram" ) )
                    {
                        *state = C_STATE_FSYSRAM;
                    }
                    else if ( !strcmp( value, "optswitch" ) )
                    {
                        *state = C_STATE_FOPT;
                    }
                    else if ( !strcmp( value, "disk0" ) )
                    {
                        *state = C_STATE_FDISK0_SECTION;
                    }
                    else if ( !strcmp( value, "disk1" ) )
                    {
                        *state = C_STATE_FDISK1_SECTION;
                    }
                    else if ( !strcmp( value, "disk2" ) )
                    {
                        *state = C_STATE_FDISK2_SECTION;
                    }
                    else if ( !strcmp( value, "disk3" ) )
                    {
                        *state = C_STATE_FDISK3_SECTION;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected fdc parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_SECTION;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FENABLED:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    bool enabled;
                    if ( get_boolean( (char *)event->data.scalar.value, &enabled ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'enabled': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.enabled = enabled ? 1 : 0;
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        case C_STATE_FUSRRAM:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    uint16_t usrram;
                    if ( get_uint16( (char *)event->data.scalar.value, &usrram ) )
                    {
                        fprintf(stderr, "Invalid user memory address: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.usrram = LE16( usrram );
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        case C_STATE_FSYSRAM:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    uint16_t sysram;
                    if ( get_uint16( (char *)event->data.scalar.value, &sysram ) )
                    {
                        fprintf(stderr, "Invalid system memory address: %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.sysram = LE16( sysram );
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FOPT:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    bool optswitch;
                    if ( get_boolean( (char *)event->data.scalar.value, &optswitch ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'optswitch': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.optswitch = optswitch ? 1 : 0;
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        case C_STATE_FDISK0_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_FDISK0_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FDISK0_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "file" ) )
                    {
                        *state = C_STATE_FD0_FILE;
                    }
                    else if ( !strcmp( value, "ro" ) )
                    {
                        *state = C_STATE_FD0_RO;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected disk0 parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD0_FILE:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    strncpy( config->fdc.disk[0].file, (char *)event->data.scalar.value, sizeof( config->fdc.disk[0].file ) );
                    config->fdc.disk[0].file[sizeof( config->fdc.disk[0].file ) - 1] = '\0';
                    *state = C_STATE_FDISK0_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD0_RO:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    bool ro;
                    if ( get_boolean( (char *)event->data.scalar.value, &ro ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'ro': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.disk[0].ro = ro ? 1 : 0;
                    *state = C_STATE_FDISK0_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        case C_STATE_FDISK1_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_FDISK1_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FDISK1_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "file" ) )
                    {
                        *state = C_STATE_FD1_FILE;
                    }
                    else if ( !strcmp( value, "ro" ) )
                    {
                        *state = C_STATE_FD1_RO;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected disk0 parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD1_FILE:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    strncpy( config->fdc.disk[1].file, (char *)event->data.scalar.value, sizeof( config->fdc.disk[1].file ) );
                    config->fdc.disk[1].file[sizeof( config->fdc.disk[1].file ) - 1] = '\0';
                    *state = C_STATE_FDISK1_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD1_RO:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    bool ro;
                    if ( get_boolean( (char *)event->data.scalar.value, &ro ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'ro': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.disk[1].ro = ro ? 1 : 0;
                    *state = C_STATE_FDISK1_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        case C_STATE_FDISK2_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_FDISK2_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FDISK2_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "file" ) )
                    {
                        *state = C_STATE_FD2_FILE;
                    }
                    else if ( !strcmp( value, "ro" ) )
                    {
                        *state = C_STATE_FD2_RO;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected disk0 parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD2_FILE:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    strncpy( config->fdc.disk[2].file, (char *)event->data.scalar.value, sizeof( config->fdc.disk[2].file ) );
                    config->fdc.disk[2].file[sizeof( config->fdc.disk[2].file ) - 1] = '\0';
                    *state = C_STATE_FDISK2_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD2_RO:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    bool ro;
                    if ( get_boolean( (char *)event->data.scalar.value, &ro ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'ro': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.disk[2].ro = ro ? 1 : 0;
                    *state = C_STATE_FDISK2_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        case C_STATE_FDISK3_SECTION:
            switch ( event->type )
            {
                case YAML_MAPPING_START_EVENT:
                    *state = C_STATE_FDISK3_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FDISK3_DATA:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    value = (char *) event->data.scalar.value;
                    if ( !strcmp( value, "file" ) )
                    {
                        *state = C_STATE_FD3_FILE;
                    }
                    else if ( !strcmp( value, "ro" ) )
                    {
                        *state = C_STATE_FD3_RO;
                    }
                    else
                    {
                        fprintf(stderr, "Unexpected disk0 parameter: %s\n", value);
                        return FAILURE;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    *state = C_STATE_FDC_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD3_FILE:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    strncpy( config->fdc.disk[3].file, (char *)event->data.scalar.value, sizeof( config->fdc.disk[3].file ) );
                    config->fdc.disk[3].file[sizeof( config->fdc.disk[3].file ) - 1] = '\0';
                    *state = C_STATE_FDISK3_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;

        case C_STATE_FD3_RO:
            switch ( event->type )
            {
                case YAML_SCALAR_EVENT:
                    bool ro;
                    if ( get_boolean( (char *)event->data.scalar.value, &ro ) )
                    {
                        fprintf(stderr, "Invalid boolean value for 'ro': %s\n", (char *)event->data.scalar.value );
                        return FAILURE;
                    }
                    config->fdc.disk[3].ro = ro ? 1 : 0;
                    *state = C_STATE_FDISK3_DATA;
                    break;
                default:
                    fputs( "Bad config file.\n", stderr );
                    return FAILURE;
            }
            break;
        
        default:
            fputs( "consume_config_event(): Should not reach here!\n", stderr );
            return FAILURE;

    }
    return SUCCESS;
}

status_t parse_config( char *file_name, config_doc_t *config )
{
    yaml_parser_t parser;
    config_state_t state = C_STATE_START;
    
    FILE *file = fopen( file_name, "r" );

    if ( NULL == file )
    {
        perror( "Can't open config file" );
        return FAILURE;
    }

    if ( !yaml_parser_initialize( &parser ) )
    {
        fputs( "Failed to initialize parser for config document.\n", stderr );
        return FAILURE;
    }

    yaml_parser_set_input_file( &parser, file );

    do
    {
        yaml_event_t event;
        int ret;

        if ( FAILURE == yaml_parser_parse( &parser, &event ) )
        {
            fprintf( stderr, "Error parsing config file.\n");
            return FAILURE;
        }

        ret = consume_config_event( &state, &event, config );

        yaml_event_delete( &event );  
        if ( ret == FAILURE )
        {
            return ret;
        }
    } 
    while ( state != C_STATE_STOP );

    yaml_parser_delete( &parser );
    fclose( file );

    return SUCCESS;
}

void free_memmap( memmap_doc_t *memmap )
{
    memmap_doc_t *next;

    while ( NULL != memmap )
    {
        next = memmap->next;

        if ( NULL != memmap->data.value )
        {
            free( memmap->data.value );
        }
        if ( NULL != memmap->file )
        {
            free( memmap->file );
        }

        free( memmap );

        memmap = next;
    }
}

status_t parse_memmap( char *file_name, memmap_doc_t **memmap )
{
    yaml_parser_t parser;
    memory_state_t state = M_STATE_START;

    FILE *file = fopen( file_name, "r" );

    if ( NULL == file )
    {
        perror( "Can't open memory map file" );
        return FAILURE;
    }

    if ( !yaml_parser_initialize( &parser ) )
    {
        fputs( "Failed to initialize parser for memory map file.\n", stderr );
        return FAILURE;
    }

    yaml_parser_set_input_file( &parser, file );

    *memmap = NULL;

    do
    {
        yaml_event_t event;
        int ret;

        if ( FAILURE == yaml_parser_parse( &parser, &event ) )
        {
            fprintf( stderr, "Error parsing memory map file.\n");
            free_memmap( *memmap );
            return FAILURE;
        }

        ret = consume_memory_event( &state, &event, memmap );

        yaml_event_delete( &event );  
        if ( ret == FAILURE )
        {
            free_memmap( *memmap );
            return ret;
        }

    } 
    while ( state != M_STATE_STOP );

    yaml_parser_delete( &parser );
    fclose( file );

    return SUCCESS;
}