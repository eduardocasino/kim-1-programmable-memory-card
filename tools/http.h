/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * HTTP protocol support
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

#ifndef MEMCFG_HTTP_H
#define MEMCFG_HTTP_H

#include <stdint.h>
#include <curl/curl.h>

#include "globals.h"

typedef struct {
        CURL *curl;
        CURLU *url;
        size_t data_size;
        long http_code;
        uint8_t *buffer;
        size_t buffer_size;
        size_t transferred_bytes;
} http_t;

typedef enum { GET = 0, PATCH = 1, POST = 2, PUT = 3 } http_method_t;

typedef size_t (*http_callback_t)( char *ptr, size_t size, size_t nmemb, http_t *http );
size_t http_read_callback( char *ptr, size_t size, size_t nmemb, http_t *http );
size_t http_write_callback( char *ptr, size_t size, size_t nmemb, http_t *http );

http_t *http_init( const char *host );
status_t http_construct_request( http_t *http, http_method_t method, const char *resource, const char *query, uint8_t *buffer, size_t buffer_size, http_callback_t callback );
status_t http_perform( http_t *http );
status_t http_send_request( http_t *http, http_method_t method, const char *host, const char *resource, const char *query, uint8_t *buffer, size_t buffer_size, http_callback_t callback );
void http_cleanup( http_t *http );

#endif