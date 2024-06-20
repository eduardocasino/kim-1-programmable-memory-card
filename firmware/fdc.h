/*
 * K-1013 Floppy Disc Controller emulation for the KIM-1 Programmable Memory Board
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

#ifndef FDC_H
#define FDC_H

#include <stdint.h>
#include "pico/sem.h"

#include "ff.h"
#include "upd765.h"
#include "imd.h"

// K-1013 control registers offsets
//
#define FDC_HARDWARE_STATUS_REGISTER_OFF    0x1FE8
#define FDC_DMA_ADDRESS_REGISTER_OFF        0x1FEA
#define FDC_DEBUG_REGISTER_OFF              0x1FEC
#define FDC_MAIN_STATUS_REGISTER_OFF        0x1FEE
#define FDC_DATA_REGISTER_OFF               0x1FEF

// K-1013 State Machine definitions
//
typedef enum { UDR_READ, UDR_WRITE, INVALID } fdc_event_t;
typedef enum { FDC_IDLE, FDC_BUSY, FDC_COMMAND, FDC_STATUS, FDC_END  } fdc_state_t;
typedef enum { INT_NONE = 0, INT_SEEK, INT_COMMAND, INT_INVALID, INT_ATTENTION } fdc_interrupt_t;

#define NUM_CMDS            32

struct fdc_sm_s;

typedef fdc_state_t(*fdc_sm_func_t)( struct fdc_sm_s *fdc, fdc_event_t event );

typedef fdc_state_t(*disk_command_fn_t)( struct fdc_sm_s *fdc );

#define FDC_MAX_CMD_LEN     9
#define FDC_MAX_RES_LEN     7
typedef struct {
    // Command request / result byte array
    uint8_t data[MAX( FDC_MAX_CMD_LEN, FDC_MAX_RES_LEN)];

    // Pending cmd bytes/result bytes to receive/send 
    int     cmd_bytes;
    int     res_bytes;

    // Pointer to the next data byte
    uint8_t *dp;
    
    disk_command_fn_t function;
} fdc_cmd_t;

typedef struct {
    int                 command_len;            // Command bytes - 1
    int                 result_len;             // Result bytes
    disk_command_fn_t   command_fn;
} fdc_cmd_table_t;

// K-1013 controller object
//
typedef struct fdc_sm_s {

    semaphore_t sem;                            // Syncronization semaphore
    fdc_event_t last_event;

    imd_sd_t sd;

    uint8_t *buffer;

    // User and system memory block start addresses;
    uint16_t user_block;
    uint16_t system_block;

    bool opt_switch;

    uint8_t HSR_save;
    // Hardware Status Read
    #define IRQREQ_FLAG     (uint8_t)( 1 << 7 )
    #define OPTSWT_FLAG     (uint8_t)( 1 << 6 )

    // Hardware Control Write
    #define DMADIR_FLAG     (uint8_t)( 1 << 0 )
    #define WRPROT_FLAG     (uint8_t)( 1 << 1 )
    #define IRQENA_FLAG     (uint8_t)( 1 << 2 )

    uint8_t *HSR;

    // DMA Address Register
    #define SYSTEM_FLAG     0b10000000
    #define ODD_FLAG        0b01000000
    #define ADDR_MASK       0b01111111
    uint8_t *DAR;

    uint8_t *MSR;

    // uPD765 Data Register
    uint8_t *UDR;
    
    // DEBUG REGISTER. FIXME: PUT BETWEEN IFDEFS
    uint8_t *DBR;

    fdc_state_t state;

    fdc_cmd_t command;          // Current command

    fdc_cmd_table_t *cmd_table;

    fdc_interrupt_t interrupt;  // Flag: set FDC interrupt
    uint8_t seek_result[2];

} fdc_sm_t;


void fdc_set_dma_write_channel( int channel );
void fdc_set_read_addr( io_rw_32 *addr );
void fdc_setup( uint16_t *mem_map );

#endif /* FDC_H */