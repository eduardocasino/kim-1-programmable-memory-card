/*
 * PIN definitions for the KIM-1 Programmable Memory Board
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

#ifndef PINS_H
#define PINS_H

#define A0       0
#define A1       1
#define A2       2
#define A3       3
#define A4       4
#define A5       5
#define A6       6
#define A7       7
#define A8       8
#define A9       9
#define A10     10
#define A11     11
#define A12     12
#define A13     13
#define A14     14
#define A15     15

#define D0       0
#define D1       1
#define D2       2
#define D3       3
#define D4       4
#define D5       5
#define D6       6
#define D7       7

#define CE      20
#define RW      21
#define PHI2    22

#define VSYNC   26
#define HSYNC   27
#define VIDEO   28

#define TX      19
#define SCK     18
#define CSn     17
#define RX      16

#define PIN_BASE_ADDR   A0
#define PIN_BASE_DATA   D0

#endif /* PINS_H */