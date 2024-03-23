// Credit: https://github.com/LearnEmbeddedSystems/pico-w-webserver-template
//

#include <string.h>

#include "pico/stdlib.h"

#include "lwipopts.h"
#include "config.h"
#include "webserver.h"
#include "debug.h"

static const tHandler handler_list[] = {
    { HTTP_GET,   "/ramrom/range",      handle_ramrom_get, NULL,                        NULL,                        NULL },
    { HTTP_PATCH, "/ramrom/range",        NULL,            handle_ramrom_begin,         handle_ramrom_receive,                  handle_ramrom_finished },
    { HTTP_PATCH, "/ramrom/range/data",   NULL,            handle_ramrom_begin,         handle_ramrom_receive_data,             handle_ramrom_finished },
    { HTTP_PATCH, "/ramrom/range/or",     NULL,            handle_ramrom_begin,         handle_ramrom_receive_or,               handle_ramrom_finished },
    { HTTP_PATCH, "/ramrom/range/and",    NULL,            handle_ramrom_begin,         handle_ramrom_receive_and,              handle_ramrom_finished },
    { HTTP_PATCH, "/ramrom/range/enable", NULL,            handle_ramrom_enable_begin,  handle_ramrom_actions_receive,  handle_ramrom_actions_finished },
    { HTTP_PATCH, "/ramrom/range/disable",NULL,            handle_ramrom_disable_begin, handle_ramrom_actions_receive,  handle_ramrom_actions_finished },
    { HTTP_PATCH, "/ramrom/range/setrom", NULL,            handle_ramrom_setrom_begin,  handle_ramrom_actions_receive,  handle_ramrom_actions_finished },
    { HTTP_PATCH, "/ramrom/range/setram", NULL,            handle_ramrom_setram_begin,  handle_ramrom_actions_receive,  handle_ramrom_actions_finished },
    { HTTP_PUT,   "/ramrom/restore",      NULL,            handle_ramrom_restore_begin, handle_ramrom_actions_receive,  handle_ramrom_actions_finished },
    { HTTP_NULL }
}; 

err_t _handle_ramrom_receive( struct http_state *hs, struct pbuf *p, operation_t op )
{
    err_t err = ERR_OK;
    u32_t r, copied, len;
    static u16_t data;
    static bool reminder = false;

    debug_printf( "hs->left = %d, p->tot_len = %d\n", hs->left, p->tot_len );

    len = ( p->tot_len > hs->left ) ? hs->left : p->tot_len;

    if ( op == OP_REPLACE )
    {
            copied = pbuf_copy_partial( p, hs->file, len, 0 );
            hs->file += copied;

    }
    else {
        copied = 0;

        while ( p->tot_len - copied > 1 )
        {
            if ( reminder )
            {
                reminder = false;

                pbuf_copy_partial( p, ((u8_t *)&data)+1, 1, copied );

                r = 2;              // Byte copied + reminder
                copied += 1;

            }
            else
            {
                r = pbuf_copy_partial( p, &data, 2, copied );
                copied += r;
            }

            switch ( op )
            {
                case ( OP_DATA ):
                    *(u16_t *)hs->file = ( *(u16_t *)hs->file & MEM_ATTR_MASK ) | ( data & MEM_DATA_MASK );
                    break;

                case ( OP_AND ):
                    *(u16_t *)hs->file &= data;
                    break;

                case ( OP_OR ):
                default:
                    *(u16_t *)hs->file |= data;
            }

            hs->file += r;
        }
    }

    if ( copied != len )
    {
        if ( p->tot_len - copied == 1 )
        {
            pbuf_copy_partial( p, (u8_t *)&data, 1, copied );
            reminder = true;
        }
        else
            err = ERR_BUF;
    }

    hs->left -= copied;

    pbuf_free( p );

    return err;
}

err_t handle_ramrom_get( struct http_state *hs )
{
    char *start, *count;
    uint32_t u_start, u_count;
    int num_args = 0;

    for (int i= 0; i < hs->paramcount; ++i )
    {
        if ( strcmp( "start", hs->params[i] ) == 0 )
        {
            start = hs->param_vals[i];
            ++num_args;
        }
        else if ( strcmp( "count", hs->params[i] ) == 0 )
        {
            count = hs->param_vals[i];
            ++num_args;
        }
    }

    if ( num_args != 2 || strlen( count ) > 5 || strlen( start ) > 4 )
    {
        return http_error_response( hs, HTTP_HDR_BAD_REQUEST );
    }
    else
    {
        size_t len;
        char *ends, *endc;
        u8_t err;

        // Init headers
        //
        hs->hdr_index = 0;
        hs->hdr_pos = 0;

        u_start = strtoul(start, &ends, 16);
        u_count = strtoul(count, &endc, 16);

        if ( *ends || *endc || !count || u_start + u_count - 1 > 0xFFFF )
        {
            return http_error_response( hs, HTTP_HDR_BAD_REQUEST );
        }

        sprintf( hs->hdr_content_len, "%u", u_count*2 );
        len = strlen( hs->hdr_content_len );
        SMEMCPY(&hs->hdr_content_len[len], CRLF, 3);

        hs->hdrs[HDR_STRINGS_IDX_HTTP_STATUS] = g_psHTTPHeaderStrings[HTTP_HDR_OK];
        hs->hdrs[HDR_STRINGS_IDX_SERVER_NAME] = g_psHTTPHeaderStrings[HTTP_HDR_SERVER];
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN] = g_psHTTPHeaderStrings[HTTP_HDR_CONTENT_LENGTH];
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_NR] = hs->hdr_content_len;
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_TYPE] = HTTP_HDR_APP;

        http_send_headers(hs->pcb, hs);

    }

    hs->buf  = ( uint8_t * )&mem_map[u_start];
    hs->file = ( uint8_t * )&mem_map[u_start];
    hs->left = u_count*2;

    http_send( hs->pcb, hs );

    return ERR_OK;
}

err_t handle_ramrom_begin(struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code)
{
    char *start;
    int num_args = 0;
    uint32_t u_start;

    for (int i= 0; i < hs->paramcount; ++i )
    {
        debug_printf("Param name: %s, Param value: %s\n", hs->params[i], hs->param_vals[i]);
        if ( strcmp( "start", hs->params[i] ) == 0 )
        {
            start = hs->param_vals[i];
            ++num_args;
        }
    }

    if ( num_args != 1 || strlen( start ) > 4 )
    {
        return http_error_response( hs, HTTP_HDR_BAD_REQUEST );
    }
    else
    {
        char *ends;
        u8_t err;


        // Init headers
        //
        hs->hdr_index = 0;
        hs->hdr_pos = 0;

        u_start = strtoul(start, &ends, 16);

        if ( *ends || !content_len || content_len % 2 || u_start + content_len/2 - 1 > 0xFFFF )
        {
            *code = HTTP_HDR_BAD_REQUEST;
            return ERR_ARG;
        }

        hs->buf  = (uint8_t *)&mem_map[u_start];
        hs->file = (uint8_t *)&mem_map[u_start];
        hs->left = content_len;

        debug_printf( "content_len = %d\n", content_len );

        return ERR_OK;
    }
}

err_t handle_ramrom_receive(struct http_state *hs, struct pbuf *p)
{
    return _handle_ramrom_receive( hs, p, OP_REPLACE );
}

err_t handle_ramrom_receive_data(struct http_state *hs, struct pbuf *p)
{
    return _handle_ramrom_receive( hs, p, OP_DATA );
}

err_t handle_ramrom_receive_or(struct http_state *hs, struct pbuf *p)
{
    return _handle_ramrom_receive( hs, p, OP_OR );
}

err_t handle_ramrom_receive_and(struct http_state *hs, struct pbuf *p)
{
    return _handle_ramrom_receive( hs, p, OP_AND );
}

void handle_ramrom_finished(struct http_state *hs, u8_t *code )
{
    if ( hs->left != 0 )
    {
        *code = HTTP_HDR_INTERNAL;
    }
    else
    {
        *code = HTTP_HDR_OK;
    }

    return;
}

err_t handle_ramrom_restore_begin(struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code)
{
    config_copy_default_memory_map( mem_map );

    return ERR_OK;
}

err_t handle_ramrom_actions_receive( struct http_state *hs, struct pbuf *p)
{
    pbuf_free(p);

    return ERR_OK;
}

void handle_ramrom_actions_finished(struct http_state *hs, u8_t *code )
{
    *code = HTTP_HDR_OK;

    return;
}

err_t _handle_ramrom_action_begin(struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code, action_t action )
{
    char *start, *count;
    int num_args = 0;

    for (int i= 0; i < hs->paramcount; ++i )
    {
        if ( strcmp( "start", hs->params[i] ) == 0 )
        {
            start = hs->param_vals[i];
            ++num_args;
        }
        else if ( strcmp( "count", hs->params[i] ) == 0 )
        {
            count = hs->param_vals[i];
            ++num_args;
        }
    }

    if ( num_args != 2 || strlen( count ) > 5 || strlen( start ) > 4 )
    {
        return http_error_response( hs, HTTP_HDR_BAD_REQUEST );
    }
    else
    {
        size_t len;
        uint32_t u_start, u_count, idx;
        char *ends, *endc;
        uint16_t *p;

        u_start = strtoul(start, &ends, 16);
        u_count = strtoul(count, &endc, 16);

        if ( *ends || *endc || !count || u_start + u_count - 1 > 0xFFFF )
        {
            return http_error_response( hs, HTTP_HDR_BAD_REQUEST );
        }

        p = &mem_map[u_start];

        for ( idx = 0; idx < u_count; ++idx, ++p )
        {
            switch ( action )
            {
                case AC_ENABLE:
                    *p &= ~MEM_ATTR_CE_MASK;
                    break;

                case AC_DISABLE:
                    *p |= MEM_ATTR_CE_MASK;
                    break;

                case AC_SETROM:
                    *p &= ~MEM_ATTR_RW_MASK;
                    break;

                case AC_SETRAM:
                    *p |= MEM_ATTR_RW_MASK;
                    break;

                default:
                    debug_printf("Should not reach here...");
            }
        }

        return ERR_OK;
    }
}

err_t handle_ramrom_enable_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code )
{
    return _handle_ramrom_action_begin( hs, uri, http_request, http_request_len, content_len,
                       code, AC_ENABLE );
}

err_t handle_ramrom_disable_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code )
{
    return _handle_ramrom_action_begin( hs, uri, http_request, http_request_len, content_len,
                       code, AC_DISABLE );
}

err_t handle_ramrom_setrom_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code )
{
    return _handle_ramrom_action_begin( hs, uri, http_request, http_request_len, content_len,
                       code, AC_SETROM );
}

err_t handle_ramrom_setram_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code )
{
    return _handle_ramrom_action_begin( hs, uri, http_request, http_request_len, content_len,
                       code, AC_SETRAM );
}

void webserver_run( void )
{
    httpd_init();
    debug_printf("HTTP server initialised.\n");

    http_set_handlers( handler_list );

    while ( true )
    {
        sleep_ms( 1000 );
    }
}

