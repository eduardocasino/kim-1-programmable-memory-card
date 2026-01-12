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
 * and Hunter Adams for his VGA implementation:
 * https://github.com/vha3/Hunter-Adams-RP2040-Demos/tree/master/VGA_Graphics
 */

#include <stdint.h>
#include <assert.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#include "video.pio.h"
#include "vga.pio.h"

#include "config.h"
#include "pins.h"
#include "dmacfg.h"
#include "video.h"
#include "debug.h"

#define NTSC_SCANLINES      260
#define PAL_SCANLINES       312
#define VERT_SYNC_SCANLINES 4

#define VIDEO_LINES        200
#define VIDEO_PIX_PER_LINE 320
#define VIDEO_MEMORY_SIZE  ( VIDEO_LINES * VIDEO_PIX_PER_LINE ) / 8

// ---------- Composite video timings ----------
// Sync PIO needs 8us per instruction
#define SYNC_INTERVAL 0.000008
// Data transmits for 40us
#define DATA_INTERVAL 0.000040

int sync_blank_lines[] = {
    ( NTSC_SCANLINES - VIDEO_LINES - VERT_SYNC_SCANLINES ) / 2 ,
    ( PAL_SCANLINES - VIDEO_LINES - VERT_SYNC_SCANLINES ) / 2
};

// ---------- VGA timings ----------
#define F_PORCH     16
#define VGA_DIV     5           // This gives 25MHz with a base clock of 125MHz

typedef enum { MODE_640_400 = 0, MODE_640_480, MODE_720_400, MODE_768_400, MODE_NUM } vga_mode_t;

typedef struct {
    uint16_t h_size;
    uint16_t v_size;
    uint8_t h_bar_size;
    uint8_t v_bar1_size;
    uint8_t v_bar2_size;
    uint vgahsync_offset;
    pio_sm_config vgahsync_config;
    uint vgavsync_offset;
    pio_sm_config vgavsync_config;
} vga_mode_data_t;

static inline vga_mode_t system_to_vga_mode( uint16_t system )
{
    vga_mode_t mode;

    assert( system > 1 && system < 6 );

    switch( system )
    {
        case 2:
            mode = MODE_640_400;
            break;
        case 3:
            mode = MODE_640_480;
            break;
        case 4:
            mode = MODE_720_400;
            break;
        case 5:
        default:
            mode = MODE_768_400;
    }

    return mode;
}

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
}

static int video_create_cvsync_sm( PIO pio, uint16_t system )
{
    int cvsync_sm       = pio_claim_unused_sm( pio, true );                             // Claim a free state machine for video sync on PIO 1
    uint cvsync_offset  = pio_add_program( pio, &cvsync_program );                      // Instruction memory offset for the SM
    float sync_clockdiv = clock_get_hz( clk_sys ) * SYNC_INTERVAL;

    pio_sm_config cvsync_config = cvsync_program_get_default_config( cvsync_offset );   // Get default config for the pal video sync SM

    sm_config_set_sideset_pins( &cvsync_config, HSYNC );                                // Pin set for side instructions
    sm_config_set_clkdiv( &cvsync_config, sync_clockdiv );                              // Set the clock speed

    pio_sm_set_consecutive_pindirs( pio, cvsync_sm, HSYNC, 1, true );                   // Set HSYNC pin as output

    pio_sm_init( pio, cvsync_sm, cvsync_offset, &cvsync_config );

    pio_sm_put( pio, cvsync_sm, sync_blank_lines[system] - 1 );                         // Tell the state machine the number of blank scanlines between vsync pulse
    pio_sm_put( pio, cvsync_sm, VIDEO_LINES - 1 );                                      // Tell the state machine the number of video lines (minus 1)

    return cvsync_sm;
}

static int video_create_cvdata_sm( PIO pio )
{
    int cvdata_sm       = pio_claim_unused_sm( pio, true );                             // Claim a free state machine for video data on PIO 1
    uint cvdata_offset  = pio_add_program( pio, &cvdata_program );                      // Instruction memory offset for the SM
    // Run the data clock 32x faster than needed to reduce horizontal jitter due to synchronisation between SMs
    float data_clockdiv = ( clock_get_hz( clk_sys ) / (VIDEO_PIX_PER_LINE / DATA_INTERVAL)) / CLOCKS_PER_BIT;

    pio_sm_config cvdata_config = cvdata_program_get_default_config( cvdata_offset );   // Get default config for the video data SM

    sm_config_set_out_pins(  &cvdata_config, VIDEO, 1 );                                // Pin set for OUT instructions.
    sm_config_set_set_pins(  &cvdata_config, VIDEO, 1 );                                // Pin set for SET instructions.
    sm_config_set_clkdiv( &cvdata_config, data_clockdiv );                              // Set the clock speed
    sm_config_set_out_shift( &cvdata_config, false, true, 8 );                          // Shift left DATA into OSR, autopush
    sm_config_set_fifo_join( &cvdata_config, PIO_FIFO_JOIN_TX );                        // Join FiFos for TX
    pio_sm_set_consecutive_pindirs( pio, cvdata_sm, VIDEO, 1, true );                   // Set HSYNC pin as output

    pio_interrupt_clear( pio, DATA_IRQ );                                               // Ensure that data IRQ is cleared at start

    pio_sm_init( pio, cvdata_sm, cvdata_offset, &cvdata_config );

    pio_sm_put( pio, cvdata_sm, VIDEO_PIX_PER_LINE - 1 );                               // Tell the state machine the number of pixels per line (minus 1)

    return cvdata_sm;
}

static void init_vga_modes( PIO pio, vga_mode_data_t *vga_mode_data )
{
    vga_mode_data[MODE_640_400].h_size = 640; 
    vga_mode_data[MODE_640_400].v_size = 400; 
    vga_mode_data[MODE_640_400].h_bar_size = 0; 
    vga_mode_data[MODE_640_400].v_bar1_size = 0;
    vga_mode_data[MODE_640_400].v_bar2_size = 0; 
    vga_mode_data[MODE_640_400].vgahsync_offset = pio_add_program( pio, &vgahsync_640_program );
    vga_mode_data[MODE_640_400].vgahsync_config = vgahsync_640_program_get_default_config( vga_mode_data[MODE_640_400].vgahsync_offset ); 
    vga_mode_data[MODE_640_400].vgavsync_offset = pio_add_program( pio, &vgavsync_400_program );
    vga_mode_data[MODE_640_400].vgavsync_config = vgavsync_400_program_get_default_config( vga_mode_data[MODE_640_400].vgavsync_offset );

    vga_mode_data[MODE_640_480].h_size = 640; 
    vga_mode_data[MODE_640_480].v_size = 480; 
    vga_mode_data[MODE_640_480].h_bar_size = 20; 
    vga_mode_data[MODE_640_480].v_bar1_size = 0;
    vga_mode_data[MODE_640_480].v_bar2_size = 0; 
    vga_mode_data[MODE_640_480].vgahsync_offset = pio_add_program( pio, &vgahsync_640_program );
    vga_mode_data[MODE_640_480].vgahsync_config = vgahsync_640_program_get_default_config( vga_mode_data[MODE_640_480].vgahsync_offset ); 
    vga_mode_data[MODE_640_480].vgavsync_offset = pio_add_program( pio, &vgavsync_480_program );
    vga_mode_data[MODE_640_480].vgavsync_config = vgavsync_480_program_get_default_config( vga_mode_data[MODE_640_480].vgavsync_offset );

    vga_mode_data[MODE_720_400].h_size = 720; 
    vga_mode_data[MODE_720_400].v_size = 400; 
    vga_mode_data[MODE_720_400].h_bar_size = 0; 
    vga_mode_data[MODE_720_400].v_bar1_size = 48;
    vga_mode_data[MODE_720_400].v_bar1_size = 32; 
    vga_mode_data[MODE_720_400].vgahsync_offset = pio_add_program( pio, &vgahsync_720_program );
    vga_mode_data[MODE_720_400].vgahsync_config = vgahsync_720_program_get_default_config( vga_mode_data[MODE_720_400].vgahsync_offset ); 
    vga_mode_data[MODE_720_400].vgavsync_offset = pio_add_program( pio, &vgavsync_400_program );
    vga_mode_data[MODE_720_400].vgavsync_config = vgavsync_400_program_get_default_config( vga_mode_data[MODE_720_400].vgavsync_offset );

    vga_mode_data[MODE_768_400].h_size = 768; 
    vga_mode_data[MODE_768_400].v_size = 400; 
    vga_mode_data[MODE_768_400].h_bar_size = 0; 
    vga_mode_data[MODE_768_400].v_bar1_size = 32; 
    vga_mode_data[MODE_768_400].v_bar2_size = 32; 
    vga_mode_data[MODE_768_400].vgahsync_offset = pio_add_program( pio, &vgahsync_768_program );
    vga_mode_data[MODE_768_400].vgahsync_config = vgahsync_768_program_get_default_config( vga_mode_data[MODE_768_400].vgahsync_offset ); 
    vga_mode_data[MODE_768_400].vgavsync_offset = pio_add_program( pio, &vgavsync_400_program );
    vga_mode_data[MODE_768_400].vgavsync_config = vgavsync_400_program_get_default_config( vga_mode_data[MODE_768_400].vgavsync_offset );    
}

static int video_create_vgahsync_sm( PIO pio, vga_mode_data_t *vga_mode_data )
{
    int vgahsync_sm = pio_claim_unused_sm( pio, true );                     // Claim a free state machine for horizontal video sync on PIO 1

    sm_config_set_set_pins( &vga_mode_data->vgahsync_config, HSYNC, 1 );    // Pin set for set instructions
    sm_config_set_clkdiv( &vga_mode_data->vgahsync_config, VGA_DIV );       // Set the clock speed to 25MHz

    pio_sm_set_consecutive_pindirs( pio, vgahsync_sm, HSYNC, 1, true );     // Set HSYNC pin as output

    pio_sm_init( pio, vgahsync_sm, vga_mode_data->vgahsync_offset, &vga_mode_data->vgahsync_config );

    pio_sm_put(pio, vgahsync_sm, vga_mode_data->h_size + vga_mode_data->h_bar_size * 2 + F_PORCH - 1 );

    return vgahsync_sm;
}

static int video_create_vgavsync_sm( PIO pio, vga_mode_data_t *vga_mode_data )
{
    int vgavsync_sm = pio_claim_unused_sm( pio, true );                     // Claim a free state machine for vertical video sync on PIO 1
        
    sm_config_set_set_pins( &vga_mode_data->vgavsync_config, VSYNC, 1 );    // Pin set for set instructions
    sm_config_set_sideset_pins( &vga_mode_data->vgavsync_config, VSYNC );   // Pin set for side instructions
    sm_config_set_clkdiv( &vga_mode_data->vgavsync_config, VGA_DIV );       // Set the clock speed to 25MHz

    pio_sm_set_consecutive_pindirs( pio, vgavsync_sm, VSYNC, 1, true );     // Set VSYNC pin as output
    

    pio_sm_init( pio, vgavsync_sm, vga_mode_data->vgavsync_offset, &vga_mode_data->vgavsync_config );

    pio_sm_put(pio, vgavsync_sm, vga_mode_data->v_size - 1 );

    return vgavsync_sm;
}

static int video_create_vgadata_sm( PIO pio, vga_mode_data_t *vga_mode_data )
{
    int vgadata_sm      = pio_claim_unused_sm( pio, true );                             // Claim a free state machine for vertical video sync on PIO 1
    uint vgadata_offset = pio_add_program( pio, &vgadata_program );                     // Instruction memory offset for the SM

    pio_sm_config vgadata_config = vgadata_program_get_default_config( vgadata_offset ); // Get default config for the pal video sync SM

    sm_config_set_out_pins( &vgadata_config, VIDEO, 1 );                                // Pin set for out instructions
    sm_config_set_sideset_pins( &vgadata_config, VIDEO );                               // Pin set for side instructions
    
    sm_config_set_out_shift( &vgadata_config, false, true, 8 );                         // Shift left DATA into OSR, autopush
    sm_config_set_fifo_join ( &vgadata_config, PIO_FIFO_JOIN_TX );                      // longer FIFO to avoid bursty data
    pio_sm_set_consecutive_pindirs( pio, vgadata_sm, VIDEO, 1, true );                  // Set VIDEO pin as output

    pio_interrupt_clear( pio, VGADATA_IRQ ); 

    pio_sm_init( pio, vgadata_sm, vgadata_offset, &vgadata_config );

    pio_sm_put( pio, vgadata_sm, vga_mode_data->h_size/2 - 1 );

    return vgadata_sm;
}

static void video_setup_composite( uint16_t *mem_map, PIO *ppio, uint16_t address, uint16_t system )
{
    // Configure PIO
    //
    PIO pio = *ppio;

    // Create and configure state machines
    //
    // * cvsync_sm generates HSYNC and VSYNC pulses
    // * cvdata_sm outputs video data
    //

    int cvdata_sm  = video_create_cvdata_sm( pio );
    int cvsync_sm  = video_create_cvsync_sm( pio, system );

    // Configure the DMA channels
    //
    // * Channel cvdata_dma:        Moves data from the video memory area in 16bit words, swaps bytes and places into TX FiFo higher bytes. Chains to cvdata_rearm_dma
    // * Channel cvdata_rearm_dma:  Reconfigures cvdata_dma and launchs it again.
    //
    int cvdata_dma          = dma_claim_unused_channel( true );
    int cvdata_rearm_dma    = dma_claim_unused_channel( true );

    // Init control block
    video_set_mem_start( address );

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

// We need to declare the DMA channels and control blocks here, as they are
// needed in the IRQ handler
//
static int vgadata_control_dma;
static int vgadata_line_dma;

// Control block with read addresses and lengths
// 480 entries for max vertical resolution (640x480)
// Add 2 * 400 entries for vertical bars for wide modes (768x400 and 720x400) 
// Add 1 entry for signaling the end of the control block
// 
static struct {uint32_t count; uint32_t addr;} control_blocks[ 480 + 2*400 + 1 ];

// IRQ handler
void __not_in_flash_func( video_vga_rearm_dma )()
{
	// Clear the interrupt request for the DMA data channel
	dma_hw->ints1 = ( 1u << vgadata_line_dma );

	// update DMA control channel with the first address and transfer count and run it
	dma_channel_set_read_addr( vgadata_control_dma, &control_blocks[0].addr, false );
	dma_channel_set_trans_count( vgadata_control_dma, control_blocks[0].count, true );
}

static void video_setup_vga( uint16_t *mem_map, PIO *ppio, uint16_t address, uint16_t system )
{
    PIO pio = *ppio;
    static vga_mode_data_t vga_mode_data[ MODE_NUM ];

    init_vga_modes( pio, vga_mode_data );

    vga_mode_data_t *data = &vga_mode_data[ system_to_vga_mode( system ) ];

    // Create and configure state machines
    //
    // * vgahsync_sm generates HSYNC pulses
    // * vgavsync_sm generates VSYNC pulses
    // * vgadata_sm outputs video data
    //
    int vgahsync_sm = video_create_vgahsync_sm( pio, data );
    int vgavsync_sm = video_create_vgavsync_sm( pio, data );
    int vgadata_sm  = video_create_vgadata_sm( pio, data );

    // DMA channels:
    // vgadata_line_dma:    Sends 40 bytes to PIO (one line)
    // vgadata_control_dma: Controls line repetition
    //
    vgadata_line_dma    = dma_claim_unused_channel( true );
    vgadata_control_dma = dma_claim_unused_channel( true );

    video_set_mem_start( address );

    // Generate control block
    //
    static uint16_t blank_line[48] = { 0 };             // Max length for 768x400 is 768/2/8
    uint32_t blank_addr = (uint32_t)&blank_line[0];
    uint32_t blank_count = (uint32_t)(data->h_size / 16);

    uint16_t block_idx = 0;

    // First, blank lines, if any
    for ( int blank_line = 0; blank_line < data->h_bar_size; ++blank_line )
    {
        control_blocks[block_idx].addr  = blank_addr;
        control_blocks[block_idx].count = blank_count;
        ++block_idx;
        control_blocks[block_idx].addr  = blank_addr;
        control_blocks[block_idx].count = blank_count;
        ++block_idx;
    }

    // Video lines
    uint32_t video_base = (uint32_t)video_mem_start;
    bool has_vertical_bars = data->v_bar1_size ? true : false;
    uint32_t v_bar1_count = (uint32_t)(data->v_bar1_size/16);
    uint32_t v_bar2_count = (uint32_t)(data->v_bar2_size/16);

    for ( int video_line = 0; video_line < VIDEO_LINES; ++video_line )
    {
        uint32_t line_addr = video_base + video_line * 80;

        for ( int scan = 0; scan < 2; ++scan )
        {
            if ( has_vertical_bars )
            {
                control_blocks[block_idx].addr  = blank_addr;
                control_blocks[block_idx].count = v_bar1_count;
                ++block_idx;
            }
            control_blocks[block_idx].addr      = line_addr;
            control_blocks[block_idx].count     = VIDEO_PIX_PER_LINE / 8;
            ++block_idx;
            if ( has_vertical_bars )
            {
                control_blocks[block_idx].addr  = blank_addr;
                control_blocks[block_idx].count = v_bar2_count;
                ++block_idx;
            }
        }
    }

    // Last blank lines
    for ( int blank_line = 0; blank_line < data->h_bar_size; ++blank_line )
    {
        control_blocks[block_idx].addr  = blank_addr;
        control_blocks[block_idx].count = blank_count;
        ++block_idx;
        control_blocks[block_idx].addr  = blank_addr;
        control_blocks[block_idx].count = blank_count;
        ++block_idx;
    }

    control_blocks[block_idx].count = 0;                        // Mark end of block

    dma_channel_config vgadata_line_dma_config = dmacfg_config_channel(
                vgadata_line_dma,
                false,                                  // Mark as normal priority
                true,                                   // Generate interrupt when a trigger is set to 0
                pio_get_dreq( pio, vgadata_sm, true ),  // Signals data transfer from PIO, transmit
                DMA_SIZE_16,
                vgadata_control_dma,                    // Chains to vgadata_control_dma
                (uint16_t *)&pio->txf[vgadata_sm]+1,    // Writes to the higher bytes of cvdata_sm TX FiFo
                video_mem_start,                        // Reads from mem_map (overwritten by vgadata_control_dma)
                0,                                      // Transfer size (overwritten by vgadata_control_dma)
                true,                                   // Enable byte swapping
                false,                                  // Do not increment write addr
                true,                                   // Increment read addr
                false                                   // Do not start
                );

    // Configure the processor to run video_vga_rearm_dma() when DMA IRQ 0 is asserted

    dma_channel_set_irq1_enabled( vgadata_line_dma, true );
    irq_set_exclusive_handler( DMA_IRQ_1, video_vga_rearm_dma );
    // set highest IRQ priority
    irq_set_priority( DMA_IRQ_1, 0 );
    irq_set_enabled( DMA_IRQ_1, true );

    dma_channel_config vgadata_control_dma_config = dmacfg_config_channel(
                vgadata_control_dma,
                false,                                  // Mark as normal priority
                false,                                  // Do not generate interrupts
                DREQ_FORCE,                             // Permanent request transfer
                DMA_SIZE_32,
                vgadata_control_dma,                    // Do not chain
                &dma_hw->ch[vgadata_line_dma].al3_transfer_count, // Writes to read address trigger of data channel
                NULL,                                   // Initial read address (set by the IRQ handler)
                2,                                      // Halt after each control block
                false,                                  // Don't do byte swapping
                false,                                  // Do not increment write addr
                true,                                   // Increment read addr
                false                                   // Do not start
                );

    // Launch control channel
    video_vga_rearm_dma();

    // Start state machines in sync
    pio_enable_sm_mask_in_sync( pio, ((1u << vgahsync_sm) | (1u << vgavsync_sm) | (1u << vgadata_sm)) );
}

void video_setup( uint16_t *mem_map )
{

    PIO pio = pio1;

    video_gpio_pins( pio );

    if ( config.video.system > 1 )
    {
        video_setup_vga( mem_map, &pio, config.video.address, config.video.system );
    }
    else
    {
        video_setup_composite( mem_map, &pio, config.video.address, config.video.system );
    }
}
