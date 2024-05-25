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

#include <string.h> /* strchr */
#include <stdio.h>
#include <stdlib.h>
#include "picowi.h"
#include "httpd.h"

int httpd_init_http_request( http_request_t *http_req, NET_SOCKET *ts, char *req, int len )
{
  http_req->seq = ts->seq;
  http_req->recvd = 0;

  /* Find beginning of headers */
  http_req->headp = strnstr( req, "\r\n", len );
  if ( NULL == http_req->headp )
  {
    return -1;
  }
  http_req->headp += 2;

  /* Find beginning of data */
  http_req->bodyp = strnstr( http_req->headp, "\r\n\r\n", len );
  if ( NULL == http_req->bodyp )
  {
    return -1;
  }
  *http_req->bodyp = '\0';
  http_req->bodyp += 4;

  httpd_extract_uri_parameters( http_req, req );
  httpd_extract_headers( http_req, http_req->headp );

  for ( int i= 0; i < http_req->headercount; ++i )
  {
    if ( strcmp( "Content-Length", http_req->headers[i] ) == 0 )
    {
      http_req->content_len = http_req->header_vals[i] ? atoi( http_req->header_vals[i] ) : 0;
    }
  }

  return 0;
}

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
 */

/**
 * Extract URI parameters from the parameter-part of an URI in the form
 * "test.cgi?x=y" @todo: better explanation!
 * Pointers to the parameters are stored in http_req->param_vals.
 *
 * @param http_req pointer to the http request structure
 * @param req pointer to the NULL-terminated request string
 * @return number of parameters extracted
 */
int httpd_extract_uri_parameters(http_request_t *http_req, char *req)
{
  char *params;
  char *pair;
  char *equals;
  int loop;

  params = strchr( req, '?' );
  if ( params ) {
    params++;
  } else {
    /* If we have no parameters at all, return immediately. */
    return (0);
  }
  
  /* Get a pointer to our first parameter */
  pair = params;

  /* Parse up to HTTPD_MAX_GET_PARAMETERS from the passed string and ignore the
   * remainder (if any) */
  for (loop = 0; (loop < HTTPD_MAX_GET_PARAMETERS) && pair; loop++) {

    /* Save the name of the parameter */
    http_req->params[loop] = pair;

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
      http_req->param_vals[loop] = equals + 1;
    } else {
      http_req->param_vals[loop] = NULL;
    }
  }

  http_req->paramcount = loop;

  return loop;
}

/**
 * Extract headers from the HTTP request
 *
 * @param http_req pointer to the http request structure
 * @param hd pointer to the beginning of the headers
 * @return number of headers extracted
 */
int httpd_extract_headers( http_request_t *http_req, char *hd )
{
  int loop;
  char *pair;
  char *equals;

  /* Pointer to the first header */
  pair = hd;

/* Parse up to HTTPD_MAX_HEADERS from the passed string and ignore the
   * remainder (if any) */
  for (loop = 0; (loop < HTTPD_MAX_HEADERS) && pair; loop++) {

    /* Save the name of the parameter */
    http_req->headers[loop] = pair;

    /* Remember the start of this name: value pair */
    equals = pair;

    /* Find the start of the next name: value pair and replace the delimiter
     * with a 0 to terminate the previous pair string. */
    pair = strchr(pair, '\r');
    if (pair) {
      *pair = '\0';
      pair += 2;
    } else {
      /* We didn't find a new parameter so find the last header */
      /* Revert to NULL so that we exit the loop as expected. */
      pair = NULL;
    }

    /* Now find the ": "" in the previous pair, replace it with '\0' and save
     * the parameter value string. */
    equals = strchr(equals, ':');
    if (equals) {
      *equals = '\0';
      http_req->header_vals[loop] = equals + 2;
    } else {
      http_req->header_vals[loop] = NULL;
    }
  }

  http_req->headercount = loop;

  return loop;
}
