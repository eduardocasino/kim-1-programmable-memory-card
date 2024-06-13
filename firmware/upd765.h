/*
 * K-1013 Floppy Disc Controller emulation for the KIM-1 Programmable Memory Board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * NEC uPD765 definitions
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

#ifndef UPD765_H
#define UPD765_H

#define MAX_DRIVES      4

// uPD765 Main Status Register bits
#define RQM_FLAG        (uint8_t)( 1 << 7 )
#define DIR_FLAG        (uint8_t)( 1 << 6 )
#define BSY_FLAG        (uint8_t)( 1 << 4 )

#define CMD_MASK                0b00011111
#define CMD_OPT_MASK            0b11100000
#define CMD_MT_FLAG             0b10000000
#define CMD_MF_FLAG             0b01000000
#define CMD_SK_FLAG             0b00100000

#define CMD1_DRIVE_NUMBER_MASK  0b00000011
#define CMD1_HEAD_MASK          0b00000100
#define CMD1_HEAD_FLAG_POS      2


// ST0 flags:
#define ST0_TERMINATION_MASK    0b11000000
#define ST0_SEEK_END_MASK       0b00100000
#define ST0_EC_MASK             0b00010000
#define ST0_NOT_READY_MASK      0b00001000
#define ST0_HEAD_MASK           0b00000100
#define ST0_DRIVE_NUMBER_MASK   0b00000011

#define ST0_HEAD_FLAG_POS       2

#define ST0_NORMAL_TERM         0b00000000
#define ST0_ABNORMAL_TERM       0b01000000
#define ST0_INVALID_CMD         0b10000000
#define ST0_READY_CHANGED       0b11000000
#define ST0_NOT_READY           0b00001000

#define ST1_NO_DATA             0b00000100
#define ST1_EN                  0b10000000
// This is unused in the real controller. We use it to mark a DMA error:
#define ST1_DM                  0b01000000
#define ST1_DE                  0b00100000
#define ST1_ND                  0b00000100
#define ST1_NW                  0b00000010
#define ST1_MA                  0b00000001

#define ST2_CM                  0b01000000
#define ST2_DD                  0b00100000
#define ST2_MD                  0b00000001

#define ST3_FT                  0b10000000
#define ST3_WP                  0b01000000
#define ST3_RY                  0b00100000
#define ST3_T0                  0b00010000
#define ST3_TS                  0b00001000
#define ST3_HEAD_ADDRESS_MASK   0b00000100
#define ST3_DRIVE_NUMBER_MASK   0b00000011

typedef struct {
    uint8_t cyl;
    uint8_t head;
    uint8_t sect;
    uint8_t nbytes;    
} upd765_format_buf_t;

typedef enum { READ, WRITE, SCAN } upd765_cmd_t;
typedef enum { NORMAL_DATA, DELETED_DATA } upd765_data_mode_t;

#endif /* UPD765_H */