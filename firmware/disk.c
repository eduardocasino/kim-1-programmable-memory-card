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
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "f_util.h"
#include "ff.h"

#include "config.h"
#include "pins.h"
#include "debug.h"

#define MAX_DRIVES      4

#define IMD_SIGNATURE                       0x20444D49
#define FDC_HARDWARE_STATUS_REGISTER_OFF    0x1FE8
#define FDC_DMA_ADDRESS_REGISTER_OFF        0x1FEA
#define FDC_DEBUG_REGISTER_OFF              0x1FEC
#define FDC_MAIN_STATUS_REGISTER_OFF        0x1FEE
#define FDC_DATA_REGISTER_OFF               0x1FEF

#define IMD_UNAVAILABLE         0x00
#define IMD_NORMAL              0x01
#define IMD_COMPRESSED          0x02
#define IMD_NORMAL_DEL          0x03
#define IMD_COMPRESSED_DEL      0x04
#define IMD_NORMAL_ERR          0x05
#define IMD_COMPRESSED_ERR      0x06
#define IMD_NORMAL_DEL_ERR      0x07
#define IMD_COMPRESSED_DEL_ERR  0x08
#define IMD_TYPE_NORMAL_MASK    0x01

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
#define ST1_ND                  0b00000010
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

typedef enum { UDR_READ, UDR_WRITE, INVALID } fdc_event_t;
typedef enum { FDC_IDLE, FDC_BUSY, FDC_COMMAND, FDC_STATUS, FDC_END  } fdc_state_t;
typedef enum { INT_NONE = 0, INT_SEEK, INT_COMMAND, INT_INVALID, INT_ATTENTION } fdc_interrupt_t;

#define NUM_CMDS        32
#define CMD_MASK        0b00011111
#define CMD_OPT_MASK    0b11100000

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
    uint8_t cyl;
    uint8_t head;
    uint8_t sect;
    uint8_t nbytes;    
} fdc_format_buf_t;

#define MAX_SIZE_CODE 6
int sizes[] = { 128, 256, 512, 1024, 2048, 4096, 8192 };
#define MAX_SECTOR_SIZE 8192

typedef struct {
    uint8_t     type;
    uint32_t    index;  // Beginning of sector data in the image file
} imd_sector_t;

#define MAX_SECTORS_PER_TRACK    32      // Should cover all 5,25 and 8 inches formats
typedef struct {
    struct imd_s {
        struct imd_data_s {
            uint8_t mode;
            uint8_t cylinder;
            uint8_t head;
            uint8_t sectors;
            uint8_t size;
        } data;
        uint8_t sector_map[MAX_SECTORS_PER_TRACK];
        imd_sector_t sector_info[MAX_SECTORS_PER_TRACK];
    } imd;

    uint32_t         track_index;    // Position of track in the image file
    uint32_t         data_index;     // Beginning of track data in the image file
} fdc_track_t;

#define MAX_HEADS                   2
#define MAX_CYLINDERS_PER_DISK     80
typedef struct {

    // TODO: ADD REGISTER WITH DISK SIGNALS FOR SENSE DRIVE!!
    //
    FIL         *fil;           // Image file descriptor

    uint8_t     cylinders;
    uint8_t     heads;
    
    // I don't know if the track ordering in an IMD file is always the same, so I will
    // assume it isn't. Hence, we need a "track map" so we can easily and quickly jump
    // to any track in the image. Each entry marks the position (index) of the track
    // in the image file
    //
    uint32_t track_map[MAX_HEADS][MAX_CYLINDERS_PER_DISK];

    fdc_track_t current_track;
} disk_t;

typedef struct {
    int                 command_len;            // Command bytes - 1
    int                 result_len;             // Result bytes
    disk_command_fn_t   command_fn;
} fdc_cmd_table_t;

typedef struct fdc_sm_s {

    semaphore_t sem;                            // Syncronization semaphore
    fdc_event_t last_event;

    disk_t disks[MAX_DRIVES];
    int current_drive;

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
    #define ADDR_MASK       0b00111111
    uint8_t *DAR;

    // uPD765 Main Status Register
    #define RQM_FLAG        (uint8_t)( 1 << 7 )
    #define DIR_FLAG        (uint8_t)( 1 << 6 )
    #define BSY_FLAG        (uint8_t)( 1 << 4 )
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

static FATFS fs;
static FIL filpa[MAX_DRIVES];

static uint8_t  disk_buffer[MAX_SECTOR_SIZE+4];    // 8Kb buffer plus some space before for the sector type mark

static fdc_sm_t fdc_sm = { 0x00 };

static int dma_write_channel;
static io_rw_32 *read_addr;

int imd_check_file_header( FIL *fil )
{
    FRESULT fr;
    uint32_t signature;
    uint8_t c;
    UINT bytes_read = 0;

    debug_printf( DBG_DEBUG, "imd_check_signature()\n" );

    f_rewind( fil );

    if ( FR_OK != ( fr = f_read( fil, &signature, sizeof(signature), &bytes_read ) ) )
    {
        debug_printf( DBG_ALWAYS, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
        return -1;
    }

    while ( FR_OK == ( fr = f_read( fil, &c, 1, &bytes_read ) ) )
    {
        if ( c == 0x1A )
        {
            break;
        }
    }

    if ( FR_OK != fr )
    {
        debug_printf( DBG_ALWAYS, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
        return -1;
    }

    if ( 0x1A != c || signature != IMD_SIGNATURE )
    {
        debug_printf( DBG_ALWAYS, "Bad IMD header\n" );
        return -1;
    }

    return 0;

}

// disk->fil must be oppened
//
int imd_parse_disk_img( disk_t *disk )
{
    FRESULT fr;
    UINT bytes_read = 0;

    debug_printf( DBG_DEBUG, "imd_parse_disk_img()\n" );
    
    assert( disk->fil != NULL );

    // Make sure it is an IMD image file and find the 0x1A mark if returns 0,
    // it is valid an file index is positioned at the beginning of track info
    //
    if ( imd_check_file_header( disk->fil ) )
    {
        return -1;
    }

    int track;
    uint8_t lastmode = 0, maxhead = 0, maxcyl = 0;
    bool sect_cyl_map, sect_head_map;
    FSIZE_t ff;

    for ( track = 0; track < MAX_HEADS * MAX_CYLINDERS_PER_DISK; ++track )
    {
        FSIZE_t track_start = f_tell( disk->fil );

		if ( FR_OK != (fr = f_read( disk->fil, &disk->current_track.imd, sizeof(struct imd_data_s), &bytes_read ) ) )
        {
            debug_printf( DBG_ALWAYS, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
            return -1;
        }

	    if ( 0 == bytes_read )
	    {
            // No more tracks
		    break;
	    }

        debug_printf( DBG_INSANE, "disk->current_track.imd.data.head: %d\n", disk->current_track.imd.data.head );
        debug_printf( DBG_INSANE, "disk->current_track.imd.data.cylinder: %d\n", disk->current_track.imd.data.cylinder );
        debug_printf( DBG_INSANE, "disk->current_track.imd.data.mode: %d\n", disk->current_track.imd.data.mode);
        debug_printf( DBG_INSANE, "disk->current_track.imd.data.sectors: %d\n", disk->current_track.imd.data.sectors );
        debug_printf( DBG_INSANE, "disk->current_track.imd.data.size: %d\n", disk->current_track.imd.data.size );
		
		sect_cyl_map  = disk->current_track.imd.data.head & 0x80;
	    sect_head_map = disk->current_track.imd.data.head & 0x40;

        uint8_t head_no = disk->current_track.imd.data.head & 0x3F;

	    if ( track != 0 && disk->current_track.imd.data.mode != lastmode)
		{
		    debug_printf( DBG_ALWAYS, "Tracks with different modes not supported\n" );
			return -1;			
		}
		lastmode = disk->current_track.imd.data.mode;

		if ( head_no > maxhead) maxhead = head_no;
		if ( disk->current_track.imd.data.cylinder > maxcyl) maxcyl = disk->current_track.imd.data.cylinder;

        disk->track_map[head_no][disk->current_track.imd.data.cylinder] = (uint32_t) track_start;

        debug_printf( DBG_INSANE, "track_start: %8.8X\n", disk->track_map[head_no][disk->current_track.imd.data.cylinder] );

        // Skip sector numbering, cylinder and head maps if present 
		ff = f_tell( disk->fil ) + disk->current_track.imd.data.sectors * ( 1 + sect_cyl_map ? 1 : 0 + sect_head_map ? 1 : 0 );
		    
        if ( FR_OK != ( fr = f_lseek ( disk->fil, ff ) ) )
        {
            debug_printf( DBG_ALWAYS, "Bad IMD file (truncated?): f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
            return -1;
        }

        // Skip to next track
		for ( int nsect = 0; nsect < disk->current_track.imd.data.sectors; ++nsect )
    	{
            static uint8_t stype;

            if ( FR_OK != ( fr = f_read( disk->fil, &stype, 1, &bytes_read ) ) )
            {
                debug_printf( DBG_ALWAYS, "Bad IMD file (truncated?): f_read error: %s (%d)\n", FRESULT_str(fr), fr );
			    return -1;
            }

    		if ( !(stype & IMD_TYPE_NORMAL_MASK) || stype == IMD_UNAVAILABLE )
	    	{
			    debug_printf( DBG_ALWAYS, "Bad or corrupt file, unsupported sector type: 0x%2.2X\n", stype );
                return -1;
			}

            ff = f_tell( disk->fil ) + sizes[disk->current_track.imd.data.size];            
                
            if ( FR_OK != ( fr = f_lseek ( disk->fil, ff ) ) )
            {
                debug_printf( DBG_ALWAYS, "Bad IMD file (truncated?): f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
                return -1;
            }  
		}

    }
    disk->cylinders = maxcyl + 1;
    disk->heads = maxhead + 1;

    return 0;
}

uint8_t imd_seek_track(
    fdc_sm_t *fdc,
    uint8_t fdd_no,
    uint8_t head,
    uint8_t cyl )
{
    UINT bytes_read;
    FSIZE_t idx;
    FRESULT fr;
    bool sect_cyl_map, sect_head_map;

    assert( fdd_no < MAX_DRIVES );

    disk_t *disk = &fdc->disks[fdd_no];

    debug_printf( DBG_DEBUG, "imd_seek_track(disk %2.2X, head %2.2X, cyl %2.2X)\n", fdd_no, head, cyl );
    
    if ( head >= disk->heads || cyl >= disk->cylinders || disk->fil == NULL )
    {
        return 0xFF;
    }

    if ( fdd_no == fdc->current_drive && head == disk->current_track.imd.data.head && cyl == disk->current_track.imd.data.cylinder )
    {
        // Already there
        debug_printf( DBG_DEBUG, "Already there\n" );
        return cyl;
    }

    fdc->current_drive = fdd_no;

    idx = disk->track_map[head][cyl];

    debug_printf( DBG_INSANE, "Track index: %8.8X\n", (uint32_t) idx );

    fr = f_lseek( disk->fil, idx );

    if ( FR_OK != fr )
    {
        debug_printf( DBG_ALWAYS, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
        return 0xFF;
    }

    // Load track info

    disk->current_track.track_index = (uint32_t) idx;

    fr = f_read( disk->fil, &disk->current_track.imd.data,
                                sizeof(struct imd_data_s),
                                &bytes_read );
    
    if ( FR_OK != fr )
    {
        debug_printf( DBG_ALWAYS, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
        return 0xFF;        
    }

	sect_cyl_map  = disk->current_track.imd.data.head & 0x80;
	sect_head_map = disk->current_track.imd.data.head & 0x40;
    disk->current_track.imd.data.head &= 0x3F;

    // Load sector map
    fr = f_read( disk->fil, disk->current_track.imd.sector_map,
                                disk->current_track.imd.data.sectors,
                                &bytes_read );

    if ( FR_OK != fr )
    {
        debug_printf( DBG_ALWAYS, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
        return 0xFF;
    }

    // Load sector info (info is in track order, not sector order)
    // First, Skip sector cylinder and head maps if present 
	
    idx = f_tell( disk->fil ) + disk->current_track.imd.data.sectors * (sect_cyl_map ? 1 : 0 + sect_head_map ? 1 : 0 );

    disk->current_track.data_index = (uint32_t)idx;

    if ( FR_OK != f_lseek ( disk->fil, idx ) )
    {
        debug_printf( DBG_ALWAYS, "Bad IMD file (truncated?) SHOULD NOT OCCUR\n" );
        return 0xFF;
    }

    for ( int nsect = 0; nsect < disk->current_track.imd.data.sectors; ++nsect )
    {
        disk->current_track.imd.sector_info[nsect].index = (uint32_t)idx;

        if ( FR_OK != ( f_read( disk->fil, &disk->current_track.imd.sector_info[nsect].type, 1, &bytes_read ) ) )
        {
            debug_printf( DBG_ALWAYS, "Bad imd file (truncated?) SHOULD NOT OCCUR\n" );
		  	return 0xFF;
        }

        // Compressed sectors not supported
    	assert( disk->current_track.imd.sector_info[nsect].type & IMD_TYPE_NORMAL_MASK );

        // Calculate and go to next sector index
	    idx = f_tell( disk->fil ) + sizes[disk->current_track.imd.data.size];
                 
        if ( FR_OK != f_lseek ( disk->fil, idx ) )
        {
            debug_printf( DBG_ALWAYS, "Bad IMD file (truncated?) SHOULD NOT OCCUR\n" );
            return 0xFF;
        }
	}

    debug_printf( DBG_DEBUG, "Head %d, Cylinder %d, Track Index %8.8X, Data index %8.8X\n",
        disk->current_track.imd.data.head,
        disk->current_track.imd.data.cylinder,
        disk->current_track.track_index,
        disk->current_track.data_index );

    for ( int nsect = 0; nsect < disk->current_track.imd.data.sectors; ++nsect )
    {
        debug_printf( DBG_INSANE, "-> Sector %2.2d, type %2.2d, index %8.8X\n",
            disk->current_track.imd.sector_map[nsect],
            disk->current_track.imd.sector_info[nsect].type,
            disk->current_track.imd.sector_info[nsect].index );
    }

    return cyl;
}

static bool imd_is_compatible_media( fdc_track_t *track, bool is_mfm )
{
    if ( ( track->imd.data.mode != 0x00 && track->imd.data.mode != 0x03 )
            || ( track->imd.data.size > 0x03 ) 
            || ( track->imd.data.mode == 0x00 && is_mfm )
            || ( track->imd.data.mode == 0x03 && !is_mfm ) )
        return false;
    else
        return true;
}

static int imd_get_sector_info_idx( fdc_track_t *track, uint8_t sect )
{
    for ( int nsect = 0; nsect < track->imd.data.sectors; ++nsect )
    {
        if ( sect == track->imd.sector_map[nsect] )
        {
            return nsect;
        }
    }

    return -1;
}

static void *disk_copy_to_memory( uint32_t size, void *dest, uint8_t *orig )
{
    uint8_t *mem = (uint8_t *)dest;

    for ( int n = 0; n < size; ++n )
    {
        *mem = orig[n];
        mem += 2;
    }

    return (void *)mem;
}

static void *disk_copy_from_memory( uint32_t size, uint8_t *dest, void *orig )
{
    uint8_t *mem = (uint8_t *)orig;

    for ( int n = 0; n < size; ++n )
    {
        dest[n] = *mem;
        mem += 2;
    }

    return (void *)mem;
}

// FIXME: TODO: Support continuing to next track
//
typedef enum { READ, WRITE, SCAN } rw_cmd_t;
typedef enum { NORMAL_DATA, DELETED_DATA } data_mode_t;

static uint32_t imd_read_write_data(
    fdc_sm_t *fdc,
    rw_cmd_t cmd,
    bool mt,
    bool mf,
    bool sk,
    uint8_t *result,
    uint8_t fdd_no,
    uint8_t head,
    uint8_t cyl,
    uint8_t sect,
    uint8_t nbytes,
    uint8_t eot,
    uint8_t dtl,
    data_mode_t mode,
    void *dmamem,
    uint16_t max_dma_transfer,
    bool do_copy )
{
    debug_printf( DBG_DEBUG, "imd_read_write_data(cmd %d, mt %s, mf %s, sk %s, disk %2.2X, "
                                "head %2.2X, cyl %2.2X, sect %2.2X, nbytes %2.2X, eot %2.2X, dtl %2.2X, mode %2.2X\n)",
                            cmd, mt ? "true" : "false", mf ? "true" : "false", sk ? "true" : "false",
                            fdd_no, head, cyl, sect, nbytes, eot, dtl, mode );
    
    assert( fdd_no < MAX_DRIVES );
    
    disk_t *disk = &fdc->disks[fdd_no];

    // FIXME: Only this is implemented
            // MT -> Multitrack bit                 0 <--- UNIMPLEMENTED
            // MF -> FM (0) / MFM (1) bit           1 <--- IMPLEMENTED
            // SK -> Skip deleted data address mark 0 <--- IMPLEMENTED

    if ( head == 1 && disk->heads == 1 )
    {
        result[0] |= ST0_ABNORMAL_TERM | ST0_NOT_READY;
        return 0;
    }

    if ( cyl != imd_seek_track( fdc, fdd_no, head, cyl ) )
    {
        result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
        return 0;
    }

    if ( ! imd_is_compatible_media( &disk->current_track, mf ))
    {
        result[0] |= ST0_ABNORMAL_TERM;
        result[1] = ST1_MA;
        return 0;
    }

    int trdata;

    if ( disk->current_track.imd.data.size != nbytes )
    {
        if ( nbytes == 0x00 )
        {
            trdata = ( dtl > sizes[disk->current_track.imd.data.size] ) ? sizes[disk->current_track.imd.data.size] : dtl;
        }
        else
        {
            result[0] |= ST0_ABNORMAL_TERM;
            result[1] = ST1_DE;
            result[6] = disk->current_track.imd.data.size;
        
            return 0;
        }
    }
    else
    {
        trdata = sizes[nbytes];
    }

    // Get position of first requested sector

    int sii = imd_get_sector_info_idx( &disk->current_track, sect );
    int count = disk->current_track.imd.data.sectors;

    debug_printf( DBG_INSANE, "Sector count: %d\n", count);

    if ( sii < 0 )
    {
        debug_printf( DBG_DEBUG, "Sector %d not present in head %d, cyl %d\n", sect, head, cyl );
        result[0] |= ST0_ABNORMAL_TERM;
        result[1] = ST1_ND;
        return 0;
    }

    uint32_t bytes_rw = 0;
    FRESULT fr;
    UINT brw;
    int s;
    for ( s= sii; s < count; ++s )
    {
        bool skip = false;

        debug_printf( DBG_INSANE, "Reading/Writing to physical sector %d, soft sector %d\n", s, disk->current_track.imd.sector_map[s]);

        result[5] = disk->current_track.imd.sector_map[s];

        imd_sector_t *sector_info = &disk->current_track.imd.sector_info[s];
    	
        if ( cmd == READ )
        {
            if (   mode == NORMAL_DATA && (sector_info->type == IMD_NORMAL_DEL || sector_info->type == IMD_NORMAL_DEL_ERR )
                || mode == DELETED_DATA &&  (sector_info->type == IMD_NORMAL || sector_info->type == IMD_NORMAL_ERR )
               )
            {
                if ( sk )
                {
                    // Skip sector
                    ++skip;
                }
                else 
                {
                    result[0] |= ST0_ABNORMAL_TERM;
                    result[2] |= ST2_CM;
                } 
            }

            if ( sector_info->type == IMD_NORMAL_DEL_ERR ||  sector_info->type == IMD_NORMAL_ERR )
            {
                result[0] |= ST0_ABNORMAL_TERM;
                result[1] |= ST1_DE;
                result[2] |= ST2_DD;
            }
        }
      
        // Read or write sector data
        //
        if ( !skip )
        {
            fr = f_lseek( disk->fil, sector_info->index );

            if ( FR_OK != fr )
            {
                debug_printf( DBG_ALWAYS, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
                result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
                break;
            }

            if ( cmd == READ )
            {
                // Reads sector type + sector data
                fr = f_read( disk->fil, fdc->buffer, sizes[nbytes]+1, &brw );

                if ( FR_OK != fr || brw != sizes[nbytes] + 1 )
                {
                    debug_printf( DBG_ALWAYS, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
                    result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
                    break;
                }

                debug_printf( DBG_INSANE, "Read %d bytes from sector %d\n", brw, disk->current_track.imd.sector_map[s] );

                // Discard sector type and transfer trdata bytes
                if ( do_copy && (bytes_rw + trdata <= max_dma_transfer ) )
                {
                    dmamem = disk_copy_to_memory( trdata, dmamem, fdc->buffer + 1 ); 
                }

                bytes_rw += trdata;

                // Check for bad CRC or DAM errors
                if ( result[0] | ST0_ABNORMAL_TERM )
                {
                    // If normal read (not track mode), abort
                    break;
                }
            }
            else    // ( cmd == WRITE )
            {
                // Put sector type
                fdc->buffer[0] = ( mode == NORMAL_DATA ) ? IMD_NORMAL : IMD_NORMAL_DEL;

                if ( do_copy && (bytes_rw + trdata <= max_dma_transfer ) )
                {
                    dmamem = disk_copy_from_memory( trdata, fdc->buffer + 1, dmamem ); 
                
                    // Writes sector type + sector data
                    fr = f_write( disk->fil, fdc->buffer, trdata+1, &brw );

                    if ( FR_OK != fr || brw != trdata + 1 )
                    {
                        debug_printf( DBG_ALWAYS, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
                        result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
                        break;
                    }
                    f_sync( disk->fil );

                    // Update sector type in current_track info
                    sector_info->type = fdc->buffer[0];

                    bytes_rw += trdata;
                }
            }
        }

        if ( eot == disk->current_track.imd.sector_map[s] )
        {
            // Reached EOT
            debug_printf( DBG_INSANE, "Found EOT, sector %d, soft sector %d\n", s, disk->current_track.imd.sector_map[s]);

            break;
        }
    }

    if ( s == count )
    {
        // Tried to read/write past last sector in cylinder
        debug_printf( DBG_INSANE, "Trying to access sector past cylinder end: %d\n", s);
        result[0] |= ST0_ABNORMAL_TERM;
        result[1] |= ST1_EN;
    }

    return bytes_rw;
}

static void imd_format_track(
    fdc_sm_t *fdc,
    bool mf,
    uint8_t *result,
    uint8_t fdd_no,
    uint8_t head,
    uint8_t cyl,
    uint8_t nsect,
    uint8_t nbytes,
    uint8_t filler,
    void *dmamem,
    uint16_t max_dma_transfer,
    bool do_copy )
{
    debug_printf( DBG_DEBUG, "imd_format_track(disk %2.2X, head %2.2X, cyl %2.2X), nsect %2.2X, nbytes %2.2X, filler %2.2X\n",
                fdd_no, head, cyl, nsect, nbytes, filler );
    
    assert( fdd_no < MAX_DRIVES );
    
    disk_t *disk = &fdc->disks[fdd_no];

    if ( head == 1 && disk->heads == 1 )
    {
        result[0] |= ST0_ABNORMAL_TERM | ST0_NOT_READY;
        return;
    }

    if ( cyl != imd_seek_track( fdc, fdd_no, head, cyl ) )
    {
        result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
        return;
    }

    if ( ! imd_is_compatible_media( &disk->current_track, mf ))
    {
        result[0] |= ST0_ABNORMAL_TERM;
        result[1] = ST1_MA;
        return;
    }

    // FIXME: Allow formatting to different sector sizes
    int trdata;
    if ( disk->current_track.imd.data.size != nbytes )
    {
        result[0] |= ST0_ABNORMAL_TERM;
        result[1] = ST1_DE;
        result[6] = disk->current_track.imd.data.size;
        
        return;
    }
    else
    {
        trdata = sizes[nbytes];
    }

    // Get position of first requested sector

    uint32_t bytes_rw = 0;
    FRESULT fr;
    UINT brw;
    int s;
    for ( s= 0; s < nsect; ++s )
    {
        debug_printf( DBG_INSANE, "Formatting physical sector %d, soft sector %d\n", s, disk->current_track.imd.sector_map[s]);

        result[5] = disk->current_track.imd.sector_map[s];

        imd_sector_t *sector_info = &disk->current_track.imd.sector_info[s];
    
        fr = f_lseek( disk->fil, sector_info->index );

        if ( FR_OK != fr )
        {
            debug_printf( DBG_ALWAYS, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
            break;
        }

        fdc_format_buf_t format_buf;

        // Copy the format information for the sector
        //
        if ( do_copy && (bytes_rw + sizeof(fdc_format_buf_t) <= max_dma_transfer ) )
        {
            dmamem = disk_copy_from_memory( sizeof(fdc_format_buf_t), (uint8_t *)&format_buf, dmamem ); 
        }

        if ( cyl != format_buf.cyl || head != format_buf.head || nbytes != format_buf.nbytes )
        {
            result[0] |= ST0_ABNORMAL_TERM;
            result[1] = ST1_MA;
            break;
        }

        // Update sector map
        //
        disk->current_track.imd.sector_map[s] = format_buf.sect;

        // Fill the buffer with blank data
        //
        fdc->buffer[0] = IMD_NORMAL;
        for ( int i = 1; i <= trdata; ++i )
        {
            fdc->buffer[i] = filler;        // In case of cmd == FORMAT, this is the filler byte
        }

        // Write sector data to disk
        fr = f_write( disk->fil, fdc->buffer, trdata+1, &brw );

        if ( FR_OK != fr || brw != trdata + 1 )
        {
            debug_printf( DBG_ALWAYS, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
            break;
        }

        bytes_rw += sizeof(fdc_format_buf_t);

    }

    if ( s == nsect )
    {
        // Tried to read/write past last sector in cylinder
        // FIXME: CHECK THIS BEHAVIOUR

        result[1] |= ST1_EN;
    }

    if ( !( result[0] & ST0_ABNORMAL_TERM ) )
    {
        // Write sector map to disk
        //
        fr = f_lseek( disk->fil, disk->current_track.track_index + sizeof(struct imd_data_s) );

        if ( FR_OK != fr )
        {
            debug_printf( DBG_ALWAYS, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
        }

        fr = f_write( disk->fil, disk->current_track.imd.sector_map, nsect, &brw );

        if ( FR_OK != fr || brw != nsect )
        {
            debug_printf( DBG_ALWAYS, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
        }
        f_sync( disk->fil ); 
    }

    return;
}

void disk_set_fdc_dma_write_channel( int channel )
{
    dma_write_channel = channel;
}

void disk_set_fdc_read_addr( io_rw_32 *addr )
{
    read_addr = addr;
}

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
    debug_printf( DBG_DEBUG, "fdc_cmd_specify(0x%4.4X)\n", fdc->state );

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

    fdc->seek_result[1] = imd_seek_track( fdc, fdd_no, head, cyl );

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
            ( !(base_address % 0x2000) &&  (dma_addr < (base_address + 0x1000)) &&  (*fdc->DAR & ODD_FLAG) )  // Block at even boundary, dma_addr starts in lower block and odd flag is set
        ||  ( !(base_address % 0x2000) && !(dma_addr < (base_address + 0x1000)) && !(*fdc->DAR & ODD_FLAG) )  // Block at even boundary, dma_addr starts in upper block and odd flag is not set
        ||  (  (base_address % 0x2000) &&  (dma_addr < (base_address + 0x1000)) && !(*fdc->DAR & ODD_FLAG) )  // Block at odd boundary,  dma_addr starts in lower block and odd flag is not set
        ||  (  (base_address % 0x2000) && !(dma_addr < (base_address + 0x1000)) &&  (*fdc->DAR & ODD_FLAG) )  // Block at odd boundary,  dma_addr starts in upper block and odd flag is set
    )
    {
        debug_printf( DBG_DEBUG, " Invalid DAR register configuration: 0x%2.2X\n", fdc->DAR );

        dma_addr = 0x0000;
    }

    debug_printf( DBG_DEBUG, "dma_addr: 0x%4.4X\n", dma_addr );

    return dma_addr;
}


static fdc_state_t _fdc_cmd_read_write( fdc_sm_t *fdc, rw_cmd_t cmd, data_mode_t mode )
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
    bool mt         = fdc->command.data[0] & CMD_MT_FLAG;
    bool mf         = fdc->command.data[0] & CMD_MF_FLAG;
    bool sk         = fdc->command.data[0] & CMD_SK_FLAG;
    uint8_t fdd_no  = fdc->command.data[1] & CMD1_DRIVE_NUMBER_MASK;   // Get drive number
    uint8_t cyl     = fdc->command.data[2];     // Cylinder
    uint8_t head    = fdc->command.data[3];     // Head. Same as HD
    uint8_t sect    = fdc->command.data[4];     // Sector id
    uint8_t nbytes  = fdc->command.data[5];     // Bytes per sector (as in sizes[])
    uint8_t eot     = fdc->command.data[6];     // Last sector to read
                                                // GPL is ignored
    uint8_t dtl     = fdc->command.data[8];     // Bytes to read if N == 0

    uint16_t base_address = *fdc->DAR & SYSTEM_FLAG ? fdc->system_block : fdc->user_block;
    uint16_t dma_addr = fdc_get_dma_addr( fdc, base_address );

    // Initialise result with the  CHRN, Disk and head
    fdc->command.data[0] = ST0_NORMAL_TERM | ( head << 2) | fdd_no;
    fdc->command.data[1] = 0;
    fdc->command.data[2] = 0;
    fdc->command.data[3] = cyl;
    fdc->command.data[4] = head;
    fdc->command.data[5] = sect;
    fdc->command.data[6] = nbytes;

    if ( dma_addr == 0x0000 )
    {
        fdc->command.data[0] = ST0_ABNORMAL_TERM | ST0_EC_MASK;
        fdc->command.data[1] = ST1_DM;
        fdc->command.data[2] = 0;
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

        imd_read_write_data( fdc, cmd, mt, mf, sk, fdc->command.data,
                                        fdd_no, head, cyl, sect, nbytes, eot, dtl, mode,
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

static fdc_state_t fdc_cmd_read( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_read(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_read_write( fdc, READ, NORMAL_DATA );
}

static fdc_state_t fdc_cmd_read_deleted( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_read_deleted(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_read_write( fdc, READ, DELETED_DATA );
}


static fdc_state_t fdc_cmd_write( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_write(0x%4.4X)\n", fdc->state );

    // Invalidate SK option
    fdc->command.data[0] &= ~CMD_SK_FLAG;

    return _fdc_cmd_read_write( fdc, WRITE, NORMAL_DATA );
}

static fdc_state_t fdc_cmd_write_deleted( fdc_sm_t *fdc )
{
    debug_printf( DBG_DEBUG, "fdc_cmd_write_deleted(0x%4.4X)\n", fdc->state );

    return _fdc_cmd_read_write( fdc, WRITE, DELETED_DATA );
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
    uint8_t nbytes  = fdc->command.data[2];     // Bytes per sector (as in sizes[])
    uint8_t nsect   = fdc->command.data[3];     // Sectors per track
    uint8_t filler  = fdc->command.data[5];     // Filler byte

    uint8_t cyl = fdc->disks[fdd_no].current_track.imd.data.cylinder;

    // Initialise result with the  CHRN, Disk and head
    fdc->command.data[0] = ST0_NORMAL_TERM | ( head << 2) | fdd_no;
    fdc->command.data[1] = 0;
    fdc->command.data[2] = 0;
    fdc->command.data[3] = cyl;            // For the format command, these four bytes are ignored
    fdc->command.data[4] = head;
    fdc->command.data[5] = nsect;
    fdc->command.data[6] = nbytes;

    uint16_t base_address = *fdc->DAR & SYSTEM_FLAG ? fdc->system_block : fdc->user_block;
    uint16_t dma_addr = fdc_get_dma_addr( fdc, base_address );

    if ( dma_addr == 0x0000 )
    {
        fdc->command.data[0] = ST0_ABNORMAL_TERM | ST0_EC_MASK;
        fdc->command.data[1] = ST1_DM;
        fdc->command.data[2] = 0;
    }

    if ( head == 1 && fdc->disks[fdd_no].heads == 1 )
    {
        fdc->command.data[0] |= ST0_ABNORMAL_TERM | ST0_NOT_READY;
        return 0;
    }

    // FIXME: Support multi head
    // NOTE: Only supported on already formatted images with the same parameters
    //
    if (
           nbytes != fdc->disks[fdd_no].current_track.imd.data.size
        || nsect  != fdc->disks[fdd_no].current_track.imd.data.sectors
       )
    {
        fdc->command.data[0] |= ST0_ABNORMAL_TERM;
        fdc->command.data[1] = ST1_MA;
        return 0;
    }

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

    imd_format_track( fdc, mf, fdc->command.data,
                                        fdd_no, head, cyl, nsect, nbytes, filler,
                                        &mem_map[dma_addr],
                                        max_dma_size,
                                        do_copy );
    
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

    if ( ! imd_is_compatible_media( &fdc->disks[fdd_no].current_track, mf ))
    {
        fdc->command.data[0] = ST0_ABNORMAL_TERM;
        fdc->command.data[1] = ST1_ND;
        fdc->command.data[2] = 0;
    }
    else
    {
        fdc->command.data[0] = ST0_NORMAL_TERM;
        fdc->command.data[1] = 0;
        fdc->command.data[2] = 0;
        fdc->command.data[3] = fdc->disks[fdd_no].current_track.imd.data.cylinder;
        fdc->command.data[4] = fdc->disks[fdd_no].current_track.imd.data.head;
        fdc->command.data[5] = fdc->disks[fdd_no].current_track.imd.data.sectors;
        fdc->command.data[6] = fdc->disks[fdd_no].current_track.imd.data.size;
    }
    
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
    // uint8_t head = (fdc->command.data[1] & CMD1_HEAD_MASK) >> ST0_HEAD_FLAG_POS;

    fdc->command.data[0] = 0;
    fdc->command.data[0] |= fdc->disks[fdd_no].fil == NULL ? ST3_FT : ST3_RY;
    fdc->command.data[0] |= fdc->disks[fdd_no].current_track.imd.data.cylinder == 0 ? ST3_T0 : 0;
    fdc->command.data[0] |= fdc->disks[fdd_no].heads = 2 ? ST3_TS : 0;
    fdc->command.data[0] |= fdc->disks[fdd_no].current_track.imd.data.head == 1 ? ST3_HEAD_ADDRESS_MASK : 0;
    fdc->command.data[0] |= fdc->current_drive;

    fdc->command.dp = fdc->command.data;

    // Put first byte of status
    *fdc->UDR = *fdc->command.dp;

    // Set data direction controller -> CPU
    *fdc->MSR |= DIR_FLAG;
    fdc->state = FDC_STATUS;

    return fdc->state;
}

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

fdc_cmd_table_t fdc_commands[NUM_CMDS] = {
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
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },    // SCAN LOW OR EQUAL
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { 0,                        1,                      fdc_cmd_unimplemented },
    { READ_CMD_LEN - 1,         READ_RES_LEN,           fdc_cmd_unimplemented },    // SCAN HIGH OR EQUAL
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

static void __not_in_flash_func(disk_fdc_emulation)( fdc_sm_t *fdc )
{
    while ( true )
    {
        sem_acquire_blocking ( &fdc->sem );
        fdc_set_notready( fdc );

        fdc_event_t event = fdc->last_event;

        // Only for desperate debug. Takes too long!!!
        // debug_printf( DBG_INSANE, "STATE: 0x%2.2X, EVENT: 0x%2.2X --> ", fdc->state, event );

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
        // debug_printf( DBG_INSANE, "NEW STATE: 0x%2.2X\n", fdc->state );

        fdc_set_ready( fdc );
        
        if ( fdc->state == FDC_IDLE )
        {
            fdc_set_notbusy( fdc );
        }

        fdc_raise_interrupt( fdc );

    }
}

static void __not_in_flash_func(disk_mem_write_interrupt_handler)( void )
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

static void __not_in_flash_func(disk_mem_read_interrupt_handler)( void )
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

// FIXME: Init all drives
// Array of MAX_DRIVES FIL
// Pass fdc and fdd_no

static inline FIL *disk_mount( fdc_sm_t *fdc, int fdd_no, const char *filename )
{
    FRESULT fr;
    disk_t *disk = &fdc->disks[fdd_no];

    debug_printf( DBG_DEBUG, "disk_mount()\n" );

    FIL *filp = &filpa[fdd_no];

    // Invalidate current track info so imd_seek_track() does not get confused
    disk->current_track.imd.data.head = 0xFF;

    fr = f_open( filp, filename, FA_READ | FA_WRITE | FA_OPEN_EXISTING );

    if ( FR_OK == fr )
    {
        disk->fil = filp;

        if ( !imd_parse_disk_img( disk ) )
        {
            debug_printf( DBG_INFO, "Mounted \"%s\", CYLS: %d, HEADS: %d\n", filename, disk->cylinders, disk->heads );
        }
        else
        {
            f_close( filp );
            filp = NULL;
        }
    }
    else
    {
        debug_printf( DBG_ALWAYS, "f_open(%s) error: %s (%d)\n", filename, FRESULT_str( fr ), fr );
        f_close( filp );
        filp = NULL;
    }

    return filp;

}

static void disk_start( void )
{
    const char* filename = "disk.imd";      // FIXME: Make configurable

    debug_printf( DBG_DEBUG, "disk_start()\n");

    // FIXME: Maybe the card is not inserted at this point, so do not stop if it does
    //        fail at this moment
    //
    if ( NULL != disk_mount( &fdc_sm, 0, filename ) )
    {
	    irq_set_exclusive_handler( PIO0_IRQ_0, disk_mem_read_interrupt_handler );
	    irq_set_enabled( PIO0_IRQ_0, true );
        irq_set_exclusive_handler( DMA_IRQ_0, disk_mem_write_interrupt_handler );
        irq_set_enabled( DMA_IRQ_0, true );

        // Set maximum priority to the disk emulation IRQs
        irq_set_priority( PIO0_IRQ_0, 0 );
        irq_set_priority( DMA_IRQ_0, 0 );

        disk_fdc_emulation( &fdc_sm );

        // Does not return
    }

}


// FIXME: Consider that the SD card may not be inserted at this time
//
static void disk_initialize_sd_card( FATFS *fs )
{
    FRESULT fr;

    fr = f_mount( fs, "0:", 1 );

    if ( FR_OK != fr )
    {
        panic( "f_mount error: %s (%d)\n", FRESULT_str(fr), fr );
    }
}

static void disk_init_controller( fdc_sm_t *fdc, uint16_t *mem_map )
{
    fdc->current_drive = -1;

    fdc->buffer = disk_buffer;
    fdc->system_block = 0xC000;         // FIXME: Do it configurable
    fdc->user_block = 0x6000;           // FIXME: Do it configurable
    fdc->opt_switch = true;             // FIXME: Do it configurable

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

void disk_setup( uint16_t *mem_map )
{
    disk_init_controller( &fdc_sm, mem_map );
    disk_initialize_sd_card( &fs );

    sleep_ms( 10 );

    multicore_reset_core1();
    multicore_launch_core1( disk_start );

    return;
}
