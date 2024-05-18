/*
 * main file for the KIM-1 Programmable Memory Board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 *  Copyright (C) 2024 Eduardo Casino
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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

#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

#include "config.h"
#include "piocfg.h"
#include "wlan.h"
#include "webserver.h"

void main( void )
{
    stdio_init_all();

    // Set overclock
    //
    // set_sys_clock_khz(200000, true);

    // Copy default memory map from flash
    //
    config_copy_default_memory_map( &mem_map[0] );
    
    // Setup PIO State Machines, pins and DMA channels
    //
    piocfg_setup( &mem_map[0] );

    // Setup wireless network. This function only returns if connection is
    // successful.
    //
    wlan_setup();

    // Setup commands webserver. This function never returns
    //
    webserver_run();

    // If returns, its been an error
    //
    wlan_blink_fast( 4 );
}
