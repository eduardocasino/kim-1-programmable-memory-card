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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "globals.h"

#include "http.h"

#define SCHEME  "http"

size_t http_read_callback( char *ptr, size_t size, size_t nmemb, http_t *http )
{
    size_t to_send = ( http->data_size - http->transferred_bytes > nmemb ) ? nmemb : http->data_size - http->transferred_bytes;

    if ( http->transferred_bytes + to_send > http->buffer_size )
    {
        fprintf( stderr, "Error: trying to send more than buffer size (%ld) bytes\n", http->buffer_size );
        return 0;
    }

    memcpy( ptr, &http->buffer[http->transferred_bytes], to_send );
    
    http->transferred_bytes += to_send;

    return to_send;
}

size_t http_write_callback( char *ptr, size_t size, size_t nmemb, http_t *http )
{
    if ( http->transferred_bytes + nmemb > http->buffer_size )
    {
        fprintf( stderr, "Error: received more than buffer size (%ld) bytes\n", http->buffer_size );
        return 0;
    }

    memcpy( &http->buffer[http->transferred_bytes], ptr, nmemb );
    
    http->transferred_bytes += nmemb;

    return nmemb;
}
 
status_t http_construct_request( http_t *http, http_method_t method, const char *resource, const char *query, uint8_t *buffer, size_t buffer_size, http_callback_t callback )
{
    static const char *methods[] = { "GET", "PATCH", "POST", "PUT" };
    CURLcode rc = CURLE_OK;
    static struct curl_slist *headers = NULL;

    http->buffer = buffer;
    http->buffer_size = buffer_size;
    
    if ( NULL != headers )
    {
        curl_slist_free_all( headers );
        headers = NULL;
    }

    // curl_easy_setopt( http->curl, CURLOPT_VERBOSE, 1 );

    switch ( method )
    {
        case PATCH:
        case PUT:            
        case POST:
            headers = curl_slist_append( headers, "Content-Type: application/octet-stream");
            if ( NULL != headers )
            {
                rc = curl_easy_setopt( http->curl, CURLOPT_HTTPHEADER, headers );
            }
            if ( CURLE_OK == rc )
            {
                curl_easy_setopt( http->curl, CURLOPT_POST, 1 );
            }
            if ( CURLE_OK == rc )
            {
                curl_easy_setopt( http->curl, CURLOPT_POSTFIELDS, 0 );
            }
            if ( CURLE_OK == rc )
            {
                if ( NULL == callback )
                {
                    rc = curl_easy_setopt( http->curl, CURLOPT_POSTFIELDSIZE, 0);
                }
                else
                {
                    rc = curl_easy_setopt( http->curl, CURLOPT_POSTFIELDSIZE, (curl_off_t) http->data_size );

                    if ( CURLE_OK == rc )
                    {
                        rc = curl_easy_setopt( http->curl, CURLOPT_READDATA, http );
                    }
                    
                    if ( CURLE_OK == rc )
                    {
                        rc = curl_easy_setopt( http->curl, CURLOPT_READFUNCTION, callback );
                    }
                }
            }

            if ( CURLE_OK == rc && POST != method )
            {
                rc = curl_easy_setopt( http->curl, CURLOPT_CUSTOMREQUEST, methods[method] );
            }
            break;    
        case GET:
        default:
            if ( CURLE_OK == rc )
            {
                curl_easy_setopt( http->curl, CURLOPT_HTTPGET, 1 );
            }

            if ( CURLE_OK == rc && NULL != callback )
            {
                rc = curl_easy_setopt( http->curl, CURLOPT_WRITEFUNCTION, callback );

                if ( CURLE_OK == rc )
                {
                    rc = curl_easy_setopt( http->curl, CURLOPT_WRITEDATA, http );
                }
            }
    }

    if ( CURLE_OK == rc && NULL != resource )
    {
        rc = curl_url_set( http->url, CURLUPART_PATH, resource, 0 );
    }

    if ( CURLE_OK == rc )
    {
        rc = curl_url_set( http->url, CURLUPART_QUERY, query, 0 );
    }

    if ( CURLE_OK == rc )
    {
        rc = curl_easy_setopt( http->curl, CURLOPT_CURLU, http->url );
    }
    
    if ( CURLE_OK != rc )
    {   
        fprintf( stderr, "Error setting up the request: %s\n", curl_easy_strerror( rc ) );
        return FAILURE;
    }

    http->transferred_bytes = 0;

    return SUCCESS;
}

status_t http_perform( http_t *http )
{
    CURLcode rc;
    curl_off_t content_length;

    rc = curl_easy_perform( http->curl );

    if ( CURLE_OK == rc )
    {
        rc = curl_easy_getinfo( http->curl, CURLINFO_RESPONSE_CODE, &http->http_code );
    }
    if ( CURLE_OK == rc )
    {
        rc = curl_easy_getinfo( http->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length );
        http->data_size = (size_t) content_length;
    }

    if ( CURLE_OK != rc )
    {
        fprintf( stderr, "Error executing the request: %s\n", curl_easy_strerror( rc ) );
    }

    return ( CURLE_OK == rc ) ? SUCCESS : FAILURE;
}

status_t http_send_request( http_t *http, http_method_t method, const char *host, const char *resource, const char *query, uint8_t *buffer, size_t buffer_size, http_callback_t callback )
{
    status_t status;

    if ( SUCCESS == ( status = http_construct_request( http, method, resource, query, buffer, buffer_size, callback ) ) )
    {
        if ( SUCCESS == ( status = http_perform( http ) ) )
        {
            if ( http->http_code != 200 )
            {
                fprintf( stderr, "Unexpected response code (%ld) from %s%s?%s\n", http->http_code, host, resource, query );
                status = FAILURE;
            }
        }
    }
    
    return status;
}

void http_cleanup( http_t *http )
{
    if ( NULL != http )
    {
        if ( NULL != http->curl )
        {
            if ( NULL != http->url )
            {
                curl_url_cleanup( http->url );

            }
            curl_easy_cleanup( http->curl );
            curl_global_cleanup();
            free( http );
        }
    }

    return;
}

http_t *http_init( const char *host )
{
    CURL *curl;
    CURLUcode rc;

    http_t *http = NULL;

    if ( CURLE_OK != curl_global_init( CURL_GLOBAL_DEFAULT ) )
    {
        fputs( "Error initializing http library\n", stderr );
    }
    else if ( NULL != ( curl = curl_easy_init() ) )
    {
        if ( NULL != ( http = malloc( sizeof( http_t ) ) ) )
        {
            http->curl = curl;
            http->url = curl_url();
            http->http_code = 0;
            http->data_size = 0;

            if ( NULL == http->url )
            {
                fprintf( stderr, "Error: Can't allocate memory for url\n" );
                http_cleanup( http );
                http = NULL;
            }
            else
            {
                if ( CURLUE_OK != ( rc = curl_url_set( http->url, CURLUPART_SCHEME, SCHEME, 0 ) ) ||
                     CURLUE_OK != ( rc = curl_url_set( http->url, CURLUPART_HOST, host, 0 ) ) )
                {
                    fprintf( stderr, "Error setting the url: %s\n", curl_easy_strerror( rc ) );
                    http_cleanup( http );
                    http = NULL;
                }
            }
        }
        else
        {
            perror( "Error creating http connection" );
            curl_easy_cleanup( curl );
            curl_global_cleanup();
        }
    }
    else
    {
        fputs( "Error creating http connection", stderr ); 
        curl_global_cleanup();
    }

    return http;
}