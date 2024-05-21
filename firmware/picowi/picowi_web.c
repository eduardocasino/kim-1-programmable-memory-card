// PicoWi Web interface, see http://iosoft.blog/picowi for details
//
// Copyright (c) 2022, Jeremy P Bentham
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// 18/05/2024 - Eduardo Casino - Add support for POST/PUT/PATCH HTTP methods and
//                               some HTTP error support functions

#include <stdio.h>
#include <string.h>
#include "picowi_defs.h"
#include "picowi_ip.h"
#include "picowi_net.h"
#include "picowi_tcp.h"
#include "picowi_web.h"

int num_web_handlers;
extern NET_SOCKET net_sockets[NUM_NET_SOCKETS];
WEB_HANDLER web_handlers[MAX_WEB_HANDLERS];

static WEB_METHOD web_methods[] = {
    { HTTP_GET,   "GET " },
    { HTTP_POST,  "POST " },
    { HTTP_PUT,   "PUT " },
    { HTTP_PATCH, "PATCH " },
    { HTTP_UNSUPPORTED, "" }
};

// Set up a Web page handler
int web_page_handler(web_method_t method, char *uri, web_handler_t handler)
{
    if (num_web_handlers < MAX_WEB_HANDLERS)
    {
        web_handlers[num_web_handlers].method = method;
        web_handlers[num_web_handlers].uri = uri;
        web_handlers[num_web_handlers++].handler = handler;
        return (1);
    }
    return (0);
}    

// Handle a Web page request, return length of response
int web_page_rx(int sock, char *req, int len)
{
    static int lastseq = -1;
    int i, n, found = 0;
    WEB_METHOD *wm=web_methods;
    WEB_HANDLER *whp=web_handlers;
    NET_SOCKET *ts = &net_sockets[sock];
    web_method_t method;
    char *uri;

    if ( lastseq == ts->seq )
    {
        return (ts->web_handler(sock, req, len));
    }
    
    lastseq = ts->seq;

    uri = strchr(req, '/' );

    /* Find method of request*/
    for (; wm->method != HTTP_UNSUPPORTED; wm++)
    {
        n = strlen(wm->method_str);
        if (!memcmp(wm->method_str, req, n))
        {
            break;
        }
    }
    method = wm->method;

    if (method == HTTP_UNSUPPORTED)
    {
        return web_405_method_not_allowed(sock);        
    }

    for (i = 0; i < MAX_WEB_HANDLERS; i++, whp++)
    {
        if (whp->uri && whp->handler)
        {
            n = strlen(whp->uri);
            if (!memcmp(whp->uri, uri, n))
            {
                ++found;
                if (whp->method == method)
                {
                    ts->web_handler = whp->handler;
                    return (whp->handler(sock, req, len));
                }
            }
        }
    }

    if (found)
        return (web_405_method_not_allowed(sock));
    else
        return (web_404_not_found(sock));
}

// Add data to an HTTP response
int web_resp_add_data(int sock, BYTE *data, int dlen)
{
    return (tcp_sock_add_tx_data(sock, data, dlen));
}

// Add string to an HTTP response
int web_resp_add_str(int sock, char *str)
{
    return (tcp_sock_add_tx_data(sock, (BYTE *)str, strlen(str)));
}

// Add content length string to an HTTP response
int web_resp_add_content_len(int sock, int n)
{
    NET_SOCKET *ts = &net_sockets[sock];
    int i = sprintf((char *)&ts->txbuff[TCP_DATA_OFFSET + ts->txdlen], HTTP_CONTENT_LENGTH, n);
    ts->txdlen += i;
    return (i);
}

// Send a Web response
int web_resp_send(int sock)
{
    NET_SOCKET *ts = net_socket_ptr(sock);

    return(tcp_sock_send(sock, TCP_ACK, 0, ts->txdlen));
}

// Send a "400 Bad Request" response
int web_400_bad_request(int sock)
{
    int n;
    
    n = web_resp_add_str(sock, HTTP_400_FAIL HTTP_SERVER HTTP_NOCACHE); 
    n += web_resp_add_content_len(sock, 0);
    n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
    tcp_sock_close(sock);
    
    return (n);
}


// Send a "404 Not Found" response
int web_404_not_found(int sock)
{
    int n;
    
    n = web_resp_add_str(sock, HTTP_404_FAIL HTTP_SERVER HTTP_NOCACHE);
    n += web_resp_add_content_len(sock, 0);
    n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
    tcp_sock_close(sock);
    
    return (n);
}

// Send a "405 Method Not Allowed" response
int web_405_method_not_allowed(int sock)
{
    int n;

    n = web_resp_add_str(sock, HTTP_405_FAIL HTTP_SERVER HTTP_NOCACHE);
    n += web_resp_add_content_len(sock, 0);
    n += web_resp_add_str(sock, HTTP_CONNECTION_CLOSE HTTP_HEADER_END);
    tcp_sock_close(sock);
    
    return (n);
}
// EOF
