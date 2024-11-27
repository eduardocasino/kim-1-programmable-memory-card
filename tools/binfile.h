/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Support functions for binary file formats
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

#ifndef MEMCFG_BINFILE_H
#define MEMCFG_BINFILE_H

#include <stdio.h>
#include <stdint.h>

#include "globals.h"

status_t binfile_bin_write( FILE *file, const uint8_t *data, size_t size, uint64_t base_addr );
status_t binfile_prg_write( FILE *file, const uint8_t *data, size_t size, uint64_t base_addr );
status_t binfile_raw_write( FILE *file, const uint8_t *data, size_t size, uint64_t base_addr );

status_t binfile_bin_read( FILE *file, uint8_t *data, size_t size, mem_block_t **blocks );
status_t binfile_prg_read( FILE *file, uint8_t *data, size_t size, mem_block_t **blocks );

#endif