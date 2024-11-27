/*
 * memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * YAML parsing functions
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

#ifndef MEMCFG_YAML_H
#define MEMCFG_YAML_H

#include <stdint.h>
#include <stdbool.h>

#include "globals.h"

#define COUNTRY_CODE_SIZE   4
#define SSID_SIZE           32
#define PASSWORD_SIZE       64
#define FILE_NAME_SIZE      64

#define MAX_DRIVES          4

#include "scan.h"

typedef struct {
    struct {
        char country[COUNTRY_CODE_SIZE];
        char ssid[SSID_SIZE];
        char password[PASSWORD_SIZE];
    } __attribute__((packed)) wifi;
    struct {
        uint16_t system;
        uint16_t address;
        // uint16_t output;
    } __attribute__((packed)) video;
    struct {
        uint8_t enabled;
        uint8_t optswitch;
        uint16_t usrram;
        uint16_t sysram;
        struct {
            char file[FILE_NAME_SIZE];
            uint8_t ro;
        } __attribute__((packed)) disk[MAX_DRIVES];
    } __attribute__((packed)) fdc;
} __attribute__((packed)) config_doc_t;

typedef struct memory_doc_s {
    struct {
        bool start;
        bool end;
        bool enabled;
        bool ro;
        bool fill;
    } flags;
    uint16_t start;
    uint16_t end;
    uint32_t count;
    bool enabled;
    bool ro;
    uint8_t fill;
    struct {
        uint8_t *value;
        size_t length;
    } data;
    char *file;
    struct memory_doc_s *next;
} memmap_doc_t;

status_t parse_config( char *filename, config_doc_t *document );
status_t parse_memmap( char *filename, memmap_doc_t **document );
void free_memmap( memmap_doc_t *memmap );

#endif
