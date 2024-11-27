/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Scanning support functions
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

#ifndef MEMCFG_SCAN_H
#define MEMCFG_SCAN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { NTSC = 0, PAL } video_system_t;
typedef enum { COMPOSITE = 0, VGA } video_output_t;

int get_hexbyte( const char *s, uint8_t *byte );
int get_hexword( const char *s, uint16_t *word );
int get_octbyte( const char *s, uint8_t *byte );
int get_uint16( const char *string, uint16_t *value );
int get_uint32( const char *string, uint32_t *value );
int get_boolean( const char *string, bool *value );
int get_country_code( const char *string, char *value );
int get_video_system( const char *string, uint16_t *value );
int get_video_output( const char *string, uint16_t *value );
int get_memory_type( const char *string, bool *value );

#endif