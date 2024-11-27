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

#include "config.h"
#include "picowi.h"
#include "httpd.h"
#include "video.h"
#include "debug.h"

#define MAX_DATA_LEN ( TCP_MSS - TCP_DATA_OFFSET )

typedef enum { AC_ENABLE, AC_DISABLE, AC_SETRAM, AC_SETROM } action_t;

typedef void ( *data_copy_t )( http_request_t *http_req, uint8_t *data, int len );


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
    web_page_handler( HTTP_GET,   "/ramrom/range",         handle_ramrom_get );
    web_page_handler( HTTP_PATCH, "/ramrom/range/data",    handle_ramrom_data_patch );
    web_page_handler( HTTP_PATCH, "/ramrom/range/enable",  handle_ramrom_enable_patch );
    web_page_handler( HTTP_PATCH, "/ramrom/range/disable", handle_ramrom_disable_patch );
    web_page_handler( HTTP_PATCH, "/ramrom/range/setrom",  handle_ramrom_setrom_patch );
    web_page_handler( HTTP_PATCH, "/ramrom/range/setram",  handle_ramrom_setram_patch );
    web_page_handler( HTTP_PATCH, "/ramrom/range",         handle_ramrom_patch );
    web_page_handler( HTTP_PUT,   "/ramrom/restore",       handle_restore_put );
    web_page_handler( HTTP_PUT,   "/ramrom/video",         handle_video_put );
    web_page_handler( HTTP_GET,   "/ramrom/video",         handle_video_get );

    while ( true )
    {
        // Get any events, poll the network-join state machine
        net_event_poll();
        net_state_poll();
        tcp_socks_poll();
    }
}
