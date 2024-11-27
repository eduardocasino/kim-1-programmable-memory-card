/*
 * Video PIO configuration for the KIM-1 Programmable Memory Board
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
 */

/*
 * Credit to Alan Reed for his composite video implementation:
 * https://github.com/alanpreed/pico-composite-video
 */

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#include "video.pio.h"

#include "config.h"
#include "pins.h"
#include "dmacfg.h"
#include "video.h"

#define NTSC_SCANLINES      260
#define PAL_SCANLINES       312
#define VERT_SYNC_SCANLINES 4

#define CVIDEO_LINES        200
#define CVIDEO_PIX_PER_LINE 320
#define VIDEO_MEMORY_SIZE   ( CVIDEO_LINES * CVIDEO_PIX_PER_LINE ) / 8

// Sync PIO needs 8us per instruction
#define SYNC_INTERVAL 0.000008
// Data transmits for 40us
#define DATA_INTERVAL 0.000040

int sync_blank_lines[] = {
    ( NTSC_SCANLINES - CVIDEO_LINES - VERT_SYNC_SCANLINES ) / 2 ,
    ( PAL_SCANLINES - CVIDEO_LINES - VERT_SYNC_SCANLINES ) / 2 ,
};


/*
#define CVIDEO_PIX_PER_LINE 320

.define         DATA_DELAY       8
.define public  CLOCKS_PER_BIT   DATA_DELAY + 2

CLOCS_PER_BIT 10



125000000 / ( 320 / 0.000040 ) / 10
*/

// 1,5625 

static uint16_t *video_mem_start;

void video_set_mem_start( uint16_t mem_start )
{
    video_mem_start = &mem_map[mem_start];
}

uint16_t video_get_mem_start( void )
{
    return (uint16_t) ( video_mem_start - mem_map );
}

static void video_gpio_pins( PIO pio )
{
    pio_gpio_init( pio, VSYNC );
    pio_gpio_init( pio, HSYNC );
    pio_gpio_init( pio, VIDEO );
    gpio_set_outover( VIDEO, GPIO_OVERRIDE_INVERT );
    gpio_put( VIDEO, 0 );

}

static int video_create_cvsync_sm( PIO pio )
{
    int cvsync_sm       = pio_claim_unused_sm( pio, true );                         // Claim a free state machine for video sync on PIO 1
    uint cvsync_offset  = pio_add_program( pio, &cvsync_program );                  // Instruction memory offset for the SM
    float sync_clockdiv = clock_get_hz( clk_sys ) * SYNC_INTERVAL;

    pio_sm_config cvsync_config = cvsync_program_get_default_config( cvsync_offset );    // Get default config for the pal video sync SM

    sm_config_set_sideset_pins( &cvsync_config, HSYNC );                           // Pin set for side instructions
    sm_config_set_clkdiv( &cvsync_config, sync_clockdiv );                         // Set the cock speed

    pio_sm_set_consecutive_pindirs( pio, cvsync_sm, HSYNC, 1, true );              // Set HSYNC pin as output

    pio_sm_init( pio, cvsync_sm, cvsync_offset, &cvsync_config );

    pio_sm_put( pio, cvsync_sm, sync_blank_lines[config.video.system] - 1 );       // Tell the state machine the number of blank scanlines between vsync pulse
    pio_sm_put( pio, cvsync_sm, CVIDEO_LINES - 1 );                                // Tell the state machine the number of video lines (minus 1)

    return cvsync_sm;
}

static int video_create_cvdata_sm( PIO pio )
{
    int cvdata_sm       = pio_claim_unused_sm( pio, true );                             // Claim a free state machine for video data on PIO 1
    uint cvdata_offset  = pio_add_program( pio, &cvdata_program );                      // Instruction memory offset for the SM
    // Run the data clock 32x faster than needed to reduce horizontal jitter due to synchronisation between SMs
    float data_clockdiv = ( clock_get_hz( clk_sys ) / (CVIDEO_PIX_PER_LINE / DATA_INTERVAL)) / CLOCKS_PER_BIT;

    pio_sm_config cvdata_config = cvdata_program_get_default_config( cvdata_offset );   // Get default config for the video data SM

    sm_config_set_out_pins(  &cvdata_config, VIDEO, 1 );                                // Pin set for OUT instructions.
    sm_config_set_set_pins(  &cvdata_config, VIDEO, 1 );                                // Pin set for SET instructions.
    sm_config_set_clkdiv( &cvdata_config, data_clockdiv );                              // Set the cock speed
    sm_config_set_out_shift( &cvdata_config, false, true, 8 );                          // Shift left DATA into OSR, autopush
    sm_config_set_fifo_join( &cvdata_config, PIO_FIFO_JOIN_TX );                        // Join FiFos for TX
    pio_sm_set_consecutive_pindirs( pio, cvdata_sm, VIDEO, 1, true );                   // Set HSYNC pin as output

    pio_interrupt_clear( pio, DATA_IRQ );                                               // Ensure that data IRQ is cleared at start

    pio_sm_init( pio, cvdata_sm, cvdata_offset, &cvdata_config );

    pio_sm_put( pio, cvdata_sm, CVIDEO_PIX_PER_LINE - 1 );                              // Tell the state machine the number of pixels per line (minus 1)

    return cvdata_sm;
}

void video_setup( uint16_t *mem_map )
{
    // Configure PIO
    //
    PIO pio = pio1;

    video_gpio_pins( pio );

    // Create and configure state machines
    //
    // * cvsync_sm performs the read operations and, when a write is requested, checks if memory is writable and, if so, starts handles control to cvdata_sm
    // * cvdata_sm performs the write operation and returns control to cvsync_sm
    //

    int cvdata_sm  = video_create_cvdata_sm( pio );
    int cvsync_sm  = video_create_cvsync_sm( pio );

    // Configure the DMA channels
    //
    // * Channel cvdata_dma:        Moves data from the video memory area in 16bit words, swaps bytes and places into TX FiFo higher bytes. Chains to cvdata_rearm_dma
    // * Channel cvdata_rearm_dma:  Reconfigures cvdata_dma and launchs it again.
    //
    int cvdata_dma          = dma_claim_unused_channel( true );
    int cvdata_rearm_dma    = dma_claim_unused_channel( true );

    // Init control block
    video_set_mem_start( config.video.address );

    video_mem_start = &mem_map[config.video.address];

    dma_channel_config cvdata_dma_config = dmacfg_config_channel(
                cvdata_dma,
                false,                                                      // Mark as normal priority
                false,                                                      // Do not generate interrupts
                pio_get_dreq( pio, cvdata_sm, true ),                       // Signals data transfer from PIO, transmit
                DMA_SIZE_16,
                cvdata_rearm_dma,                                           // Chains to cvdata_rearm_dma
                (uint16_t *)&pio->txf[cvdata_sm]+1,                         // Writes to the higher bytes of cvdata_sm TX FiFo
                &mem_map,                                                   // Reads from mem_map (overwritten by cvdata_rearm_dma)
                VIDEO_MEMORY_SIZE,                                          // Transfer whole video buffer (overwritten by cvdata_rearm_dma)
                true,                                                       // Enable byte swapping
                false,                                                      // Do not increment write addr
                true,                                                       // Increment read addr
                false                                                       // Do not start
                );
    
    dma_channel_config cvdata_rearm_dma_config = dmacfg_config_channel(
                cvdata_rearm_dma,
                false,                                                      // Mark as normal priority
                false,                                                      // Do not generate interrupts
                DREQ_FORCE,                                                 // Permanent request transfer
                DMA_SIZE_32,
                cvdata_rearm_dma,                                           // Do not chain
                &dma_hw->ch[cvdata_dma].al3_read_addr_trig,                 // Configures read address, triggers channel
                &video_mem_start,                                           // Reads from pointer to video memory start
                1,                                                          // Transfer 1 dword
                false,                                                      // Do not do byte swapping
                false,                                                      // Do not increment write addr
                false,                                                      // Do not increment read addr
                true                                                        // Start immediately
                );
    
    // Enable State Machines
    //
    pio_sm_set_enabled( pio, cvdata_sm, true );
    pio_sm_set_enabled( pio, cvsync_sm, true );

}

