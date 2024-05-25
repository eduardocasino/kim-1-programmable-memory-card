/*
 * HTTP support functions for the KIM-1 Programmable Memory Board
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
 * 
 * httpd_extract_uri_parameters() and httpd_extract_headers() are
 * derived from code in the lwIP TCP/IP stack, original copyright below
 */

#ifndef HTTPD_H
#define HTTPD_H

#include <stdint.h>
#include "picowi.h"

#define HTTPD_MAX_GET_PARAMETERS 16
#define HTTPD_MAX_HEADERS 16

typedef struct http_request_s {
    char *headp;                                /* Pointer to first header */
    uint8_t *bodyp;                             /* Pointer to the request body */
    uint16_t paramcount;
    char *params[HTTPD_MAX_GET_PARAMETERS];     /* Params extracted from the request URI */
    char *param_vals[HTTPD_MAX_GET_PARAMETERS]; /* Values for each extracted param */

    uint16_t headercount;
    char *headers[HTTPD_MAX_HEADERS];           /* Params extracted from the request */
    char *header_vals[HTTPD_MAX_HEADERS];       /* Values for each extracted header */

    uint32_t hlen;                              /* Response headers length */

    int seq;                                    /* Sequence num that uniquely identifies request */
    int content_len;                            /* Expected content length */
    int recvd;                                  /* Cumulative bytes received */
    uint8_t *buf;                               /* Destination buffer */
} http_request_t;

int httpd_init_http_request( http_request_t *http_req, NET_SOCKET *ts, char *req, int len );
int httpd_extract_uri_parameters( http_request_t *http_req, char *req );
int httpd_extract_headers( http_request_t *http_req, char *hd );

#endif /* HTTPD_H */
