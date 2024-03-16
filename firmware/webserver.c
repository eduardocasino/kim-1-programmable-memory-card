// Credit: https://github.com/LearnEmbeddedSystems/pico-w-webserver-template
//

#include <string.h>

#include "pico/stdlib.h"

#include "lwipopts.h"
#include "httpd.h"
#include "config.h"


typedef enum { OP_REPLACE, OP_AND, OP_OR } operation_t;
/*

    POST /ramrom/set
    GET  /ramrom/get
    GET  /ramrom/restore
    GET  /ramrom/save
*/

err_t handle_ramrom_get( struct http_state *hs );
err_t handle_ramrom_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code);
err_t handle_ramrom_receive( struct http_state *hs, struct pbuf *p);
err_t handle_ramrom_receive_or( struct http_state *hs, struct pbuf *p);
err_t handle_ramrom_receive_and( struct http_state *hs, struct pbuf *p);
void  handle_ramrom_finished( struct http_state *hs, u8_t *code );

err_t handle_ramrom_restore_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code);
err_t handle_ramrom_restore_receive( struct http_state *hs, struct pbuf *p);
void  handle_ramrom_restore_finished( struct http_state *hs, u8_t *code );

static const tHandler handler_list[] = {
    { HTTP_GET,   "/ramrom/range",      handle_ramrom_get, NULL,                NULL,                        NULL },
    { HTTP_PATCH, "/ramrom/range",      NULL,              handle_ramrom_begin, handle_ramrom_receive,                  handle_ramrom_finished },
    { HTTP_PATCH, "/ramrom/range/or",   NULL,              handle_ramrom_begin, handle_ramrom_receive_or,               handle_ramrom_finished },
    { HTTP_PATCH, "/ramrom/range/and",  NULL,              handle_ramrom_begin, handle_ramrom_receive_and,              handle_ramrom_finished },
    { HTTP_PUT,   "/ramrom/restore",    NULL,              handle_ramrom_restore_begin, handle_ramrom_restore_receive,  handle_ramrom_restore_finished },
    { HTTP_NULL }
}; 

err_t _handle_ramrom_receive( struct http_state *hs, struct pbuf *p, operation_t op )
{
    err_t err = ERR_OK;
    u32_t copied, len;
    u16_t mask, data, *m;

    len = ( p->tot_len > hs->left ) ? hs->left : p->tot_len;

    if ( op == OP_REPLACE )
    {
            copied = pbuf_copy_partial( p, hs->file, len, 0 );
            hs->file += copied;

    }
    else {
        copied = 0;

        while ( copied < p->tot_len )
        {
            copied += pbuf_copy_partial( p, &mask, 2, copied );
            
            if ( op == OP_AND )
            {
                *(u16_t *)hs->file &= mask;
            }
            else
            {
                *(u16_t *)hs->file |= mask;
            }
            
            hs->file += copied;
        }
    }

    if ( copied != len )
    {
        err = ERR_BUF;
    }
    
    hs->left -= copied;

    pbuf_free(p);

    return err;
}
err_t handle_ramrom_get( struct http_state *hs )
{
    char *start, *count;
    int num_args = 0;
    uint32_t u_start, u_count;

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

    hs->buf  = (uint8_t *)&mem_map[u_start];
    hs->file = (uint8_t *)&mem_map[u_start];
    hs->left = u_count*2;

    http_send(hs->pcb, hs);

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
        printf("Param name: %s, Param value: %s\n", hs->params[i], hs->param_vals[i]);
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

        return ERR_OK;
    }
}

err_t handle_ramrom_receive(struct http_state *hs, struct pbuf *p)
{
    return _handle_ramrom_receive( hs, p, OP_REPLACE );
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

err_t handle_ramrom_restore_receive( struct http_state *hs, struct pbuf *p)
{
    pbuf_free(p);

    return ERR_OK;
}

void handle_ramrom_restore_finished(struct http_state *hs, u8_t *code )
{
    *code = HTTP_HDR_OK;

    return;
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

