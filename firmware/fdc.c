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

// Supported data record lengths: 128, 256, 512, 1024, 2048, 4096, 8192
// Supported modes: 00 (500kbps FM), 03 (500kbps MFM)
//

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "upd765.h"
#include "fdc.h"
#include "imd.h"
#include "config.h"
#include "pins.h"
#include "debug.h"

// Transfer buffer for dma operations
//
static uint8_t  fdc_disk_buffer[MAX_SECTOR_SIZE+4];    // 8Kb buffer plus some space before for the sector type mark

// K-1013 Floppy Disk Controller object
//
static fdc_sm_t fdc_sm = { 0x00 };

// DMA memory write channel. Must be initialized with fdc_set_dma_write_channel()
//
static int dma_write_channel;

// Read address of the DMA memory channels. Must be initializad by fdc_set_read_addr()
//
static io_rw_32 *read_addr;


static inline void fdc_set_ready( fdc_sm_t *fdc )
{
    *fdc->MSR |= RQM_FLAG;
}

static inline void fdc_set_notready( fdc_sm_t *fdc )
{
    *fdc->MSR &= ~RQM_FLAG;
}

static inline void fdc_set_busy( fdc_sm_t *fdc )
{
    *fdc->MSR |= BSY_FLAG;
}

static inline void fdc_set_notbusy( fdc_sm_t *fdc )
{
    *fdc->MSR &= ~BSY_FLAG;
}

static inline void fdc_update_hsr( fdc_sm_t *fdc, uint8_t newval )
{
    *fdc->HSR = newval;
    fdc->HSR_save = newval & 0b11000000;
}

static inline int fdc_command_phase( fdc_sm_t *fdc )
{
    // Get next byte
    *(fdc->command.dp++) = *fdc->UDR;

    return --fdc->command.cmd_bytes;
}

static inline void fdc_clear_interrupt( fdc_sm_t *fdc, fdc_interrupt_t int_type )
{
    if ( fdc->interrupt == int_type )
    {
        fdc_update_hsr( fdc, *fdc->HSR | IRQREQ_FLAG );
        fdc->interrupt = INT_NONE;
    }

    // TODO: If/when the pico controls the IRQ line, check that IRQENA_FLAG is set
    // and, in that case, generate an interrupt
}

static inline void fdc_raise_interrupt( fdc_sm_t *fdc )
{
    if ( fdc->interrupt != INT_NONE )
    {
        fdc_update_hsr( fdc, *fdc->HSR & ~IRQREQ_FLAG );
    }

    // TODO: If/when the pico controls the IRQ line, check that IRQENA_FLAG is set
    // and, in that case, generate an interrupt
}

static fdc_state_t fdc_cmd_unimplemented( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_unimplemented(Command: 0x%2.2X, 0x%4.4X)\n", *fdc->UDR, fdc->state );

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;

    // Put status into the uDP765 Data Register
    fdc->command.res_bytes = 1;
    *fdc->UDR = 0x80;

    fdc->state = FDC_STATUS;

    return fdc->state;
}

static fdc_state_t fdc_cmd_specify( fdc_sm_t *fdc )
{
    // W 0 0 0 0 0 0 1 1    Command code
    // W <-SRT-> <-HUT->
    // W <----HLT---->ND
    //
    // These parameters are meaningless for the emulation
    //
    debug_printf( DBG_DEBUG, "fdc_cmd_specify(): data[1] == 0x%2.2X, data[2] ==0x%2.2X\n",
                                    fdc->command.data[1], fdc->command.data[2] );

    *fdc->MSR &= ~DIR_FLAG;
    fdc->state = FDC_IDLE;

    return fdc->state;
}

#define CMD_SEEK            0
#define CMD_RECALIBRATE     1
static fdc_state_t _fdc_cmd_seek( fdc_sm_t *fdc, int called )
{
    fdc_set_busy( fdc );

    // W 0 0 0 0 0 1 1 1 Command codes.
    // W X X X X X 0 US1 US0
    //<------ NCN ------>       Cylinder
    //
    uint8_t fdd_no = fdc->command.data[1] & CMD1_DRIVE_NUMBER_MASK;   // Get drive number
    uint8_t head = 0;
    uint8_t cyl = called == CMD_SEEK ? fdc->command.data[2] : 0;

    fdc->seek_result[1] = imd_seek_track( &fdc->sd.disks[fdd_no], head, cyl );

    if ( fdc->seek_result[1] != cyl )
    {
        // Drive not ready.
        fdc->seek_result[0] = ST0_ABNORMAL_TERM | ST0_SEEK_END_MASK | ST0_EC_MASK;
    }
    else
    {
        fdc->seek_result[0] = ST0_NORMAL_TERM | ST0_SEEK_END_MASK;
    }

    fdc->seek_result[0] |= (head << ST0_HEAD_FLAG_POS) & ST0_HEAD_MASK;
    fdc->seek_result[0] |= fdd_no;

    *fdc->MSR &= ~DIR_FLAG;
    fdc->state = FDC_IDLE;

    fdc->interrupt = INT_SEEK;

    return fdc->state;
}

static fdc_state_t fdc_cmd_recalibrate( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_recalibrate(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_seek( fdc, CMD_RECALIBRATE );
}

static fdc_state_t fdc_cmd_seek( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_seek(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_seek( fdc, CMD_SEEK );
}

static uint16_t fdc_get_dma_addr( fdc_sm_t *fdc, uint16_t base_address )
{
    debug_printf( DBG_DEBUG, "fdc_get_dma_addr(): base_addr: 0x%4.4X, DAR = 0x%2.2X\n", base_address, *fdc->DAR );

    uint16_t dma_addr = base_address + (( *fdc->DAR & ADDR_MASK ) << 6);

    // FIXME:
    // Check that the DMA Address Register has a good value
    // I don't know exactly what the real K-1013 does in case it hasn't.
    // For now, just fail with an EC condition and we also mark it with the unused
    // bit 6 of ST1 for debug purposes
    //
    if (
            ( (base_address % 0x2000 == 0) && (dma_addr <  (base_address + 0x1000)) && (*fdc->DAR & ODD_FLAG != 0) )  // Block at even boundary, dma_addr starts in lower block and odd flag is set
        ||  ( (base_address % 0x2000 == 0) && (dma_addr >= (base_address + 0x1000)) && (*fdc->DAR & ODD_FLAG == 0) )  // Block at even boundary, dma_addr starts in upper block and odd flag is not set
        ||  ( (base_address % 0x2000 != 0) && (dma_addr <  (base_address + 0x1000)) && (*fdc->DAR & ODD_FLAG == 0) )  // Block at odd boundary,  dma_addr starts in lower block and odd flag is not set
        ||  ( (base_address % 0x2000 != 0) && (dma_addr >= (base_address + 0x1000)) && (*fdc->DAR & ODD_FLAG != 0) )  // Block at odd boundary,  dma_addr starts in upper block and odd flag is set
    )
    if ( (base_address % 0x2000 != 0) )
    {
        // Flip bit 12 of dma_addr
        if ( dma_addr & ( 1 << 12) )
        {
            dma_addr &= ~( 1 << 12 );
        }
        else
        {
            dma_addr |= ( 1 << 12 );
        }
    }

    debug_printf( DBG_DEBUG, "dma_addr: 0x%4.4X\n", dma_addr );

    return dma_addr;
}

static void fdc_init_data_command(
    fdc_sm_t *fdc,
    uint8_t *cmd,
    bool *mt,
    bool *mf,
    bool *sk,
    uint8_t *fdd_no,
    uint8_t *cyl,
    uint8_t *head,
    uint8_t *sect,
    uint8_t *nbytes,
    uint8_t *eot,
    uint8_t *dtl )
{
    *cmd     = fdc->command.data[0] & CMD_MASK;
    *mt      = fdc->command.data[0] & CMD_MT_FLAG;
    *mf      = fdc->command.data[0] & CMD_MF_FLAG;
    *sk      = fdc->command.data[0] & CMD_SK_FLAG;
    *fdd_no  = fdc->command.data[1] & CMD1_DRIVE_NUMBER_MASK;   // Get drive number
    *cyl     = fdc->command.data[2];                            // Cylinder
    *head    = fdc->command.data[3];                            // Head. Same as HD
    *sect    = fdc->command.data[4];                            // Sector id
    *nbytes  = fdc->command.data[5];                            // Bytes per sector
    *eot     = fdc->command.data[6];                            // Last sector to read
    *dtl     = fdc->command.data[8];                            // Bytes to read if N == 0

    // Initialise result with the  CHRN, Disk and head
    fdc->command.data[0] = ST0_NORMAL_TERM | ( *head << 2) | *fdd_no;
    fdc->command.data[1] = 0;
    fdc->command.data[2] = 0;
    fdc->command.data[3] = *cyl;
    fdc->command.data[4] = *head;
    fdc->command.data[5] = *sect;
    fdc->command.data[6] = *nbytes;
}

static fdc_state_t _fdc_cmd_read_write( fdc_sm_t *fdc, upd765_data_mode_t mode )
{
    fdc_set_busy( fdc );

    // W MT MF SK 0 0 1 1 0     Command codes
    // W X X X X X HD US1 US0
    // W <------- C ------- >   Sector ID information prior to command execution.
    // W <------- H ------- >   The 4 bytes are compared against header on floppy disk.
    // W <------- R ------- >
    // W <------- N ------- >
    // W <------ EOT ------ >
    // W <------ GPL ------ >
    // W <------ DTL ------ >
    //
    // MT -> Multitrac bit
    // MF -> FM (0) / MFM (1) bit
    // SK -> Skip deleted data address mark
    //
    uint8_t cmd, fdd_no, cyl, head, sect, nbytes, eot, dtl;
    bool mt, mf, sk;
    uint16_t base_address = *fdc->DAR & SYSTEM_FLAG ? fdc->system_block : fdc->user_block;
    uint16_t dma_addr = fdc_get_dma_addr( fdc, base_address );

    fdc_init_data_command( fdc, &cmd, &mt, &mf, &sk, &fdd_no, &cyl, &head, &sect, &nbytes, &eot, &dtl );

    debug_printf( DBG_DEBUG, "cmd = %d, mt= %d, mf = %d, sk = %d, fdd_no = %d, cyl = %d, head = %d, sect = %d, nbytes = %d, eot = %d, dtl = %d\n", cmd, mt, mf, sk, fdd_no, cyl, head, sect, nbytes, eot, dtl );
    if ( dma_addr == 0x0000 )
    {
        fdc->command.data[0] = ST0_ABNORMAL_TERM | ST0_EC_MASK;
        fdc->command.data[1] = ST1_DM;
    }
    else
    {
        // Calculate max dma size. Also, if the bank starts in an odd 4k boundary and
        // the dma_addr starts in the lower 4K bank, set the DMA limit in the 4K boundary.
        // FIXME: I have to study the DMA circuit in more detail, but I assume that if either
        // the upper limit or the 4K boundary in case of odd base addresses are crossed,
        // the DMA counter wraps. We will just stop reading into memory for now.
        //
        uint16_t max_dma_size = (uint16_t)(base_address + 0x2000 - dma_addr );

        if ( (base_address % 0x2000) && (dma_addr < (base_address + 0x1000)) )
        {
            max_dma_size -= 0x1000;
        }

        // If the DMA data direction iw wrong, do not transfer to/from memory
        // FIXME: Check what the real K-1013 does
        //
        bool do_copy = ( cmd == WRITE ) ? !(*fdc->HSR & DMADIR_FLAG) : *fdc->HSR & DMADIR_FLAG;

        if ( cmd == READ || cmd == READ_DEL )
        {
            imd_read_data( &fdc->sd.disks[fdd_no], fdc->buffer, mt, mf, sk, fdc->command.data,
                                        head, cyl, sect, nbytes, eot, dtl, mode,
                                        &mem_map[dma_addr],
                                        max_dma_size,
                                        do_copy );
        }
        else
        {
            imd_write_data( &fdc->sd.disks[fdd_no], fdc->buffer, mt, mf, sk, fdc->command.data,
                                        head, cyl, sect, nbytes, eot, dtl, mode,
                                        &mem_map[dma_addr],
                                        max_dma_size,
                                        do_copy );
        }
    }

    fdc->command.dp = fdc->command.data;

    // Put first byte of status
    *fdc->UDR = *fdc->command.dp;

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;
    fdc->state = FDC_STATUS;

    fdc->interrupt = INT_COMMAND;

    return fdc->state;
}

static fdc_state_t fdc_cmd_read( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_read(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_read_write( fdc, NORMAL_DATA );
}

static fdc_state_t fdc_cmd_read_deleted( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_read_deleted(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_read_write( fdc, DELETED_DATA );
}

static fdc_state_t fdc_cmd_write( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_write(0x%4.4X)\n", fdc->state );

    // Invalidate SK option
    fdc->command.data[0] &= ~CMD_SK_FLAG;

    return _fdc_cmd_read_write( fdc, NORMAL_DATA );
}

static fdc_state_t fdc_cmd_write_deleted( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_write_deleted(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_read_write( fdc, DELETED_DATA );
}

static fdc_state_t fdc_cmd_sense_int( fdc_sm_t *fdc )
{
    fdc_set_busy( fdc );

    debug_printf( DBG_DEBUG, "fdc_cmd_sense_int(0x%4.4X)\n", fdc->state );

    fdc_clear_interrupt( fdc, INT_SEEK );

    fdc->command.data[0] = fdc->seek_result[0];
    fdc->command.data[1] = fdc->seek_result[1];
    fdc->command.dp = fdc->command.data;

    // Put first byte of status
    *fdc->UDR = *fdc->command.dp;

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;
     fdc->state = FDC_STATUS;

    return fdc->state;
}

static fdc_state_t fdc_cmd_format_track( fdc_sm_t *fdc )
{
    fdc_set_busy( fdc );

    debug_printf( DBG_DEBUG, "fdc_cmd_format_track(0x%4.4X)\n", fdc->state );

    // W 0 MF 0 0 1 1 0 1     Command codes
    // W X X X X X HD US1 US0
    // W <------- N ------- >   Bytes/sector.
    // W <-------SC ------- >   Sectors/track.
    // W <------ GPL ------ >   Gap 3 (Ignored)
    // W <------- D ------- >   Filler byte.

    uint8_t mf = fdc->command.data[0] & CMD_MF_FLAG;
    uint8_t fdd_no  = fdc->command.data[1] & CMD1_DRIVE_NUMBER_MASK;   // Get drive number
    uint8_t head = (fdc->command.data[1] & CMD1_HEAD_MASK) >> ST0_HEAD_FLAG_POS;
    uint8_t nbytes  = fdc->command.data[2];     // Bytes per sector
    uint8_t nsect   = fdc->command.data[3];     // Sectors per track
    uint8_t filler  = fdc->command.data[5];     // Filler byte

    // Initialise result with the  CHRN, Disk and head
    fdc->command.data[0] = ST0_NORMAL_TERM | ( head << 2) | fdd_no;
    fdc->command.data[1] = 0;
    fdc->command.data[2] = 0;
    fdc->command.data[3] = 0;            // For the format command, these four bytes are ignored
    fdc->command.data[4] = 0;
    fdc->command.data[5] = 0;
    fdc->command.data[6] = 0;

    uint16_t base_address = *fdc->DAR & SYSTEM_FLAG ? fdc->system_block : fdc->user_block;
    uint16_t dma_addr = fdc_get_dma_addr( fdc, base_address );


    if ( dma_addr == 0x0000 )
    {
        fdc->command.data[0] = ST0_ABNORMAL_TERM | ST0_EC_MASK;
        fdc->command.data[1] = ST1_DM;
    }
    else
    {

        // Calculate max dma size. Also, if the bank starts in an odd 4k boundary and
        // the dma_addr starts in the lower 4K bank, set the DMA limit in the 4K boundary.
        // FIXME: I have to study the DMA circuit in more detail, but I assume that if either
        // the upper limit or the 4K boundary in case of odd base addresses are crossed,
        // the DMA counter wraps. We will just stop reading into memory for now.
        //
        uint16_t max_dma_size = (uint16_t)(base_address + 0x2000 - dma_addr );

        if ( (base_address % 0x2000) && (dma_addr < (base_address + 0x1000)) )
        {
            max_dma_size -= 0x1000;
        }

        bool do_copy = !(*fdc->HSR & DMADIR_FLAG);

        imd_format_track( &fdc->sd.disks[fdd_no], fdc->buffer, mf, fdc->command.data,
                                        head, nsect, nbytes, filler,
                                        &mem_map[dma_addr],
                                        max_dma_size,
                                        do_copy );
    }

    fdc->command.dp = fdc->command.data;

    // Put first byte of status
    *fdc->UDR = *fdc->command.dp;

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;
     fdc->state = FDC_STATUS;

    fdc->interrupt = INT_COMMAND;

    return fdc->state;

}

// Just return the info from the current track
//
static fdc_state_t fdc_cmd_read_id( fdc_sm_t *fdc )
{
    fdc_set_busy( fdc );

    debug_printf( DBG_DEBUG, "fdc_cmd_read_id(0x%4.4X)\n", fdc->state );

    // W MT MF 0 0 1 0 1 0     Command codes
    // W X X X X X HD US1 US0

    bool mf = fdc->command.data[0] & CMD_MF_FLAG;
    uint8_t fdd_no = fdc->command.data[1] & CMD1_DRIVE_NUMBER_MASK;

    imd_read_id( &fdc->sd.disks[fdd_no], mf, fdc->command.data );

    fdc->command.dp = fdc->command.data;

    // Put first byte of status
    *fdc->UDR = *fdc->command.dp;

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;
     fdc->state = FDC_STATUS;

    return fdc->state;
}

static fdc_state_t fdc_cmd_sense_drive( fdc_sm_t *fdc )
{
    fdc_set_busy( fdc );

    debug_printf( DBG_DEBUG, "fdc_cmd_sense_drive(0x%4.4X)\n", fdc->state );

    // W 0 0 0 0 0 1 0 0     Command codes
    // W X X X X X HD US1 US0

    uint8_t fdd_no = fdc->command.data[1] & CMD1_DRIVE_NUMBER_MASK;
    uint8_t head = (fdc->command.data[1] & CMD1_HEAD_MASK) >> ST0_HEAD_FLAG_POS;

    fdc->command.data[0] = head | fdd_no;

    imd_sense_drive( &fdc->sd.disks[fdd_no],  fdc->command.data );

    fdc->command.dp = fdc->command.data;

    // Put first byte of status
    *fdc->UDR = *fdc->command.dp;

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;
    fdc->state = FDC_STATUS;

    return fdc->state;
}

static fdc_cmd_table_t fdc_commands[NUM_CMDS] = {
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_unimplemented },    // READ A TRACK
    { SPECIFY_CMD_LEN - 1,      SPECIFY_RES_LEN,        fdc_cmd_specify },          // SPECIFY
    { SENSE_DRIVE_CMD_LEN -1,   SENSE_DRIVE_RES_LEN,    fdc_cmd_sense_drive },      // SENSE DRIVE
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_write },            // WRITE DATA
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_read },             // READ DATA
    { RECALIBRATE_CMD_LEN - 1,  RECALIBRATE_RES_LEN,    fdc_cmd_recalibrate },      // RECALIBRATE
    { SENSE_INT_CMD_LEN - 1,    SENSE_INT_RES_LEN,      fdc_cmd_sense_int },        // SENSE INT
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_write_deleted },    // WRITE DELETED DATA
    { READ_ID_CMD_LEN - 1,      READ_ID_RES_LEN,        fdc_cmd_read_id },          // READ ID
    { 0,                        1,                      fdc_cmd_unimplemented },
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_read_deleted },     // READ DELETED DATA
    { FORMAT_CMD_LEN - 1,       FORMAT_RES_LEN,         fdc_cmd_format_track },     // FORMAT TRACK
    { 0,                        1,                      fdc_cmd_unimplemented },
    { SEEK_CMD_LEN - 1,         SEEK_RES_LEN,           fdc_cmd_seek },             // SEEK
    { 0,                        1,                      fdc_cmd_unimplemented },
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_unimplemented },    // SCAN EQUAL
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_unimplemented },    // SCAN LOW OR EQUAL
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_unimplemented },    // SCAN HIGH OR EQUAL
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented }
};

static inline fdc_state_t fdc_get_status_byte( fdc_sm_t *fdc, fdc_event_t event )
{
    if ( --fdc->command.res_bytes )
    {
        // Get next byte
        *fdc->UDR = *(++fdc->command.dp);
    }
    else
    {
        *fdc->MSR &= ~DIR_FLAG;
        fdc->state = FDC_IDLE;
    }
    return fdc->state;
}

static inline fdc_state_t fdc_get_command_byte( fdc_sm_t *fdc, fdc_event_t event )
{
    *(fdc->command.dp++) = *fdc->UDR;

    if ( ! --fdc->command.cmd_bytes )
    {
        fdc->state = fdc->command.function( fdc );
    }

    return fdc->state;
}

static inline fdc_state_t fdc_init_command( fdc_sm_t *fdc, fdc_event_t event )
{
    fdc_cmd_table_t *cmd = &fdc->cmd_table[*fdc->UDR & CMD_MASK];

    fdc->command.function = cmd->command_fn;
    fdc->command.cmd_bytes = cmd->command_len;
    fdc->command.res_bytes = cmd->result_len;
    fdc->command.dp = fdc->command.data;

    *(fdc->command.dp++) = *fdc->UDR;

    if ( cmd->command_len )
    {
        fdc->state = FDC_COMMAND;
    }
    else
    {
        fdc->state = fdc->command.function( fdc );
    }
    return fdc->state;
}

static void __not_in_flash_func(fdc_disk_emulation)( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_disk_emulation()\n" );

    while ( true )
    {
        sem_acquire_blocking ( &fdc->sem );
        fdc_set_notready( fdc );

        fdc_event_t event = fdc->last_event;

        // Only for desperate debug. Takes too long!!!
        // debug_printf( DBG_DEBUG, "STATE: 0x%2.2X, EVENT: 0x%2.2X --> ", fdc->state, event );

        if ( fdc->state == FDC_STATUS )
        {
            fdc_clear_interrupt( fdc, INT_COMMAND );

            if ( event == UDR_READ )
            {
                fdc->state = fdc_get_status_byte( fdc, event );
            }
        }
        else if ( fdc->state == FDC_IDLE )
        {
            if ( event == UDR_WRITE )
            {
                fdc->state = fdc_init_command( fdc, event );
            }
        }
        else if ( fdc->state == FDC_COMMAND)
        {
            if ( event == UDR_WRITE )
            {
                fdc->state = fdc_get_command_byte( fdc, event );
            }
        }

        // Only for desperate debug. Takes too long!!!
        // debug_printf( DBG_DEBUG, "NEW STATE: 0x%2.2X\n", fdc->state );

        fdc_set_ready( fdc );

        if ( fdc->state == FDC_IDLE )
        {
            fdc_set_notbusy( fdc );
        }

        fdc_raise_interrupt( fdc );

    }
}

static void __not_in_flash_func(fdc_mem_write_interrupt_handler)( void )
{
    if ( *read_addr == (uint32_t)fdc_sm.HSR )
    {
        // Trick to preserve the two most significant bits when HSR is written
        *fdc_sm.HSR = ( *fdc_sm.HSR & 0b00111111 ) | fdc_sm.HSR_save;
    }
    else if ( *read_addr == (uint32_t)fdc_sm.UDR )
    {
        fdc_sm.last_event = UDR_WRITE;
        sem_release( &fdc_sm.sem );
    }

    // Acknowledge interrupt
    dma_hw->ints0 = 1u << dma_write_channel;

    return;
}

static void __not_in_flash_func(fdc_mem_read_interrupt_handler)( void )
{
    if ( *read_addr == (uint32_t)fdc_sm.UDR )
    {
        fdc_sm.last_event = UDR_READ;
        sem_release( &fdc_sm.sem );
    }

    // acknowledge interrupt
    pio0->irq = 1u << 0;

    return;
}

static void fdc_start( void )
{
    debug_printf( DBG_DEBUG, "fdc_start()\n");

	irq_set_exclusive_handler( PIO0_IRQ_0, fdc_mem_read_interrupt_handler );
	irq_set_enabled( PIO0_IRQ_0, true );
    irq_set_exclusive_handler( DMA_IRQ_0, fdc_mem_write_interrupt_handler );
    irq_set_enabled( DMA_IRQ_0, true );

    // Set maximum priority to the disk emulation IRQs
    irq_set_priority( PIO0_IRQ_0, 0 );
    irq_set_priority( DMA_IRQ_0, 0 );

    fdc_disk_emulation( &fdc_sm );

    // Does not return

}

static void fdc_init_controller( fdc_sm_t *fdc, uint16_t *mem_map )
{
    fdc->buffer       = fdc_disk_buffer;

    fdc->opt_switch   = config.fdc.optswitch;
    fdc->system_block = config.fdc.sysram;
    fdc->user_block   = config.fdc.usrram;

    fdc->sd.fs = NULL;

    for ( int d = 0; d < MAX_DRIVES; ++d )
    {
        strcpy( fdc->sd.disks[d].imagename, config.fdc.drives[d].imagename );
        fdc->sd.disks[d].readonly = config.fdc.drives[d].readonly;
        fdc->sd.disks[d].fil = NULL;
    }

    // Set the registers' addresses
    fdc->HSR = (uint8_t *)&mem_map[fdc->system_block+FDC_HARDWARE_STATUS_REGISTER_OFF];
    fdc->DAR = (uint8_t *)&mem_map[fdc->system_block+FDC_DMA_ADDRESS_REGISTER_OFF];
    fdc->MSR = (uint8_t *)&mem_map[fdc->system_block+FDC_MAIN_STATUS_REGISTER_OFF];
    fdc->UDR = (uint8_t *)&mem_map[fdc->system_block+FDC_DATA_REGISTER_OFF];

    // FIXME: PUT THIS BETWEEN IFDEFS
    fdc->DBR = (uint8_t *)&mem_map[fdc->system_block+FDC_DEBUG_REGISTER_OFF];
    *fdc->DBR = 0x00;       // Clear Debug Register
    //

    *fdc->UDR = 0x00;       // Clear Data Register
    *fdc->MSR = RQM_FLAG;   // Init to ready, receive
    fdc_update_hsr( fdc, IRQREQ_FLAG | (fdc->opt_switch ? OPTSWT_FLAG : 0x00) ); // No interrupt pending

    fdc->cmd_table = fdc_commands;
    fdc->state = FDC_IDLE;
    fdc->interrupt = INT_NONE;

    sem_init ( &fdc->sem, 0, 1 );
    fdc->last_event = INVALID;

    return;
}

void fdc_setup( uint16_t *mem_map )
{

    fdc_init_controller( &fdc_sm, mem_map );

    imd_mount_sd_card( &fdc_sm.sd );

    for ( int d = 0; d < MAX_DRIVES; ++d )
    {
        imd_disk_mount( &fdc_sm.sd, d );
    }

    sleep_ms( 10 );

    multicore_reset_core1();
    multicore_launch_core1( fdc_start );

    return;
}

void fdc_set_dma_write_channel( int channel )
{
    dma_write_channel = channel;
}

void fdc_set_read_addr( io_rw_32 *addr )
{
    read_addr = addr;
}
