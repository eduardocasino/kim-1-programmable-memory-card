/*
 * Configuration web server for the KIM-1 Programmable Memory Board
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"

#include "config.h"
#include "picowi.h"
#include "httpd.h"
#include "video.h"
#include "imd.h"
#include "fdc.h"
#include "debug.h"

#include "ff.h"
#include "f_util.h"

#define MAX_DATA_LEN ( TCP_MSS - TCP_DATA_OFFSET )

typedef enum { AC_ENABLE, AC_DISABLE, AC_SETRAM, AC_SETROM } action_t;

typedef void ( *data_copy_t )( http_request_t *http_req, uint8_t *data, int len );

static char *image_mounted = "Image mounted";
static char *image_or_drive_mounted = "Image or drive mounted";
static char *file_exists = "File exists";
static char *not_mounted = "Not mounted";


static void raw_data_copy( http_request_t *http_req, uint8_t *data, int len )
{
    memcpy( &http_req->buf[http_req->recvd], data, len );
    http_req->recvd += len;
}

static void bin_data_copy( http_request_t *http_req, uint8_t *data, int len )
{
    int copied = 0;
    uint8_t *dp = &http_req->buf[http_req->recvd*2];

    while ( len - copied )
    {
        *dp = *data++;
        dp += 2;
        ++copied;
    }

    http_req->recvd += len;
}

// Handler for PATCH /ramrom/range raw and data requests
static int _handle_ramrom_range( int sock, char *req, int oset, int module, data_copy_t copy_fn )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    int num_args = 0;
    char *start;
    uint32_t u_start;
    char *ends;

    int datalen;

    static http_request_t http_req = {0};

    if ( req )
    {
        if ( http_req.seq == ts->seq )
        {
            copy_fn( &http_req, req, oset );
        }
        else
        {
            if ( httpd_init_http_request( &http_req, ts, req, oset ) )
            {
                return ( web_400_bad_request( sock ) );
            }

            datalen = oset - ((char *)http_req.bodyp - req);

            debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );
        
            for ( int i= 0; i < http_req.paramcount; ++i )
            {
                if ( strcmp( "start", http_req.params[i] ) == 0 )
                {
                    start = http_req.param_vals[i];
                    ++num_args;
                }
            }

            if ( num_args != 1 || strlen( start ) > 4 )
            {
                return ( web_400_bad_request( sock ) );
            }

            u_start = strtoul( start, &ends, 16 );

            if ( *ends || !http_req.content_len || http_req.content_len % module || u_start + http_req.content_len/module - 1 > 0xFFFF )
            {
                return ( web_400_bad_request( sock ) );
            }

            http_req.buf = (uint8_t *)&mem_map[u_start];

            if ( datalen )
            {
                copy_fn( &http_req, http_req.bodyp, datalen );
            }

        }
    }

    if ( http_req.recvd == http_req.content_len )
    {
        n = web_resp_add_str( sock,
                        HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return (n);
}

// Handler for PATCH /ramrom/range
static int handle_ramrom_patch( int sock, char *req, int oset )
{
    return _handle_ramrom_range( sock, req, oset, 2, raw_data_copy );
}

// Handler for PATCH /ramrom/range/data
static int handle_ramrom_data_patch( int sock, char *req, int oset )
{
    return _handle_ramrom_range( sock, req, oset, 1, bin_data_copy );
}

// Handler for PATCH /ramrom/range/<actions>
static int _handle_ramrom_actions_patch( int sock, char *req, int oset, action_t action )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    char *start, *count;
    int num_args = 0;

    uint32_t u_start, u_count, idx;

    char *ends, *endc;

    uint16_t *dp;

    http_request_t http_req = {0};

    if ( req )
    {
            if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for ( int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "start", http_req.params[i] ) == 0 )
            {
                start = http_req.param_vals[i];
                ++num_args;
            }
            else if ( strcmp( "count", http_req.params[i] ) == 0 )
            {
                count = http_req.param_vals[i];
                ++num_args;
            }
        }

        if ( num_args != 2 || strlen( count ) > 5 || strlen( start ) > 4 )
        {
            return ( web_400_bad_request( sock ) );
        }

        u_start = strtoul( start, &ends, 16 );
        u_count = strtoul( count, &endc, 16 );

        if ( *ends || *endc || !count || u_start + u_count - 1 > 0xFFFF )
        {
            return ( web_400_bad_request( sock ) );
        }

        dp = &mem_map[u_start];

        for ( idx = 0; idx < u_count; ++idx, ++dp )
        {
            switch ( action )
            {
                case AC_ENABLE:
                    *dp &= ~MEM_ATTR_CE_MASK;
                    break;

                case AC_DISABLE:
                    *dp |= MEM_ATTR_CE_MASK;
                    break;

                case AC_SETROM:
                    *dp &= ~MEM_ATTR_RW_MASK;
                    break;

                case AC_SETRAM:
                default:
                    *dp |= MEM_ATTR_RW_MASK;
                    break;
            }
        }
        
        n = web_resp_add_str( sock,
                        HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return ( n );
}

// Handler for PATCH /ramrom/range/enable
static int handle_ramrom_enable_patch( int sock, char *req, int oset )
{
    return _handle_ramrom_actions_patch( sock, req, oset, AC_ENABLE );
}

// Handler for PATCH /ramrom/range/disable
static int handle_ramrom_disable_patch( int sock, char *req, int oset )
{
    return _handle_ramrom_actions_patch( sock, req, oset, AC_DISABLE );
}

// Handler for PATCH /ramrom/range/setrom
static int handle_ramrom_setrom_patch( int sock, char *req, int oset )
{
    return _handle_ramrom_actions_patch( sock, req, oset, AC_SETROM );
}

// Handler for PATCH /ramrom/range/setram
static int handle_ramrom_setram_patch( int sock, char *req, int oset )
{
    return _handle_ramrom_actions_patch( sock, req, oset, AC_SETRAM );
}

// Handler for GET /ramrom/range
static int handle_ramrom_get( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    uint32_t u_start, u_count;

    char *start, *count;
    int num_args = 0;

    char *ends, *endc;

    static http_request_t http_req = {0};


    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for (int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "start", http_req.params[i] ) == 0 )
            {
                start = http_req.param_vals[i];
                ++num_args;
            }
            else if ( strcmp( "count", http_req.params[i] ) == 0 )
            {
                count = http_req.param_vals[i];
                ++num_args;
            }
        }

        if ( num_args != 2 || strlen( count ) > 5 || strlen( start ) > 4 )
        {
            return ( web_400_bad_request( sock ) );
        }

        u_start = strtoul( start, &ends, 16 );
        u_count = strtoul( count, &endc, 16 );

        if ( *ends || *endc || !count || u_start + u_count - 1 > 0xFFFF )
        {
            return ( web_400_bad_request( sock ) );
        }
        
        u_count *= 2;

        http_req.buf = (uint8_t *)&mem_map[u_start];
        http_req.content_len = u_count;

        n = web_resp_add_str( sock,
            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_CONTENT_BINARY );
        n += web_resp_add_content_len( sock, u_count );
        n += web_resp_add_str( sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
        http_req.hlen = n;

        n += web_resp_add_data( sock, http_req.buf, MIN( u_count, MAX_DATA_LEN - http_req.hlen ) );
    }
    else
    {
        n = MIN( MAX_DATA_LEN, http_req.content_len + http_req.hlen - oset );

        if ( n > 0 )
        {
            web_resp_add_data( sock, &http_req.buf[oset - http_req.hlen], n );
        }
        else
        {
            tcp_sock_close( sock );
        }
    }

    return ( n );
}

// Handler for PUT /ramrom/restore
static int handle_restore_put( int sock, char *req, int oset )
{
    int n = 0;

    if ( req )
    {

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        config_copy_default_memory_map( mem_map );
        video_set_mem_start( config.video.address );
        
        n = web_resp_add_str( sock,
                            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_ORIGIN_ANY
                            HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
        tcp_sock_close( sock );

    }

    return (n);
}

// Handler for PUT /ramrom/video
static int handle_video_put( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    char *video;
    int num_args = 0;

    uint32_t u_video;

    char *ends;

    http_request_t http_req = {0};

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for ( int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "address", http_req.params[i] ) == 0 )
            {
                video = http_req.param_vals[i];
                ++num_args;
            }
        }

        if ( num_args != 1 || strlen( video ) > 4 )
        {
            return ( web_400_bad_request( sock ) );
        }

        u_video = strtoul( video, &ends, 16 );

        if ( *ends || u_video < 0x2000 || u_video > 0xDFFF || u_video % 0x2000 )
        {
            return ( web_400_bad_request( sock ) );
        }

        video_set_mem_start( ( uint16_t )u_video );

        n = web_resp_add_str( sock,
                        HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return ( n );

}

// Handler for GET /ramrom/video
static int handle_video_get( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    static http_request_t http_req = {0};

    static uint8_t addr_buf[5];

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        sprintf( addr_buf, "%4.4X", video_get_mem_start() );
        http_req.buf = addr_buf;
        http_req.content_len = sizeof( addr_buf );

        n = web_resp_add_str( sock,
            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_CONTENT_BINARY );
        n += web_resp_add_content_len( sock, http_req.content_len );
        n += web_resp_add_str( sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
        http_req.hlen = n;

        n += web_resp_add_data( sock, http_req.buf, MIN( http_req.content_len, MAX_DATA_LEN - http_req.hlen ) );
    }
    else
    {
        n = MIN( MAX_DATA_LEN, http_req.content_len + http_req.hlen - oset );

        if ( n > 0 )
        {
            web_resp_add_data( sock, &http_req.buf[oset - http_req.hlen], n );
        }
        else
        {
            tcp_sock_close( sock );
        }
    }

    return ( n );
}

static uint8_t img_buffer[MAX_SECTOR_SIZE+4];
static char sd_buffer[256];
static uint8_t result[2];

// Handler for GET /sd/dir
static int handle_dir_get( int sock, char *req, int oset )
{
    int n = 0, h = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    static http_request_t http_req = {0};

    static dir_t dir = {0};

    static FILINFO fno;

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        // Close any active dir listing

        if ( dir.is_open )
        {
            f_closedir( &dir.dir );
            dir.is_open = false;
        }

        if ( FR_OK == f_opendir( &dir.dir, "/" ) )
        {
            dir.is_open = true;
        }
        else
        {
            return ( web_500_internal_server_error( sock ) );
        }

        h = web_resp_add_str( sock,
            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_CONTENT_TEXT
            HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
    }
    else
    {
        if ( !dir.is_open )
        {
            return ( web_500_internal_server_error( sock ) );
        }
    }

    while ( FR_OK == f_readdir( &dir.dir, &fno ) && fno.fname[0] )
    {
        if ( ! ( fno.fattrib & (AM_HID | AM_SYS | AM_DIR ) ) )
        {
            sprintf( sd_buffer, "%s\r\n", fno.fname );
            n = web_resp_add_data( sock, sd_buffer, strlen( sd_buffer ) );

            break;
        }
    }

    if ( !n )
    {
        f_closedir( &dir.dir );
        dir.is_open = false;
        tcp_sock_close( sock );
    }

    return ( h + n );
}

// Handler for PATCH /sd/file
static int handle_file_patch( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    http_request_t http_req = {0};

    char *old_filename = NULL;
    char *new_filename = NULL;

    fdc_sm_t *fdc = fdc_get_sm();

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for ( int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "fname", http_req.params[i] ) == 0 )
            {
                old_filename = http_req.param_vals[i];
            }
            if ( strcmp( "nfname", http_req.params[i] ) == 0 )
            {
                new_filename = http_req.param_vals[i];
            }
        }

        if ( !old_filename || !new_filename
                || strlen( old_filename ) == 0 || strlen( new_filename ) == 0 )
        {
            return ( web_400_bad_request( sock ) );
        }

        MUTEX_EXT_CALL( imd_image_rename, &fdc->sd, result, old_filename, new_filename );

        if ( result[0] != ST4_NORMAL_TERM )
        {
            if ( result[0] & ST4_NOT_FOUND )
            {
                return ( web_404_not_found( sock ) );
            }
            if ( result[1] & ST5_IMG_NAME )
            {
                return ( web_400_bad_request( sock ) );
            }
            if ( result[1] & ST5_IMG_EXISTS )
            {
                return ( web_409_conflict( sock, file_exists ) );
            }
            if (   result[1] & ST5_IMG_MOUNTED
                || result[1] & ST5_IMG2_MOUNTED
               )
            {
                return ( web_409_conflict( sock, image_mounted ) );
            }

            return ( web_500_internal_server_error( sock ) );
        }

        n = web_resp_add_str( sock, HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return n;
}

// Handler for GET /sd/mnt
static int handle_mount_get( int sock, char *req, int oset )
{
    static int drive;
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    static http_request_t http_req = {0};

    imd_sd_t *sd = &fdc_get_sm()->sd;

    if ( req )
    {
        drive = 0;

        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        n = web_resp_add_str( sock,
            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_CONTENT_TEXT
            HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
    }

    if ( imd_disk_is_drive_mounted( sd, drive ) )
    {
        sprintf( sd_buffer, "%d: -> %s %s\r\n", drive,
                        imd_disk_get_imagename( sd, drive ),
                        imd_disk_is_ro( sd, drive ) ? "(RO)" : "" );
    }
    else
    {
        sprintf( sd_buffer, "%d:\r\n", drive );
    }

    n = web_resp_add_data( sock, sd_buffer, strlen( sd_buffer ) );

    if ( ++drive == MAX_DRIVES )
    {
        tcp_sock_close( sock );
    }

    return ( n );
}

// Handler for POST /sd/mnt
static int handle_mount_post( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    http_request_t http_req = {0};

    char *img = NULL;
    int drive = -1;
    bool ro = false;

    fdc_sm_t *fdc = fdc_get_sm();

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for ( int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "img", http_req.params[i] ) == 0 )
            {
                img = http_req.param_vals[i];
            }
            if ( strcmp( "drive", http_req.params[i] ) == 0 )
            {
                drive = atoi( http_req.param_vals[i] );
            }
            if ( strcmp( "ro", http_req.params[i] ) == 0 )
            {
                ro = (bool) atoi( http_req.param_vals[i] );
            }
        }

        if ( drive < 0 || drive >= MAX_DRIVES || !img )
        {
            return ( web_400_bad_request( sock ) );
        }

        MUTEX_EXT_CALL( imd_disk_mount, &fdc->sd, drive, result, img, ro );

        if ( result[0] != ST4_NORMAL_TERM )
        {
            if ( result[0] & ST4_NOT_FOUND )
            {
                return ( web_404_not_found( sock ) );
            }
            if ( result[1] & ST5_IMG_NAME )
            {
                return ( web_400_bad_request( sock ) );
            }
            if (    result[1] & ST5_IMG_MOUNTED
                 || result[1] & ST5_DRV_MOUNTED
               )
            {
                return ( web_409_conflict( sock, image_or_drive_mounted ) );
            }
            if ( result[1] & ST5_IMG_INVALID )
            {
                return ( web_499_invalid_image_format( sock ) );
            }

            return ( web_500_internal_server_error( sock ) );
        }

        n = web_resp_add_str( sock, HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return n;
}

// Handler for DELETE /sd/mnt
static int handle_mount_del( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    http_request_t http_req = {0};

    int drive = -1;

    fdc_sm_t *fdc = fdc_get_sm();

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for ( int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "drive", http_req.params[i] ) == 0 )
            {
                drive = atoi( http_req.param_vals[i] );
            }
        }

        if ( drive < 0 || drive >= MAX_DRIVES )
        {
            return ( web_400_bad_request( sock ) );
        }

        MUTEX_EXT_CALL( imd_disk_unmount, &fdc->sd, drive, result );

        if ( result[0] != ST4_NORMAL_TERM )
        {
            if ( result[1] & ST5_DRV_NOT_MOUNTED )
            {
                return ( web_409_conflict( sock, not_mounted ) );
            }
            return ( web_500_internal_server_error( sock ) );
        }

        n = web_resp_add_str( sock, HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return n;
}

// Handler for GET /sd/file
static int handle_file_get( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    char *fname = NULL;
    static FIL fp;
    FRESULT fr;
    UINT datalen, rcount;

    fdc_sm_t *fdc = fdc_get_sm();

    static http_request_t http_req = {0};

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for (int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "fname", http_req.params[i] ) == 0 )
            {
                fname = http_req.param_vals[i];
            }
        }

        if ( !fname )
        {
            return ( web_400_bad_request( sock ) );
        }

        if ( imd_disk_is_image_mounted( &fdc->sd, fname ) )
        {
            return ( web_409_conflict( sock, image_mounted ) );
        }

        if ( ! mutex_enter_timeout_ms( &fdc->mutex, MUTEX_TMOUT ) )
        {
            return ( web_500_internal_server_error( sock ) );
        }

        if ( FR_OK != (fr = f_open( &fp, fname, FA_OPEN_EXISTING | FA_READ ) ) )
        {
            debug_printf( DBG_ERROR, "f_open error: %s (%d)\n", FRESULT_str(fr), fr );

            mutex_exit( &fdc->mutex );

            switch ( fr )
            {
                case FR_NO_FILE:
                case FR_NO_PATH:
                    return ( web_404_not_found( sock ) );

                case FR_INVALID_NAME:
                    return ( web_400_bad_request( sock ) );

                default:
                    return ( web_500_internal_server_error( sock ) );
            }
        }

        n = web_resp_add_str( sock,
            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_CONTENT_BINARY );
        n += web_resp_add_content_len( sock, f_size( &fp ) );
        n += web_resp_add_str( sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
    }
    else
    {
        datalen = MIN( MAX_SECTOR_SIZE+4, MAX_DATA_LEN );

        if ( FR_OK == ( fr = f_read( &fp, img_buffer, datalen, &rcount ) ) )
        {
            if ( rcount )
            {
                n = web_resp_add_data( sock, img_buffer, rcount );
            }
        }

        if ( !n )
        {
            f_close( &fp );
            mutex_exit( &fdc->mutex );
            tcp_sock_close( sock );
        }
    }

    return ( n );
}

// Handler for POST /sd/file
static int handle_file_post( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    char *fname = NULL;
    char *nfname = NULL;
    bool owrite = false;

    static FIL fp;
    BYTE mode = FA_WRITE;
    FRESULT fr;
    UINT datalen, wcount;

    fdc_sm_t *fdc = fdc_get_sm();

    static http_request_t http_req = {0};

    if ( req )
    {
        if ( http_req.seq == ts->seq )
        {
            int copied = 0;

            fr = f_write( &fp, req, oset, &wcount );

            if ( FR_OK != fr )
            {
                debug_printf( DBG_ERROR, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
                f_close( &fp );
                return ( web_500_internal_server_error( sock ) );
            }
            if ( wcount != oset )
            {
                f_close( &fp );
                return ( web_507_insufficient_storage( sock ) );
            }

            http_req.recvd += oset;
        }
        else
        {
            if ( httpd_init_http_request( &http_req, ts, req, oset ) )
            {
                return ( web_400_bad_request( sock ) );
            }

            datalen = oset - ((char *)http_req.bodyp - req);

            debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

            for ( int i= 0; i < http_req.paramcount; ++i )
            {
                if ( strcmp( "fname", http_req.params[i] ) == 0 )
                {
                    fname = http_req.param_vals[i];
                }
                if ( strcmp( "nfname", http_req.params[i] ) == 0 )
                {
                    nfname = http_req.param_vals[i];
                }
                if ( strcmp( "owrite", http_req.params[i] ) == 0 )
                {
                    owrite = (bool) atoi( http_req.param_vals[i] );
                }
            }

            if ( !fname )
            {
                return ( web_400_bad_request( sock ) );
            }

            if ( imd_disk_is_image_mounted( &fdc->sd, fname ) )
            {
                return ( web_409_conflict( sock, image_mounted ) );
            }

            if ( nfname )
            {
                MUTEX_EXT_CALL( imd_image_copy,
                                &fdc->sd, result, img_buffer, MAX_SECTOR_SIZE+4,
                                fname, nfname, owrite );

                if ( result[0] != ST4_NORMAL_TERM )
                {
                    if ( result[0] & ST4_BAD_PARAM )
                    {
                        return ( web_400_bad_request( sock ) );
                    }
                    if ( result[0] & ST4_NOT_FOUND )
                    {
                        return ( web_404_not_found( sock ) );
                    }
                    if ( result[1] & ST5_IMG_EXISTS )
                    {
                        return ( web_409_conflict( sock, file_exists ) );
                    }
                    if ( result[1] & ST5_IMG2_MOUNTED )
                    {
                        return ( web_409_conflict( sock, image_mounted ) );
                    }
                    if ( result[1] & ST5_IMG_NAME )
                    {
                        return ( web_400_bad_request( sock ) );
                    }
                    if ( result[1] & ST5_DISK_FULL )
                    {
                        return ( web_507_insufficient_storage( sock ) );
                    }

                    return ( web_500_internal_server_error( sock ) );
                }
            }
            else
            {
                mode |= owrite ? FA_CREATE_ALWAYS : FA_CREATE_NEW;

                if ( ! mutex_enter_timeout_ms( &fdc->mutex, MUTEX_TMOUT ) )
                {
                    return ( web_500_internal_server_error( sock ) );
                }

                if ( FR_OK != ( fr = f_open( &fp, fname, mode ) ) )
                {
                    debug_printf( DBG_ERROR, "f_open error: %s (%d)\n", FRESULT_str(fr), fr );

                    mutex_exit( &fdc->mutex );

                    switch( fr )
                    {
                        case FR_EXIST:
                            return ( web_409_conflict( sock, file_exists ) );
                        case FR_INVALID_NAME:
                            return ( web_400_bad_request( sock ) );
                        default:
                            return ( web_500_internal_server_error( sock ) );
                    }
                }

                if ( datalen )
                {
                    fr = f_write( &fp, http_req.bodyp, datalen, &wcount );

                    if ( FR_OK != fr )
                    {
                        debug_printf( DBG_ERROR, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
                        f_close( &fp );
                        mutex_exit( &fdc->mutex );
                        return ( web_500_internal_server_error( sock ) );
                    }
                    if ( wcount != datalen )
                    {
                        f_close( &fp );
                        mutex_exit( &fdc->mutex );
                        return ( web_507_insufficient_storage( sock ) );
                    }

                    http_req.recvd += datalen;
                }
            }
        }
    }

    if ( http_req.recvd == http_req.content_len )
    {
        n = web_resp_add_str( sock,
                        HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
        f_close( &fp );
        mutex_exit( &fdc->mutex );
    }

    return (n);
}

// Handler for DEL /sd/file
static int handle_file_del( int sock, char *req, int oset )
{
    int n = 0;

    NET_SOCKET *ts = &net_sockets[sock];

    http_request_t http_req = {0};

    char *fname = NULL;
    uint8_t tracks, spt, ssize, filler;
    bool packed = false;

    fdc_sm_t *fdc = fdc_get_sm();

    if ( req )
    {
        if ( httpd_init_http_request( &http_req, ts, req, oset ) )
        {
            return ( web_400_bad_request( sock ) );
        }

        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        for ( int i= 0; i < http_req.paramcount; ++i )
        {
            if ( strcmp( "fname", http_req.params[i] ) == 0 )
            {
                fname = http_req.param_vals[i];
            }
        }

        if ( !fname || strlen( fname ) == 0 )
        {
            return ( web_400_bad_request( sock ) );
        }

        MUTEX_EXT_CALL( imd_image_erase, &fdc->sd, result, fname );

        if ( result[0] != ST4_NORMAL_TERM )
        {
            if ( result[0] & ST4_NOT_FOUND )
            {
                return ( web_204_no_content( sock ) );
            }
            if ( result[1] & ST5_IMG_NAME )
            {
                return ( web_400_bad_request( sock ) );
            }
            if ( result[1] & ST5_IMG_MOUNTED )
            {
                return ( web_409_conflict( sock, image_mounted ) );
            }

            return ( web_500_internal_server_error( sock ) );
        }

        n = web_resp_add_str( sock, HTTP_200_OK HTTP_SERVER HTTP_NOCACHE );
        n += web_resp_add_content_len(sock, 0);
        n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
        tcp_sock_close( sock );
    }

    return n;
}

// Handler for POST /sd/mnt/save
static int handle_save_post( int sock, char *req, int oset )
{
    int n = 0;

    fdc_sm_t *fdc = fdc_get_sm();

    if ( req )
    {
        debug_printf( DBG_INFO, "\nTCP socket %d Rx %s\n", sock, strtok( req, "\r\n" ) );

        MUTEX_EXT_CALL( imd_save_mounts, &fdc->sd, result );

        if ( result[0] != ST4_NORMAL_TERM )
        {
            return ( web_500_internal_server_error( sock ) );
        }
     
        n = web_resp_add_str( sock,
                            HTTP_200_OK HTTP_SERVER HTTP_NOCACHE HTTP_ORIGIN_ANY
                            HTTP_CONNECTION_CLOSE HTTP_HEADER_END );
        tcp_sock_close( sock );

    }

    return (n);
}

void webserver_run( void )
{
    int server_sock;
    struct sockaddr_in server_addr;

    server_sock = socket( AF_INET, SOCK_STREAM, 0) ;
    memset( &server_addr, 0, sizeof( server_addr ) ); 
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons( HTTPORT );         
    if ( server_sock < 0 ||
        bind( server_sock, ( struct sockaddr * )&server_addr, sizeof( server_addr ) ) < 0 ||
        listen( server_sock, 3 ) < 0 )
    { 
        perror( "socket/bind/listen failed" ); 
        return; 
    }
    
    debug_printf( DBG_INFO, "Web server on port %u\n", HTTPORT );
    web_page_handler( HTTP_GET,    "/ramrom/range",         handle_ramrom_get );
    web_page_handler( HTTP_PATCH,  "/ramrom/range/data",    handle_ramrom_data_patch );
    web_page_handler( HTTP_PATCH,  "/ramrom/range/enable",  handle_ramrom_enable_patch );
    web_page_handler( HTTP_PATCH,  "/ramrom/range/disable", handle_ramrom_disable_patch );
    web_page_handler( HTTP_PATCH,  "/ramrom/range/setrom",  handle_ramrom_setrom_patch );
    web_page_handler( HTTP_PATCH,  "/ramrom/range/setram",  handle_ramrom_setram_patch );
    web_page_handler( HTTP_PATCH,  "/ramrom/range",         handle_ramrom_patch );
    web_page_handler( HTTP_PUT,    "/ramrom/restore",       handle_restore_put );
    web_page_handler( HTTP_PUT,    "/ramrom/video",         handle_video_put );
    web_page_handler( HTTP_GET,    "/ramrom/video",         handle_video_get );
    web_page_handler( HTTP_GET,    "/sd/file",              handle_file_get );
    web_page_handler( HTTP_POST,   "/sd/file",              handle_file_post );
    web_page_handler( HTTP_PATCH,  "/sd/file",              handle_file_patch );
    web_page_handler( HTTP_DELETE, "/sd/file",              handle_file_del );
    web_page_handler( HTTP_POST,   "/sd/mnt/save",          handle_save_post );
    web_page_handler( HTTP_GET,    "/sd/mnt",               handle_mount_get );
    web_page_handler( HTTP_POST,   "/sd/mnt",               handle_mount_post );
    web_page_handler( HTTP_DELETE, "/sd/mnt",               handle_mount_del );
    web_page_handler( HTTP_GET,    "/sd",                   handle_dir_get );

    while ( true )
    {
        // Get any events, poll the network-join state machine
        net_event_poll();
        net_state_poll();
        tcp_socks_poll();
    }
}
