/*
 * DMA channels configuration for the KIM-1 Programmable Memory Board
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

#include "hardware/dma.h"

dma_channel_config dmacfg_config_channel(
                int channel,
                bool high_priority,
                bool quiet,
                uint dreq,
                enum dma_channel_transfer_size size,
                int chain,
                volatile void *write_addr,
                const volatile void *read_addr,
                uint transfer_count,
                bool byte_swap,
                bool write_increment,
                bool read_increment,
                bool trigger
                )
{
    dma_channel_config dma_config = dma_channel_get_default_config( channel );  // Get default config structure

    channel_config_set_high_priority( &dma_config, high_priority );
    channel_config_set_irq_quiet ( &dma_config, quiet );                        // generate interrupts?
    channel_config_set_dreq( &dma_config, dreq );
    channel_config_set_transfer_data_size( &dma_config, size );
    channel_config_set_write_increment( &dma_config, write_increment );
    channel_config_set_read_increment( &dma_config, read_increment );
    channel_config_set_chain_to( &dma_config, chain );
    channel_config_set_bswap( &dma_config, byte_swap );
    dma_channel_configure(  channel,
                            &dma_config,
                            write_addr,
                            read_addr,
                            transfer_count,                                     // Do 1 transfer
                            trigger );

    return dma_config;
}

