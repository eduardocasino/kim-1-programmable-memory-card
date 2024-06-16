/*
 * Memory emulation PIO configuration for the KIM-1 Programmable Memory Board
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

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "mememul.pio.h"

#include "config.h"
#include "pins.h"
#include "dmacfg.h"
#include "fdc.h"

static void mememul_gpio_pins( PIO pio )
{
    // Configure #CE , #R/W and 02 GPIOs
    //
    pio_gpio_init( pio, CE );

    pio_gpio_init( pio, RW );
    gpio_pull_up ( RW );

    pio_gpio_init( pio, PHI2 );
    gpio_pull_up ( PHI2 );

    // Configure address (and data) bus pins
    //
    for ( int pin = PIN_BASE_ADDR; pin < PIN_BASE_ADDR+16; ++pin ) {
        pio_gpio_init( pio, pin );
        gpio_pull_down ( pin );
    }
}

static int mememul_create_memread_sm( PIO pio )
{
    int memread_sm      = pio_claim_unused_sm( pio, true );                                 // Claim a free state machine for memory emulation on PIO 0
    uint memread_offset = pio_add_program( pio, &memread_program );                         // Instruction memory offset for the SM

    pio_sm_config memread_config = memread_program_get_default_config( memread_offset );    // Get default config for the memory emulation SM

    sm_config_set_in_pins ( &memread_config, PIN_BASE_ADDR );                               // Pin set for IN and GET instructions
    sm_config_set_out_pins( &memread_config, PIN_BASE_DATA, 8 );                            // Pin set for OUT instructions
    sm_config_set_jmp_pin ( &memread_config, RW );                                          // Pin for conditional JMP instructions
    sm_config_set_set_pins( &memread_config, CE, 1 );                                       // Pin set for SET instructions

    sm_config_set_in_shift ( &memread_config, false, true, 16+1 );                          // Shift left address bus and additional 0 from ISR,
                                                                                            // autopush resultant 32bit address to DMA RXF
    sm_config_set_out_shift( &memread_config, true, false, 10 );                            // Shift right 10 bits to OSR: DATA + CE + RW, no autopull

    pio_sm_set_consecutive_pindirs( pio, memread_sm, PIN_BASE_ADDR, 16, false );            // Set address bus pins as inputs
    pio_sm_set_pindirs_with_mask( pio, memread_sm, (1 << CE), (1 << CE)|(1 << RW)|(1 << PHI2) ); // Set CE as output, RW and PHI2 as input
    pio_sm_set_pins_with_mask( pio, memread_sm, (1 << CE), (1 << CE) );                     // Ensure CE is disabled by default

	if ( config.fdc.enabled )
    {
        pio_set_irq0_source_enabled(pio, pis_interrupt0, true);                                 // Make this SM the exclusive source for PIO0_IRQ_0;
    }

    pio_sm_init( pio, memread_sm, memread_offset, &memread_config );

    return memread_sm;
}

static int mememul_create_memwrite_sm( PIO pio )
{
    int memwrite_sm      = pio_claim_unused_sm( pio, true );                                // Claim a free state machine for memory write on PIO 0
    uint memwrite_offset = pio_add_program( pio, &memwrite_program );                       // Instruction memory offset for the SM

    pio_sm_config memwrite_config = memwrite_program_get_default_config( memwrite_offset ); // Get default config for the memory write SM

    sm_config_set_in_pins(  &memwrite_config, PIN_BASE_DATA );                              // Pin set for IN and GET instructions.

    sm_config_set_in_shift( &memwrite_config, false, true, 8 );                             // Shift left DATA into ISR, autopush

    pio_interrupt_clear( pio, WRITE_IRQ );                                                  // Ensure that the write IRQ is cleared at start

    pio_sm_init( pio, memwrite_sm, memwrite_offset, &memwrite_config );

    return memwrite_sm;
}

void mememul_setup( uint16_t *mem_map )
{
    // Configure PIO
    //
    PIO pio = pio0;

    mememul_gpio_pins( pio );

    // Create and configure state machines
    //
    // * memread_sm performs the read operations and, when a write is requested, checks if memory is writable and, if so, starts handles control to memwrite_sm
    // * memwrite_sm performs the write operation and returns control to memread_sm
    //

    int memwrite_sm     = mememul_create_memwrite_sm( pio );
    int memread_sm      = mememul_create_memread_sm ( pio );

    // Configure the DMA channels
    //
    // * Channel read_addr_dma:  Moves address from memread_sm RX FiFo ( addr bus combined with the mem_map base address) to the read_data_dma channel config read address. Chains to write_addr_dma
    // * Channel write_addr_dma: Moves address from read_data_dma channel config read address to the write_data_dma channel config write trigger address. Chains to read_data_dma.
    // * Channel read_data_dma:  Moves data from mem_map (as previosly set by read_addr_dma) to memread_sm TX FiFo. Chains to read_addr_dma
    // * Channel write_data_dma: Moves data from memwrite_sm RX FiFo to the mem_map addr configured by write_addr_dma. Does not chain.
    //
    int read_addr_dma   = dma_claim_unused_channel( true );
    int read_data_dma   = dma_claim_unused_channel( true );
    int write_addr_dma  = dma_claim_unused_channel( true );
    int write_data_dma  = dma_claim_unused_channel( true );

    dma_channel_config write_data_dma_config = dmacfg_config_channel(
                write_data_dma,
                true,                                                       // Mark as high priority
                !config.fdc.enabled,                                        // Generate interrupts if fdc is enabled
                pio_get_dreq( pio, memwrite_sm, false ),                    // Signals data transfer from PIO, receive
                DMA_SIZE_8,
                write_data_dma,                                             // Does not chain (chain to itself means no chain)
                mem_map,                                                    // Writes to mem_map (efective address configured by write_addr_dma)
                &pio->rxf[memwrite_sm],                                     // Reads from memwrite_sm RX FiFo
                1,                                                          // Transfer 1 byte
                false,                                                      // Don't do byte swapping
                false,                                                      // Do not increment write addr
                false,                                                      // Do not increment read addr
                false                                                       // Does not start
                );

    dma_channel_config read_data_dma_config = dmacfg_config_channel(
                read_data_dma,
                true,                                                       // Mark as high priority
                true,                                                       // Quiet, do not generate interrupts
                pio_get_dreq( pio, memread_sm, true ),                      // Signals data transfer from PIO, transmit
                DMA_SIZE_16,
                read_data_dma,                                              // Does not chain
                &pio->txf[memread_sm],                                      // Writes memread_sm TX FiFo
                mem_map,                                                    // Reads from mem_map (efective address configured by read_addr_dma)
                1,                                                          // Transfer 1 word
                false,                                                      // Don't do byte swapping
                false,                                                      // Do not increment write addr
                false,                                                      // Do not increment read addr
                false                                                       // Does not start
                );

    dma_channel_config write_addr_dma_config = dmacfg_config_channel(
                write_addr_dma,
                true,                                                       // Mark as high priority
                true,                                                       // Quiet, do not generate interrupts
                DREQ_FORCE,                                                 // Permanent request transfer
                DMA_SIZE_32,
                read_addr_dma,                                              // Chains to read_addr_dma when finished
                &dma_channel_hw_addr(write_data_dma)->al2_write_addr_trig,  // Writes to write address trigger of write_data_dma
                &dma_channel_hw_addr(read_data_dma)->read_addr,             // Reads from read address of read_data_dma
                1,                                                          // Transfer 1 dword
                false,                                                      // Don't do byte swapping
                false,                                                      // Do not increment write addr
                false,                                                      // Do not increment read addr
                false                                                       // Does not start
                );

    dma_channel_config read_addr_dma_config = dmacfg_config_channel(
                read_addr_dma,
                true,                                                       // Mark as high priority
                true,                                                       // Quiet, do not generate interrupts
                pio_get_dreq( pio, memread_sm, false ),                     // Signals data transfer from PIO, receive
                DMA_SIZE_32,
                write_addr_dma,                                             // Chains to read_data_dma when finished
                &dma_channel_hw_addr(read_data_dma)->al3_read_addr_trig,    // Writes to read address trigger of read_data_dma
                &pio->rxf[memread_sm],                                      // Reads from memread_sm RX FiFo
                1,                                                          // Transfer 1 dword
                false,                                                      // Don't do byte swapping
                false,                                                      // Do not increment write addr
                false,                                                      // Do not increment read addr
                true                                                        // Starts immediately
                );

    if ( config.fdc.enabled )
    {
        fdc_set_dma_write_channel( write_data_dma );
        fdc_set_read_addr( &dma_channel_hw_addr(read_data_dma)->read_addr );

        dma_channel_set_irq0_enabled( write_data_dma, true );

    }

    // Enable State Machines
    //
    pio_sm_set_enabled( pio, memwrite_sm, true );

    // Put mem_map base address shifted 17 bits, so the 16 gpios + a '0' (multiplied by 2) gives the correct addr
    //
    pio_sm_put( pio, memread_sm, dma_channel_hw_addr( read_data_dma )->read_addr >> 17 );
    pio_sm_set_enabled( pio, memread_sm, true );

}

