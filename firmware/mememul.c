#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

#include "config.h"
#include "piocfg.h"
#include "wlan.h"
#include "webserver.h"

void main( void )
{

    // Set overclock
    // set_sys_clock_pll(1064000000, 4, 1);    // Overclock to 266MHz

    stdio_init_all();

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
