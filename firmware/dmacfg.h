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

#ifndef DMACFG_H
#define DMACFG_H

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
                );

#endif /* DMACFG_H */
