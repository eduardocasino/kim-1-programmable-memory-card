/**
 * @file
 * LWIP HTTP server implementation
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
 *         Simon Goldschmidt
 *
 * Modified for basic REST support by Eduardo Casino <mail@eduardocasino.es>
 *
 */

/**
 * @defgroup httpd HTTP server
 * @ingroup apps
 *
 * This httpd supports for a
 * rudimentary server-side-include facility which will replace tags of the form
 * <!--#tag--> in any file whose extension is .shtml, .shtm or .ssi with
 * strings provided by an include handler whose pointer is provided to the
 * module via function http_set_ssi_handler().
 * Additionally, a simple common
 * gateway interface (CGI) handling mechanism has been added to allow clients
 * to hook functions to particular request URIs.
 */
#include "lwip/init.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/def.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"

#include "httpd.h"

#include <string.h> /* memset */
#include <stdlib.h> /* atoi */
#include <stdio.h>

#if LWIP_TCP && LWIP_CALLBACK_API

/** Minimum length for a valid HTTP/0.9 request: "GET /\r\n" -> 7 bytes */
#define MIN_REQ_LEN   7

/* Return values for http_send_*() */
#define HTTP_DATA_TO_SEND_FREED    3
#define HTTP_DATA_TO_SEND_BREAK    2
#define HTTP_DATA_TO_SEND_CONTINUE 1
#define HTTP_NO_DATA_TO_SEND       0

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
/** HTTP request is copied here from pbufs for simple parsing */
static char httpd_req_buf[LWIP_HTTPD_MAX_REQ_LENGTH + 1];
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */

#if LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN > LWIP_HTTPD_MAX_REQUEST_URI_LEN
#define LWIP_HTTPD_URI_BUF_LEN LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN
#endif

#define HTTP_ALLOC_HTTP_STATE() (struct http_state *)mem_malloc(sizeof(struct http_state))
#define HTTP_FREE_HTTP_STATE(x) mem_free(x)

static err_t http_close_conn(struct altcp_pcb *pcb, struct http_state *hs);
static err_t http_close_or_abort_conn(struct altcp_pcb *pcb, struct http_state *hs, u8_t abort_conn);
static err_t http_process_get_request( struct http_state *hs, const char *uri );
static err_t http_poll(void *arg, struct altcp_pcb *pcb);

static const tHandler *httpd_handlers = NULL;

static tPostBeginFn         httpd_post_begin;
static tPostReceiveDataFn   httpd_post_receive_data;
static tPostFinishedFn      httpd_post_finished;

/** Initialize a struct http_state.
 */
static void
http_state_init(struct http_state *hs)
{
  /* Initialize the structure. */
  memset(hs, 0, sizeof(struct http_state));
#if LWIP_HTTPD_DYNAMIC_HEADERS
  /* Indicate that the headers are not yet valid */
  hs->hdr_index = NUM_FILE_HDR_STRINGS;
#endif /* LWIP_HTTPD_DYNAMIC_HEADERS */
}

/** Allocate a struct http_state. */
static struct http_state *
http_state_alloc(void)
{
  struct http_state *ret = HTTP_ALLOC_HTTP_STATE();
#if LWIP_HTTPD_KILL_OLD_ON_CONNECTIONS_EXCEEDED
  if (ret == NULL) {
    http_kill_oldest_connection(0);
    ret = HTTP_ALLOC_HTTP_STATE();
  }
#endif /* LWIP_HTTPD_KILL_OLD_ON_CONNECTIONS_EXCEEDED */
  if (ret != NULL) {
    http_state_init(ret);
  }
  return ret;
}

/** Free a struct http_state.
 * Also frees the file data if dynamic.
 */
static void
http_state_eof(struct http_state *hs)
{
    if (hs->buf != NULL)
    {
        hs->buf = NULL;
    }
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
    if (hs->req) {
        pbuf_free(hs->req);
        hs->req = NULL;
    }
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
}

/** Free a struct http_state.
 * Also frees the file data if dynamic.
 */
static void
http_state_free(struct http_state *hs)
{
  if (hs != NULL) {
    http_state_eof(hs);
    HTTP_FREE_HTTP_STATE(hs);
  }
}

/** Call tcp_write() in a loop trying smaller and smaller length
 *
 * @param pcb altcp_pcb to send
 * @param ptr Data to send
 * @param length Length of data to send (in/out: on return, contains the
 *        amount of data sent)
 * @param apiflags directly passed to tcp_write
 * @return the return value of tcp_write
 */
static err_t
http_write(struct altcp_pcb *pcb, const void *ptr, u16_t *length, u8_t apiflags)
{
  u16_t len, max_len;
  err_t err;
  LWIP_ASSERT("length != NULL", length != NULL);
  len = *length;
  if (len == 0) {
    return ERR_OK;
  }
  /* We cannot send more data than space available in the send buffer. */
  max_len = altcp_sndbuf(pcb);
  if (max_len < len) {
    len = max_len;
  }
#ifdef HTTPD_MAX_WRITE_LEN
  /* Additional limitation: e.g. don't enqueue more than 2*mss at once */
  max_len = HTTPD_MAX_WRITE_LEN(pcb);
  if (len > max_len) {
    len = max_len;
  }
#endif /* HTTPD_MAX_WRITE_LEN */
  do {
    LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Trying to send %d bytes\n", len));
    err = altcp_write(pcb, ptr, len, apiflags);
    if (err == ERR_MEM) {
      if ((altcp_sndbuf(pcb) == 0) ||
          (altcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN)) {
        /* no need to try smaller sizes */
        len = 1;
      } else {
        len /= 2;
      }
      LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE,
                  ("Send failed, trying less (%d bytes)\n", len));
    }
  } while ((err == ERR_MEM) && (len > 1));

  if (err == ERR_OK) {
    LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Sent %d bytes\n", len));
    *length = len;
  } else {
    LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Send failed with err %d (\"%s\")\n", err, lwip_strerr(err)));
    *length = 0;
  }

  return err;
}

/**
 * The connection shall be actively closed (using RST to close from fault states).
 * Reset the sent- and recv-callbacks.
 *
 * @param pcb the tcp pcb to reset callbacks
 * @param hs connection state to free
 */
static err_t http_close_or_abort_conn( struct altcp_pcb *pcb, struct http_state *hs, u8_t abort_conn )
{
  err_t err;
  u8_t code;
  LWIP_DEBUGF(HTTPD_DEBUG, ("Closing connection %p\n", (void *)pcb));

  if (hs != NULL) {
    if ((hs->post_content_len_left != 0) ) {
      /* make sure the post code knows that the connection is closed */
      httpd_post_finished(hs, &code);
    }
  }

  altcp_arg(pcb, NULL);
  altcp_recv(pcb, NULL);
  altcp_err(pcb, NULL);
  altcp_poll(pcb, NULL, 0);
  altcp_sent(pcb, NULL);
  if (hs != NULL) {
    http_state_free(hs);
  }

  if (abort_conn) {
    altcp_abort(pcb);
    return ERR_OK;
  }
  err = altcp_close(pcb);
  if (err != ERR_OK) {
    LWIP_DEBUGF(HTTPD_DEBUG, ("Error %d closing %p\n", err, (void *)pcb));
    /* error closing, try again later in poll */
    altcp_poll(pcb, http_poll, HTTPD_POLL_INTERVAL);
  }
  return err;
}

/**
 * The connection shall be actively closed.
 * Reset the sent- and recv-callbacks.
 *
 * @param pcb the tcp pcb to reset callbacks
 * @param hs connection state to free
 */
static err_t
http_close_conn(struct altcp_pcb *pcb, struct http_state *hs)
{
  return http_close_or_abort_conn(pcb, hs, 0);
}

/** End of file: either close the connection (Connection: close) or
 * close the file (Connection: keep-alive)
 */
static void
http_eof(struct altcp_pcb *pcb, struct http_state *hs)
{
    http_close_conn(pcb, hs);
}

/**
 * Extract URI parameters from the parameter-part of an URI in the form
 * "test.cgi?x=y" @todo: better explanation!
 * Pointers to the parameters are stored in hs->param_vals.
 *
 * @param hs http connection state
 * @param params pointer to the NULL-terminated parameter string from the URI
 * @return number of parameters extracted
 */
static int
extract_uri_parameters(struct http_state *hs, char *params)
{
  char *pair;
  char *equals;
  int loop;

  LWIP_UNUSED_ARG(hs);

  /* If we have no parameters at all, return immediately. */
  if (!params || (params[0] == '\0')) {
    return (0);
  }

  /* Get a pointer to our first parameter */
  pair = params;

  /* Parse up to LWIP_HTTPD_MAX_GET_PARAMETERS from the passed string and ignore the
   * remainder (if any) */
  for (loop = 0; (loop < LWIP_HTTPD_MAX_GET_PARAMETERS) && pair; loop++) {

    /* Save the name of the parameter */
    hs->params[loop] = pair;

    /* Remember the start of this name=value pair */
    equals = pair;

    /* Find the start of the next name=value pair and replace the delimiter
     * with a 0 to terminate the previous pair string. */
    pair = strchr(pair, '&');
    if (pair) {
      *pair = '\0';
      pair++;
    } else {
      /* We didn't find a new parameter so find the end of the URI and
       * replace the space with a '\0' */
      pair = strchr(equals, ' ');
      if (pair) {
        *pair = '\0';
      }

      /* Revert to NULL so that we exit the loop as expected. */
      pair = NULL;
    }

    /* Now find the '=' in the previous pair, replace it with '\0' and save
     * the parameter value string. */
    equals = strchr(equals, '=');
    if (equals) {
      *equals = '\0';
      hs->param_vals[loop] = equals + 1;
    } else {
      hs->param_vals[loop] = NULL;
    }
  }

  return loop;
}

u8_t http_send_headers(struct altcp_pcb *pcb, struct http_state *hs)
{
    err_t err;
    u16_t len;
    u8_t data_to_send = HTTP_NO_DATA_TO_SEND;
    u16_t hdrlen, sendlen;

    /* How much data can we send? */
    len = altcp_sndbuf(pcb);
    sendlen = len;

    while ( len && ( hs->hdr_index < NUM_FILE_HDR_STRINGS ) && sendlen ) {
        const void *ptr;
        u16_t old_sendlen;
        u8_t apiflags;
        /* How much do we have to send from the current header? */
        hdrlen = (u16_t)strlen(hs->hdrs[hs->hdr_index]);

        /* How much of this can we send? */
        sendlen = (len < (hdrlen - hs->hdr_pos)) ? len : (hdrlen - hs->hdr_pos);

        /* Send this amount of data or as much as we can given memory
         * constraints. */
        ptr = (const void *)(hs->hdrs[hs->hdr_index] + hs->hdr_pos);
        old_sendlen = sendlen;
        apiflags = 0;
    
        if (hs->hdr_index == HDR_STRINGS_IDX_CONTENT_LEN_NR) {
            /* content-length is always volatile */
            apiflags |= TCP_WRITE_FLAG_COPY;
        }
        if (hs->hdr_index < NUM_FILE_HDR_STRINGS - 1) {
            apiflags |= TCP_WRITE_FLAG_MORE;
        }
        err = http_write(pcb, ptr, &sendlen, apiflags);
        
        if (err != ERR_OK) {
            return err;
        }

        /* Fix up the header position for the next time round. */
        hs->hdr_pos += sendlen;
        len -= sendlen;

        /* Have we finished sending this string? */
        if (hs->hdr_pos == hdrlen) {
            /* Yes - move on to the next one */
            hs->hdr_index++;
            /* skip headers that are NULL (not all headers are required) */
            while ((hs->hdr_index < NUM_FILE_HDR_STRINGS) &&
                    (hs->hdrs[hs->hdr_index] == NULL)) {
                hs->hdr_index++;
            }
            hs->hdr_pos = 0;
        }
    }

    return ERR_OK;
}


/** Sub-function of http_send(): This is the normal send-routine for non-ssi files
 *
 * @returns: - 1: data has been written (so call tcp_ouput)
 *           - 0: no data has been written (no need to call tcp_output)
 */
static u8_t http_send_data( struct altcp_pcb *pcb, struct http_state *hs )
{
  err_t err;
  u16_t len;
  u8_t data_to_send = 0;

  /* We are not processing an SHTML file so no tag checking is necessary.
   * Just send the data as we received it from the file. */
  len = (u16_t)LWIP_MIN(hs->left, 0xffff);

  err = http_write(pcb, hs->file, &len, 0);
  if (err == ERR_OK) {
    data_to_send = 1;
    hs->file += len;
    hs->left -= len;
  }

  return data_to_send;
}

/**
 * Try to send more data on this pcb.
 *
 * @param pcb the pcb to send data
 * @param hs connection state
 */
u8_t http_send(struct altcp_pcb *pcb, struct http_state *hs)
{
  u8_t data_to_send = HTTP_NO_DATA_TO_SEND;

  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_send: pcb=%p hs=%p left=%d\n", (void *)pcb,
              (void *)hs, hs != NULL ? (int)hs->left : 0));

  /* If we were passed a NULL state structure pointer, ignore the call. */
  if (hs == NULL) {
    return 0;
  }

  /* Have we run out of file data to send? */
  if (hs->left == 0) {
    return 0;
  }

  data_to_send = http_send_data(pcb, hs);

  if ( (hs->left == 0) ) {
    /* We reached the end of the buffer so this request is done.
     * This adds the FIN flag right into the last data segment. */
    LWIP_DEBUGF(HTTPD_DEBUG, ("End of file.\n"));
    http_eof(pcb, hs);
    return 0;
  }
  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("send_data end.\n"));
  return data_to_send;
}

err_t http_error_response( struct http_state *hs, u8_t code )
{
    size_t len;

    LWIP_ASSERT( "http_error_response: wrong code", code < HTTP_HDR_CONTENT_LENGTH );
    
    sprintf( hs->hdr_content_len, "%u", 0 );
    len = strlen( hs->hdr_content_len );
    SMEMCPY(&hs->hdr_content_len[len], CRLF, 3);
    
    hs->hdrs[HDR_STRINGS_IDX_HTTP_STATUS] = g_psHTTPHeaderStrings[code];
    hs->hdrs[HDR_STRINGS_IDX_SERVER_NAME] = g_psHTTPHeaderStrings[HTTP_HDR_SERVER];
    hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN] = g_psHTTPHeaderStrings[HTTP_HDR_CONTENT_LENGTH];
    hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_NR] = hs->hdr_content_len;
    hs->hdrs[HDR_STRINGS_IDX_CONTENT_TYPE] = "\r\n";

    http_send_headers(hs->pcb, hs);

    return ERR_OK;
}

static err_t http_handle_post_finished(struct http_state *hs)
{
    u8_t code;

    /* application error or POST finished */
    httpd_post_finished(hs, &code);

    return http_error_response( hs, code );
}

/** Pass received POST body data to the application and correctly handle
 * returning a response document or closing the connection.
 * ATTENTION: The application is responsible for the pbuf now, so don't free it!
 *
 * @param hs http connection state
 * @param p pbuf to pass to the application
 * @return ERR_OK if passed successfully, another err_t if the response file
 *         hasn't been found (after POST finished)
 */
static err_t
http_post_rxpbuf(struct http_state *hs, struct pbuf *p)
{
  err_t err;

  if (p != NULL) {
    /* adjust remaining Content-Length */
    if (hs->post_content_len_left < p->tot_len) {
      hs->post_content_len_left = 0;
    } else {
      hs->post_content_len_left -= p->tot_len;
    }
  }

  if (p != NULL) {
    err = httpd_post_receive_data(hs, p);
  } else {
    err = ERR_OK;
  }

  if (err != ERR_OK) {
    /* Ignore remaining content in case of application error */
    hs->post_content_len_left = 0;
  }
  if (hs->post_content_len_left == 0) {
    /* application error or POST finished */
    return http_handle_post_finished(hs);
  }

  return ERR_OK;
}

/** Handle a post request. Called from http_parse_request when method 'POST'
 * is found.
 *
 * @param p The input pbuf (containing the POST header and body).
 * @param hs The http connection state.
 * @param data HTTP request (header and part of body) from input pbuf(s).
 * @param data_len Size of 'data'.
 * @param uri The HTTP URI parsed from input pbuf(s).
 * @param uri_end Pointer to the end of 'uri' (here, the rest of the HTTP
 *                header starts).
 * @return ERR_OK: POST correctly parsed and accepted by the application.
 *         ERR_INPROGRESS: POST not completely parsed (no error yet)
 *         another err_t: Error parsing POST or denied by the application
 */
static err_t
http_post_request(struct pbuf *inp, struct http_state *hs,
                  char *data, u16_t data_len, char *uri, char *uri_end)
{
  err_t err;
  /* search for end-of-header (first double-CRLF) */
  char *crlfcrlf = lwip_strnstr(uri_end + 1, CRLF CRLF, data_len - (uri_end + 1 - data));

  if (crlfcrlf != NULL) {
    /* search for "Content-Length: " */
#define HTTP_HDR_CONTENT_LEN                "Content-Length: "
#define HTTP_HDR_CONTENT_LEN_LEN            16
#define HTTP_HDR_CONTENT_LEN_DIGIT_MAX_LEN  10
    char *scontent_len = lwip_strnstr(uri_end + 1, HTTP_HDR_CONTENT_LEN, crlfcrlf - (uri_end + 1));
    if (scontent_len != NULL) {
      char *scontent_len_end = lwip_strnstr(scontent_len + HTTP_HDR_CONTENT_LEN_LEN, CRLF, HTTP_HDR_CONTENT_LEN_DIGIT_MAX_LEN);
      if (scontent_len_end != NULL) {
        int content_len;
        char *content_len_num = scontent_len + HTTP_HDR_CONTENT_LEN_LEN;
        content_len = atoi(content_len_num);
        if (content_len == 0) {
          /* if atoi returns 0 on error, fix this */
          if ((content_len_num[0] != '0') || (content_len_num[1] != '\r')) {
            content_len = -1;
          }
        }
        if (content_len >= 0) {
          /* adjust length of HTTP header passed to application */
          const char *hdr_start_after_uri = uri_end + 1;
          u16_t hdr_len = (u16_t)LWIP_MIN(data_len, crlfcrlf + 4 - data);
          u16_t hdr_data_len = (u16_t)LWIP_MIN(data_len, crlfcrlf + 4 - hdr_start_after_uri);
          u8_t code;
          /* trim http header */
          *crlfcrlf = 0;
          err = httpd_post_begin(hs, uri, hdr_start_after_uri, hdr_data_len, content_len, &code);
          if (err == ERR_OK) {
            /* try to pass in data of the first pbuf(s) */
            struct pbuf *q = inp;
            u16_t start_offset = hdr_len;

            /* set the Content-Length to be received for this POST */
            hs->post_content_len_left = (u32_t)content_len;

            /* get to the pbuf where the body starts */
            while ((q != NULL) && (q->len <= start_offset)) {
              start_offset -= q->len;
              q = q->next;
            }
            if (q != NULL) {
              /* hide the remaining HTTP header */
              pbuf_remove_header(q, start_offset);

              pbuf_ref(q);
              return http_post_rxpbuf(hs, q);
            } else if (hs->post_content_len_left == 0) {
              q = pbuf_alloc(PBUF_RAW, 0, PBUF_REF);
              return http_post_rxpbuf(hs, q);
            } else {
              return ERR_OK;
            }
          } else {
            /* return error code passed from application */
            return http_error_response( hs, code );          }
        } else {
          LWIP_DEBUGF(HTTPD_DEBUG, ("POST received invalid Content-Length: %s\n",
                                    content_len_num));
          return ERR_ARG;
        }
      }
    }
    /* If we come here, headers are fully received (double-crlf), but Content-Length
       was not included. Since this is currently the only supported method, we have
       to fail in this case! */
    LWIP_DEBUGF(HTTPD_DEBUG, ("Error when parsing Content-Length\n"));
    return ERR_ARG;
  }
  /* if we come here, the POST is incomplete */
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
  return ERR_INPROGRESS;
#else /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
  return ERR_ARG;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
}

static err_t
http_process_post_request(struct pbuf *inp, struct http_state *hs,
                  char *data, u16_t data_len, char *uri, char *uri_end)
{
    char *params = NULL;
    int uri_exists = 0;

    /* Have we been asked for the default file (in root or a directory) ? */
    if ( ( uri[0] == '/' ) &&  ( uri[1] == 0 ) )
    {
        /* Not supported */
        return http_error_response( hs, HTTP_HDR_NOT_FOUND );
    }

    /* No - we've been asked for a specific resource. */
    /* First, isolate the base URI (without any parameters) */
    params = ( char * ) strchr( uri, '?' );
    if ( params != NULL )
    {
        /* URI contains parameters. NULL-terminate the base URI */
        *params = '\0';
        params++;
    }

    for ( int i = 0; httpd_handlers && httpd_handlers[i].method != HTTP_NULL; ++i )
    {
        if ( strcmp( uri, httpd_handlers[i].resource ) == 0 )
        {
            /* Found a handler for this URI */
            ++uri_exists;

            if ( httpd_handlers[i].method == hs->method )
            {
                hs->paramcount = extract_uri_parameters(hs, params);

                httpd_post_begin        = httpd_handlers[i].post_begin;
                httpd_post_receive_data = httpd_handlers[i].post_receive_data;
                httpd_post_finished     = httpd_handlers[i].post_finished;

                return http_post_request( inp, hs, data, data_len, uri, uri_end );
            }    
        }
    }

    if ( uri_exists )
    {
        return http_error_response( hs, HTTP_HDR_NOT_IMPL );
    }

    return http_error_response( hs, HTTP_HDR_NOT_FOUND );

}


/** Try to find the file specified by uri and, if found, initialize hs
 * accordingly.
 *
 * @param hs the connection state
 * @param uri the HTTP header URI
 * @return ERR_OK if file was found and hs has been initialized correctly
 *         another err_t otherwise
 */
static err_t http_process_get_request( struct http_state *hs, const char *uri )
{
    char *params = NULL;
    int uri_exists = 0;

    /* Have we been asked for the default file (in root or a directory) ? */
    if ( ( uri[0] == '/' ) &&  ( uri[1] == 0 ) )
    {
        /* Not supported */
        return http_error_response( hs, HTTP_HDR_NOT_FOUND );
    }

    /* No - we've been asked for a specific resource. */
    /* First, isolate the base URI (without any parameters) */
    params = ( char * ) strchr( uri, '?' );
    if ( params != NULL )
    {
        /* URI contains parameters. NULL-terminate the base URI */
        *params = '\0';
        params++;
    }

    for ( int i = 0; httpd_handlers && httpd_handlers[i].method != HTTP_NULL; ++i )
    {
        if ( strcmp( uri, httpd_handlers[i].resource ) == 0 )
        {
            /* Found a handler for this URI */
            ++uri_exists;

            if ( httpd_handlers[i].method == HTTP_GET )
            { 
                hs->paramcount = extract_uri_parameters(hs, params);
                return httpd_handlers[i].handler( hs );
            }    
        }
    }

    if ( uri_exists )
    {
        return http_error_response( hs, HTTP_HDR_NOT_IMPL );
    }

    return http_error_response( hs, HTTP_HDR_NOT_FOUND );
}

/**
 * When data has been received in the correct state, try to parse it
 * as a HTTP request.
 *
 * @param inp the received pbuf
 * @param hs the connection state
 * @param pcb the altcp_pcb which received this packet
 * @return ERR_OK if request was OK and hs has been initialized correctly
 *         ERR_INPROGRESS if request was OK so far but not fully received
 *         another err_t otherwise
 */
static err_t http_parse_request( struct pbuf *inp, struct http_state *hs, struct altcp_pcb *pcb )
{
    char * data;
    char * crlf;
    u16_t data_len;
    struct pbuf * p = inp;
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
    u16_t clen;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
    err_t err;

    LWIP_UNUSED_ARG( pcb ); /* only used for post */
    LWIP_ASSERT( "p != NULL", p != NULL );
    LWIP_ASSERT( "hs != NULL", hs != NULL );


    if ( ( hs->buf != NULL ) )
    {
        LWIP_DEBUGF( HTTPD_DEBUG, ( "Received data while sending data\n" ) );
        /* already sending data */
        return ERR_USE;
    }

#if LWIP_HTTPD_SUPPORT_REQUESTLIST

    LWIP_DEBUGF( HTTPD_DEBUG, ( "Received %"U16_F" bytes\n", p->tot_len ) );

    /* first check allowed characters in this pbuf? */

    /* enqueue the pbuf */
    if ( hs->req == NULL )
    {
        LWIP_DEBUGF( HTTPD_DEBUG, ( "First pbuf\n" ) );
        hs->req = p;
    }
    else
    {
        LWIP_DEBUGF( HTTPD_DEBUG, ( "pbuf enqueued\n" ) );
        pbuf_cat( hs->req, p );
    }
    /* increase pbuf ref counter as it is freed when we return but we want to
       keep it on the req list */
    pbuf_ref( p );

    if ( hs->req->next != NULL )
    {
        data_len = LWIP_MIN( hs->req->tot_len, LWIP_HTTPD_MAX_REQ_LENGTH );
        pbuf_copy_partial( hs->req, httpd_req_buf, data_len, 0 );
        data = httpd_req_buf;
    }
    else
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
    {
        data = (char * ) p->payload;
        data_len = p->len;
        if ( p->len != p->tot_len )
        {
            LWIP_DEBUGF( HTTPD_DEBUG, ( "Warning: incomplete header due to chained pbufs\n" ) );
        }
    }

    /* received enough data for minimal request? */
    if ( data_len >= MIN_REQ_LEN )
    {
        /* wait for CRLF before parsing anything */
        crlf = lwip_strnstr( data, CRLF, data_len );
        if (crlf != NULL)
        {
            char * sp1, * sp2;
            u16_t left_len, uri_len;
            LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "CRLF received, parsing request\n" ) );
            /* parse method */
            if ( !strncmp( data, "GET ", 4 ) )
            {
                sp1 = data + 3;
                /* received GET request */
            LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "Received GET request\"\n" ) );
            }
            else if ( !strncmp( data, "PUT ", 4 ) )
            {
                /* store request type */
                hs->method = HTTP_PUT;
                sp1 = data + 3;
                /* received PUT request */
                LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "Received PUT request\n" ) );
            }
            else if ( !strncmp( data, "POST ", 5 ) )
            {
                /* store request type */
                hs->method = HTTP_POST;
                sp1 = data + 4;
                /* received POST request */
                LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "Received POST request\n" ) );
            }
            else if ( !strncmp( data, "PATCH ", 6 ) )
            {
                /* store request type */
                hs->method = HTTP_PATCH;
                sp1 = data + 5;
                /* received POST request */
                LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "Received PATCH request\n" ) );
            }
            else
            {
                /* null-terminate the METHOD (pbuf is freed anyway wen returning) */
                data[4] = 0;
                /* unsupported method! */
                LWIP_DEBUGF( HTTPD_DEBUG, ( "Unsupported request method (not implemented): \"%s\"\n",
                                  data ) );
                return http_error_response( hs, HTTP_HDR_NOT_IMPL );
            }
            /* if we come here, method is OK, parse URI */
            left_len = ( u16_t ) ( data_len - ( ( sp1 + 1 ) - data ) );
            sp2 = lwip_strnstr( sp1 + 1, " ", left_len );
            uri_len = (u16_t)(sp2 - (sp1 + 1));
            if ( ( sp2 != NULL ) && ( sp2 > sp1 ) )
            {
                /* wait for CRLFCRLF (indicating end of HTTP headers) before parsing anything */
                if ( lwip_strnstr( data, CRLF CRLF, data_len ) != NULL )
                {
                    char *uri = sp1 + 1;
                    /* null-terminate the METHOD (pbuf is freed anyway when returning) */
                    *sp1 = 0;
                    uri[uri_len] = 0;
                    LWIP_DEBUGF( HTTPD_DEBUG, ("Received \"%s\" request for URI: \"%s\"\n",
                                    data, uri ) );
                    if ( hs->method == HTTP_POST || hs->method == HTTP_PUT || hs->method == HTTP_PATCH )
                    {
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
                        struct pbuf *q = hs->req;
#else /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
                        struct pbuf *q = inp;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
                        err = http_process_post_request( q, hs, data, data_len, uri, sp2 );
                        if ( err != ERR_OK )
                        {
                            /* restore header for next try */
                            *sp1 = ' ';
                            *sp2 = ' ';
                            uri[uri_len] = ' ';
                        }
                        if ( err == ERR_ARG )
                        {
                            goto badrequest;
                        }
                        return err;
                    }
                    else
                    {
                        return http_process_get_request(hs, uri);
                    }
                }
            }
            else
            {
                LWIP_DEBUGF( HTTPD_DEBUG, ( "invalid URI\n" ) );
            }
        }
    }

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
    clen = pbuf_clen( hs->req );
    if ( ( hs->req->tot_len <= LWIP_HTTPD_REQ_BUFSIZE ) &&
         ( clen <= LWIP_HTTPD_REQ_QUEUELEN ) )
    {
        /* request not fully received (too short or CRLF is missing) */
        return ERR_INPROGRESS;
    }
    else
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
    {
badrequest:
        LWIP_DEBUGF( HTTPD_DEBUG, ( "bad request\n" ) );
        /* could not parse request */
        return http_error_response( hs, HTTP_HDR_BAD_REQUEST );
    }
}

/**
 * The pcb had an error and is already deallocated.
 * The argument might still be valid (if != NULL).
 */
static void http_err( void *arg, err_t err )
{
    struct http_state *hs = ( struct http_state * ) arg;
    LWIP_UNUSED_ARG( err );

    LWIP_DEBUGF( HTTPD_DEBUG, ( "http_err: %s", lwip_strerr( err ) ) );

    if ( hs != NULL )
    {
        http_state_free( hs );
    }
}

/**
 * Data has been sent and acknowledged by the remote host.
 * This means that more data can be sent.
 */
static err_t http_sent( void *arg, struct altcp_pcb *pcb, u16_t len )
{
    struct http_state *hs = ( struct http_state * ) arg;

    LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "http_sent %p\n", ( void * ) pcb ) );

    LWIP_UNUSED_ARG( len );

    if ( hs == NULL )
    {
        return ERR_OK;
    }

    hs->retries = 0;

    http_send( pcb, hs );

    return ERR_OK;
}

/**
 * The poll function is called every 2nd second.
 * If there has been no data sent (which resets the retries) in 8 seconds, close.
 * If the last portion of a file has not been sent in 2 seconds, close.
 *
 * This could be increased, but we don't want to waste resources for bad connections.
 */
static err_t http_poll( void *arg, struct altcp_pcb *pcb )
{
    struct http_state *hs = ( struct http_state * ) arg;
    LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "http_poll: pcb=%p hs=%p pcb_state=%s\n",
                    ( void * ) pcb, ( void * ) hs, tcp_debug_state_str( altcp_dbg_get_tcp_state( pcb ) ) ) );

    if ( hs == NULL )
    {
        err_t closed;
        /* arg is null, close. */
        LWIP_DEBUGF( HTTPD_DEBUG, ( "http_poll: arg is NULL, close\n" ) );
        closed = http_close_conn( pcb, NULL );
        LWIP_UNUSED_ARG( closed );
#if LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR
        if ( closed == ERR_MEM )
        {
            altcp_abort(pcb);
            return ERR_ABRT;
        }
#endif /* LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR */
        return ERR_OK;
    }
    else
    {
        hs->retries++;
        if ( hs->retries == HTTPD_MAX_RETRIES )
        {
            LWIP_DEBUGF( HTTPD_DEBUG, ( "http_poll: too many retries, close\n" ) );
            http_close_conn( pcb, hs );
            return ERR_OK;
        }

        /* If this connection has a file open, try to send some more data. If
        * it has not yet received a GET request, don't do this since it will
        * cause the connection to close immediately. */
        if ( hs->buf )
        {
            LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "http_poll: try to send more data\n" ) );
            if ( http_send( pcb, hs ) )
            {
                /* If we wrote anything to be sent, go ahead and send it now. */
                LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "tcp_output\n" ) );
                altcp_output( pcb );
            }
        }
    }

    return ERR_OK;
}

/**
 * Data has been received on this pcb.
 * For HTTP 1.0, this should normally only happen once (if the request fits in one packet).
 */
static err_t http_recv( void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err )
{
    struct http_state *hs = ( struct http_state * ) arg;
    LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "http_recv: pcb=%p pbuf=%p err=%s\n", ( void * ) pcb,
                ( void * ) p, lwip_strerr( err ) ) );

    if ( ( err != ERR_OK ) || ( p == NULL ) || ( hs == NULL ) )
    {
        /* error or closed by other side? */
        if ( p != NULL )
        {
            /* Inform TCP that we have taken the data. */
            altcp_recved( pcb, p->tot_len );
            pbuf_free( p );
        }
        if ( hs == NULL )
        {
            /* this should not happen, only to be robust */
            LWIP_DEBUGF( HTTPD_DEBUG, ( "Error, http_recv: hs is NULL, close\n" ) );
        }
        http_close_conn( pcb, hs );
        return ERR_OK;
    }

    /* Inform TCP that we have taken the data. */
    altcp_recved( pcb, p->tot_len );

    if ( hs->post_content_len_left > 0 )
    {
        /* reset idle counter when POST/PUT/PATCH data is received */
        hs->retries = 0;
        /* this is data for a POST/PUT/PATCH, pass the complete pbuf to the application */
        http_post_rxpbuf( hs, p );
        /* pbuf is passed to the application, don't free it! */
        if ( hs->post_content_len_left == 0 )
        {
            /* all data received, send response or close connection */
            http_send( pcb, hs );
        }
        return ERR_OK;
    }
    else
    {
        if ( hs->buf == NULL )
        {
            err_t parsed = http_parse_request( p, hs, pcb );
            LWIP_ASSERT( "http_parse_request: unexpected return value", parsed == ERR_OK
                  || parsed == ERR_INPROGRESS || parsed == ERR_ARG || parsed == ERR_USE );
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
            if ( parsed != ERR_INPROGRESS )
            {
                /* request fully parsed or error */
                if ( hs->req != NULL )
                {
                    pbuf_free( hs->req );
                    hs->req = NULL;
                }
            }
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
            pbuf_free( p );
            if ( parsed == ERR_OK )
            {
                if ( hs->post_content_len_left == 0 )
                {
                    LWIP_DEBUGF( HTTPD_DEBUG | LWIP_DBG_TRACE, ( "http_recv: data %p len %"S32_F"\n", ( const void * ) hs->file, hs->left ) );
                    http_send( pcb, hs );
                }
            }
            else if ( parsed == ERR_ARG )
            {
                /* @todo: close on ERR_USE? */
                http_close_conn( pcb, hs );
            }
        }
        else
        {
            LWIP_DEBUGF( HTTPD_DEBUG, ( "http_recv: already sending data\n" ) );
            /* already sending but still receiving data, we might want to RST here? */
            pbuf_free( p );
        }
    }
    return ERR_OK;
}

/**
 * A new incoming connection has been accepted.
 */
static err_t http_accept( void *arg, struct altcp_pcb *pcb, err_t err )
{
    struct http_state *hs;
    LWIP_UNUSED_ARG( err );
    LWIP_UNUSED_ARG( arg );
    LWIP_DEBUGF( HTTPD_DEBUG, ( "http_accept %p / %p\n", (void *)pcb, arg ));

    if (( err != ERR_OK ) || ( pcb == NULL ) )
    {
        return ERR_VAL;
    }

    /* Set priority */
    altcp_setprio( pcb, HTTPD_TCP_PRIO );

    /* Allocate memory for the structure that holds the state of the
       connection - initialized by that function. */
    hs = http_state_alloc();
    if ( hs == NULL )
    {
        LWIP_DEBUGF( HTTPD_DEBUG, ( "http_accept: Out of memory, RST\n" ) );
        return ERR_MEM;
    }
    hs->pcb = pcb;

    /* Tell TCP that this is the structure we wish to be passed for our
       callbacks. */
    altcp_arg( pcb, hs );

    /* Set up the various callback functions */
    altcp_recv( pcb, http_recv );
    altcp_err ( pcb, http_err );
    altcp_poll( pcb, http_poll, HTTPD_POLL_INTERVAL );
    altcp_sent( pcb, http_sent );

    return ERR_OK;
}

static void httpd_init_pcb( struct altcp_pcb *pcb, u16_t port )
{
    err_t err;

    if ( pcb )
    {
        altcp_setprio( pcb, HTTPD_TCP_PRIO );
 
        /* set SOF_REUSEADDR here to explicitly bind httpd to multiple interfaces */
        err = altcp_bind( pcb, IP_ANY_TYPE, port );
        LWIP_UNUSED_ARG( err ); /* in case of LWIP_NOASSERT */
        LWIP_ASSERT( "httpd_init: tcp_bind failed", err == ERR_OK );
        pcb = altcp_listen( pcb );
        LWIP_ASSERT( "httpd_init: tcp_listen failed", pcb != NULL );
        altcp_accept( pcb, http_accept );
    }
}

/**
 * @ingroup httpd
 * Initialize the httpd: set up a listening PCB and bind it to the defined port
 */
void httpd_init( void )
{
    struct altcp_pcb *pcb;

    LWIP_DEBUGF(HTTPD_DEBUG, ("httpd_init\n"));

    pcb = altcp_tcp_new_ip_type( IPADDR_TYPE_ANY );
    LWIP_ASSERT("httpd_init: tcp_new failed", pcb != NULL);
    httpd_init_pcb( pcb, HTTPD_SERVER_PORT );
}

/**
 * @ingroup httpd
 * Set an array of resource handler functions
 *
 * @param handlers an array of resource handler functions
 */
void http_set_handlers( const tHandler *handlers )
{
  LWIP_ASSERT( "no handlers given", handlers != NULL );

  httpd_handlers = handlers;  
}

#endif /* LWIP_TCP && LWIP_CALLBACK_API */