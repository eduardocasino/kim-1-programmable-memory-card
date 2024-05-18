/*
 * DMA channels configuration for the KIM-1 Programmable Memory Board
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

#include "hardware/dma.h"

dma_channel_config dmacfg_config_channel(
                int channel,
                uint dreq,
                enum dma_channel_transfer_size size,
                int chain,
                volatile void *write_addr,
                const volatile void *read_addr,
                bool trigger
                )
{
    dma_channel_config dma_config = dma_channel_get_default_config( channel );  // Get default config structure

    channel_config_set_high_priority( &dma_config, true );
    channel_config_set_irq_quiet ( &dma_config, true );                         // Do not generate interrupts
    channel_config_set_dreq( &dma_config, dreq );
    channel_config_set_transfer_data_size( &dma_config, size );
    channel_config_set_read_increment( &dma_config, false );                    // No increment, stay on same address
    channel_config_set_chain_to( &dma_config, chain );
    dma_channel_configure(  channel,
                            &dma_config,
                            write_addr,
                            read_addr,
                            1,                                                  // Do 1 transfer
                            trigger );

    return dma_config;
}

