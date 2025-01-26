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

// uPD765 commands
#define READ_TRACK  2
#define SPECIFY     3
#define SENSE_DRIVE 4
#define WRITE       5
#define READ        6
#define RECALIBRATE 7
#define SENSE_INT   8
#define WRITE_DEL   9
#define READ_ID     10
#define READ_DEL    12
#define FORMAT      13
#define SEEK        15
#define SCAN_EQ     17
#define SCAN_LE     25
#define SCAN_GE     29

// Command string and result lengths
#define SPECIFY_CMD_LEN     3
#define SPECIFY_RES_LEN     0
#define SENSE_DRIVE_CMD_LEN 2
#define SENSE_DRIVE_RES_LEN 1
#define SENSE_INT_CMD_LEN   1
#define SENSE_INT_RES_LEN   2
#define READ_CMD_LEN        9
#define READ_RES_LEN        7
#define READ_ID_CMD_LEN     2
#define READ_ID_RES_LEN     7
#define SEEK_CMD_LEN        3
#define SEEK_RES_LEN        0
#define RECALIBRATE_CMD_LEN 2
#define RECALIBRATE_RES_LEN 0
#define FORMAT_CMD_LEN      6
#define FORMAT_RES_LEN      7
#define EXT_CMD_LEN         2
#define EXT_RES_LEN         2


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
#define ST2_WC                  0b00010000
#define ST2_MD                  0b00000001

#define ST3_FT                  0b10000000
#define ST3_WP                  0b01000000
#define ST3_RY                  0b00100000
#define ST3_T0                  0b00010000
#define ST3_TS                  0b00001000
#define ST3_HEAD_ADDRESS_MASK   0b00000100
#define ST3_DRIVE_NUMBER_MASK   0b00000011

// Extended command set
#define EXT_CMD_DIR         0b00000000
#define EXT_CMD_NXT         0b00000100
#define EXT_CMD_MNTS        0b00001000
#define EXT_CMD_NXT_MNT     0b00001100
#define EXT_CMD_NEW         0b00010000
#define EXT_CMD_ERA         0b00010100
#define EXT_CMD_CPY         0b00011000
#define EXT_CMD_MOV         0b00011100
#define EXT_CMD_MNT         0b00100000
#define EXT_CMD_UMNT        0b00100100
#define EXT_CMD_MASK        0b11111100
#define EXT_DRIVE_MASK      0b00000011


#define ST4_UNSUPPORTED     0b10000000
#define ST4_ABNORMAL_TERM   0b01000000
#define ST4_INVALID_CMD     0b00100000
#define ST4_BAD_PARAM       0b00010000
#define ST4_NOT_FOUND       0b00001000
#define ST4_INVLD_DRV       0b00000100
#define ST4_NOT_OPEN        0b00000010
#define ST4_NO_DATA         0b00000001
#define ST4_NORMAL_TERM     0b00000000

#define ST5_DRV_MOUNTED     0b00000001
#define ST5_DRV_NOT_MOUNTED 0b00000010
#define ST5_IMG_MOUNTED     0b00000100
#define ST5_IMG2_MOUNTED    0b00001000
#define ST5_IMG_NAME        0b00010000
#define ST5_IMG_INVALID     0b00100000
#define ST5_IMG_EXISTS      0b01000000
#define ST5_DISK_FULL       0b10000000

typedef struct {
    uint8_t cyl;
    uint8_t head;
    uint8_t sect;
    uint8_t nbytes;    
} upd765_format_buf_t;

typedef enum { NORMAL_DATA, DELETED_DATA } upd765_data_mode_t;

#endif /* UPD765_H */