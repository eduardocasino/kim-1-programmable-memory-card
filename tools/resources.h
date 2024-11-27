
/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * Web server resource paths
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

#ifndef MEMCFG_RESOURCES_H
#define MEMCFG_RESOURCES_H

typedef enum { 
    RES_RANGE = 0,
    RES_RANGE_DATA,
    RES_RANGE_ENABLE,
    RES_RANGE_DISABLE,
    RES_RANGE_SETROM,
    RES_RANGE_SETRAM,
    RES_RESTORE,
    RES_VIDEO
} resource_t;

const char *get_resource_path( resource_t resource );

#endif