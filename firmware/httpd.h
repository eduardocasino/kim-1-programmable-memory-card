/**
 * @file
 * HTTP server
 */

/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 * This version of the file has been modified by Texas Instruments to offer
 * simple server-side-include (SSI) and Common Gateway Interface (CGI)
 * capability.
 */

#ifndef LWIP_HDR_APPS_HTTPD_H
#define LWIP_HDR_APPS_HTTPD_H

#include "lwip/init.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/altcp.h"

#include "httpd_opts.h"
#include "httpd_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_FILE_HDR_STRINGS 5
#define HDR_STRINGS_IDX_HTTP_STATUS           0 /* e.g. "HTTP/1.0 200 OK\r\n" */
#define HDR_STRINGS_IDX_SERVER_NAME           1 /* e.g. "Server: "HTTPD_SERVER_AGENT"\r\n" */
#define HDR_STRINGS_IDX_CONTENT_LEN           2 /* e.g. "Content-Length: xy\r\n" */
#define HDR_STRINGS_IDX_CONTENT_LEN_NR        3 /* the byte count, when content-length is used */
#define HDR_STRINGS_IDX_CONTENT_TYPE          4 /* the content type (or default answer content type including default document) */

/* The dynamically generated Content-Length buffer needs space for CRLF + NULL */
#define LWIP_HTTPD_MAX_CONTENT_LEN_OFFSET 3
#ifndef LWIP_HTTPD_MAX_CONTENT_LEN_SIZE
/* The dynamically generated Content-Length buffer shall be able to work with
   ~953 MB (9 digits) */
#define LWIP_HTTPD_MAX_CONTENT_LEN_SIZE   (9 + LWIP_HTTPD_MAX_CONTENT_LEN_OFFSET)
#endif

#define CRLF "\r\n"

typedef enum { HTTP_NULL = 0, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_PATCH } tHTTPmethod;

struct http_state {
    tHTTPmethod     method;
    u8_t            *buf;
    char            *file;          /* Pointer to first unsent byte in buf. */
    u32_t           left;           /* Number of unsent bytes in buf. */

    struct altcp_pcb *pcb;

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
  struct pbuf *req;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */

    u8_t retries;
    u16_t paramcount;
    char *params[LWIP_HTTPD_MAX_GET_PARAMETERS];        /* Params extracted from the request URI */
    char *param_vals[LWIP_HTTPD_MAX_GET_PARAMETERS];    /* Values for each extracted param */

    const char *hdrs[NUM_FILE_HDR_STRINGS]; /* HTTP headers to be sent. */
    char hdr_content_len[LWIP_HTTPD_MAX_CONTENT_LEN_SIZE];
    u16_t hdr_pos;      /* The position of the first unsent header byte in the
                           current string */
    u16_t hdr_index;    /* The index of the hdr string currently being sent. */

    u32_t post_content_len_left;
};

/* These functions must be implemented by the application */

typedef err_t (*tHandlerFn)( struct http_state *hs );

/**
 * @ingroup httpd
 * Called when a POST request has been received. The application can decide
 * whether to accept it or not.
 *
 * @param connection Unique connection identifier, valid until httpd_post_end
 *        is called.
 * @param uri The HTTP header URI receiving the POST request.
 * @param http_request The raw HTTP request (the first packet, normally).
 * @param http_request_len Size of 'http_request'.
 * @param content_len Content-Length from HTTP header.
 * @param code Buffer to hold the http code for the reply, to be filled when denying the
 *        request
 * @return ERR_OK: Accept the POST request, data may be passed in
 *         another err_t: Deny the POST request, send back 'bad request'.
 */
typedef err_t (*tPostBeginFn)(struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code);

/**
 * @ingroup httpd
 * Called for each pbuf of data that has been received for a POST.
 * ATTENTION: The application is responsible for freeing the pbufs passed in!
 *
 * @param connection Unique connection identifier.
 * @param p Received data.
 * @return ERR_OK: Data accepted.
 *         another err_t: Data denied, http_post_get_response_uri will be called.
 */
typedef err_t (*tPostReceiveDataFn)(struct http_state *hs, struct pbuf *p);

/**
 * @ingroup httpd
 * Called when all data is received or when the connection is closed.
 * The application must return the HTTP code to send in response
 * to this POST request.
 *
 * @param connection Unique connection identifier.
 * @param code Pointer to the address to hold the response code
 */
typedef void (*tPostFinishedFn)(struct http_state *hs, u8_t *code );
typedef struct
{
    tHTTPmethod         method;
    const char          *resource;
    tHandlerFn          handler;
    tPostBeginFn        post_begin;
    tPostReceiveDataFn  post_receive_data;
    tPostFinishedFn     post_finished;
} tHandler;

/** Set the array of resource handlers. */
void http_set_handlers( const tHandler *pHandlers );
u8_t http_send_headers(struct altcp_pcb *pcb, struct http_state *hs);
u8_t http_send(struct altcp_pcb *pcb, struct http_state *hs);

err_t http_error_response( struct http_state *hs, u8_t code );

void httpd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_HDR_APPS_HTTPD_H */
