/*
 * WiFi support functions for the KIM-1 Programmable Memory Board
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
 *  Derived from code of the PicoWi library, original copyrigth follows
 */

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

#include <stdio.h>
#include "pico/stdlib.h"
#include "picowi.h"

#include "config.h"
#include "debug.h"

#define EVENT_POLL_USEC     100000

void wlan_blink_fast( int number )
{
    bool ledon=false;

    while ( true )
    {
        for ( int i= 0; i < number; ++i )
        {
            wifi_set_led( ledon = !ledon );
            sleep_ms( 100 );
        }
        sleep_ms( 500 );
    }
}

void wlan_setup( void )
{
    // set_display_mode( DISP_INFO | DISP_JOIN | DISP_TCP | DISP_TCP_STATE );
    set_display_mode( DISP_INFO | DISP_JOIN | DISP_TCP_STATE );
    io_init();
    usdelay( 1000 );

    if ( !net_init() )
    {
        debug_printf( DBG_ERROR, "WiFi: Failed to initialise.\n" );
        wlan_blink_fast( 2 );
    }

    else if ( !net_join( config.network.country, config.network.ssid, config.network.passwd ) )
    {
        debug_printf( DBG_ERROR, "Failed to connect.\n" );
        wlan_blink_fast( 3 );
    }

    else
    {    
        uint32_t led_ticks, poll_ticks, ping_ticks;
        bool ledon=false;
        
        ustimeout( &led_ticks, 0 );
        ustimeout( &poll_ticks, 0 );
        while ( dhcp_complete != 2 )
        {
            // Toggle LED at 0.5 Hz if joined, 5 Hz if not
            if ( ustimeout( &led_ticks, link_check() > 0 ? 1000000 : 100000 ) )
            {
                wifi_set_led( ledon = !ledon );
                ustimeout( &ping_ticks, 0 );
            }
            // Get any events, poll the network-join state machine
            if ( wifi_get_irq() || ustimeout( &poll_ticks, EVENT_POLL_USEC ) )
            {
                event_poll();
                join_state_poll( config.network.ssid, config.network.passwd );
                ustimeout( &poll_ticks, 0 );
            }
            // If link is up, poll DHCP state machine
            if ( link_check() > 0 )
                dhcp_poll();
            // When DHCP is complete, print IP addresses
            if ( dhcp_complete == 1 )
            {
                printf( "DHCP complete, IP address " );
                print_ip_addr( my_ip) ;
                printf( "\n" );
                dhcp_complete = 2;
            }
        }

        // turn on LED to signal connected
        wifi_set_led( true );
    }
}
