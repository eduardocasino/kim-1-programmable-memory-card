#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "config.h"

void wlan_blink_fast( int number )
{
    while ( true )
    {
        for ( int i= 0; i < number; ++i )
        {
            cyw43_arch_gpio_put( CYW43_WL_GPIO_LED_PIN, 1 );
            sleep_ms( 100 );
            cyw43_arch_gpio_put( CYW43_WL_GPIO_LED_PIN, 0 );
            sleep_ms( 100 );
        }
        sleep_ms( 500 );
    }
}

void wlan_setup( void )
{
    if ( cyw43_arch_init_with_country( config.network.country ) )
    {
        printf( "WiFi: Failed to initialise.\n" );
        wlan_blink_fast( 2 );
    }

    cyw43_arch_enable_sta_mode();           // Enable Wi-Fi in Station mode such that connections
                                            // can be made to other Wi-Fi Access Points
    
    // TODO: Check if CYW43_AGGRESSIVE_PM gives enough performance
    //
    cyw43_wifi_pm( &cyw43_state, CYW43_DEFAULT_PM );

    printf( "WiFi: Connecting..." );
    if ( cyw43_arch_wifi_connect_timeout_ms( config.network.ssid, config.network.passwd, CYW43_AUTH_WPA2_AES_PSK, 60000 ) )
    {
        printf( " Failed to connect.\n" );
        wlan_blink_fast( 3 );
    }
    else
    {
        printf( " Connected.\n" );

        int ip_addr = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
        printf( "IP Address: %lu.%lu.%lu.%lu\n", ip_addr & 0xFF, (ip_addr >> 8) & 0xFF, (ip_addr >> 16) & 0xFF, ip_addr >> 24 );
    }
    
    // turn on LED to signal connected
    cyw43_arch_gpio_put( CYW43_WL_GPIO_LED_PIN, 1 );
}
