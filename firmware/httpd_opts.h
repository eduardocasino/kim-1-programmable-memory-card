/**
 * @file
 * HTTP server options list
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

#ifndef LWIP_HDR_APPS_HTTPD_OPTS_H
#define LWIP_HDR_APPS_HTTPD_OPTS_H

#include "lwip/opt.h"
#include "lwip/prot/iana.h"

/**
 * @defgroup httpd_opts Options
 * @ingroup httpd
 * @{
 */


/** Set this to 1 to support HTTP POST */
#if !defined LWIP_HTTPD_SUPPORT_POST || defined __DOXYGEN__
#define LWIP_HTTPD_SUPPORT_POST   1
#endif

/* The maximum number of parameters that the CGI handler can be sent. */
#if !defined LWIP_HTTPD_MAX_GET_PARAMETERS || defined __DOXYGEN__
#define LWIP_HTTPD_MAX_GET_PARAMETERS 16
#endif

/** This string is passed in the HTTP header as "Server: " */
#if !defined HTTPD_SERVER_AGENT || defined __DOXYGEN__
#define HTTPD_SERVER_AGENT "lwIP/" LWIP_VERSION_STRING " (http://savannah.nongnu.org/projects/lwip)"
#endif

#if !defined HTTPD_DEBUG || defined __DOXYGEN__
#define HTTPD_DEBUG         LWIP_DBG_OFF
#endif

/** The server port for HTTPD to use */
#if !defined HTTPD_SERVER_PORT || defined __DOXYGEN__
#define HTTPD_SERVER_PORT                   LWIP_IANA_PORT_HTTP
#endif

/** The https server port for HTTPD to use */
#if !defined HTTPD_SERVER_PORT_HTTPS || defined __DOXYGEN__
#define HTTPD_SERVER_PORT_HTTPS             LWIP_IANA_PORT_HTTPS
#endif

/** Enable https support? */
#if !defined HTTPD_ENABLE_HTTPS || defined __DOXYGEN__
#define HTTPD_ENABLE_HTTPS                  0
#endif

/** Maximum retries before the connection is aborted/closed.
 * - number of times pcb->poll is called -> default is 4*500ms = 2s;
 * - reset when pcb->sent is called
 */
#if !defined HTTPD_MAX_RETRIES || defined __DOXYGEN__
#define HTTPD_MAX_RETRIES                   4
#endif

/** The poll delay is X*500ms */
#if !defined HTTPD_POLL_INTERVAL || defined __DOXYGEN__
#define HTTPD_POLL_INTERVAL                 4
#endif

/** Priority for tcp pcbs created by HTTPD (very low by default).
 *  Lower priorities get killed first when running out of memory.
 */
#if !defined HTTPD_TCP_PRIO || defined __DOXYGEN__
#define HTTPD_TCP_PRIO                      TCP_PRIO_MIN
#endif

/** Set this to 1 to support HTTP request coming in in multiple packets/pbufs */
#if !defined LWIP_HTTPD_SUPPORT_REQUESTLIST || defined __DOXYGEN__
#define LWIP_HTTPD_SUPPORT_REQUESTLIST      1
#endif

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
/** Number of rx pbufs to enqueue to parse an incoming request (up to the first
    newline) */
#if !defined LWIP_HTTPD_REQ_QUEUELEN || defined __DOXYGEN__
#define LWIP_HTTPD_REQ_QUEUELEN             5
#endif

/** Number of (TCP payload-) bytes (in pbufs) to enqueue to parse and incoming
    request (up to the first double-newline) */
#if !defined LWIP_HTTPD_REQ_BUFSIZE || defined __DOXYGEN__
#define LWIP_HTTPD_REQ_BUFSIZE              LWIP_HTTPD_MAX_REQ_LENGTH
#endif

/** Defines the maximum length of a HTTP request line (up to the first CRLF,
    copied from pbuf into this a global buffer when pbuf- or packet-queues
    are received - otherwise the input pbuf is used directly) */
#if !defined LWIP_HTTPD_MAX_REQ_LENGTH || defined __DOXYGEN__
#define LWIP_HTTPD_MAX_REQ_LENGTH           LWIP_MIN(1023, (LWIP_HTTPD_REQ_QUEUELEN * PBUF_POOL_BUFSIZE))
#endif
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */

/** Maximum length of the filename to send as response to a POST request,
 * filled in by the application when a POST is finished.
 */
#if !defined LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN || defined __DOXYGEN__
#define LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN 63
#endif

/** Set this to 1 to call tcp_abort when tcp_close fails with memory error.
 * This can be used to prevent consuming all memory in situations where the
 * HTTP server has low priority compared to other communication. */
#if !defined LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR || defined __DOXYGEN__
#define LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR  0
#endif

/* By default, the httpd is limited to send 2*pcb->mss to keep resource usage low
   when http is not an important protocol in the device. */
#if !defined HTTPD_LIMIT_SENDING_TO_2MSS || defined __DOXYGEN__
#define HTTPD_LIMIT_SENDING_TO_2MSS 1
#endif

/* Define this to a function that returns the maximum amount of data to enqueue.
   The function have this signature: u16_t fn(struct altcp_pcb* pcb);
   The best place to define this is the hooks file (@see LWIP_HOOK_FILENAME) */
#if !defined HTTPD_MAX_WRITE_LEN || defined __DOXYGEN__
#if HTTPD_LIMIT_SENDING_TO_2MSS
#define HTTPD_MAX_WRITE_LEN(pcb)    ((u16_t)(2 * altcp_mss(pcb)))
#endif
#endif

/**
 * @}
 */

#endif /* LWIP_HDR_APPS_HTTPD_OPTS_H */
