#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "pico/stdlib.h"

#include "ff.h"
#include "f_util.h"

#include "debug.h"
#include "imd.h"
#include "upd765.h"

// File descriptors for FatFs
//
static FATFS fs;
static FIL filpa[MAX_DRIVES];

// Sector sizes
//
int sizes[] = { 128, 256, 512, 1024, 2048, 4096, 8192 };

static int imd_check_file_header( FIL *fil )
{
    FRESULT fr;
    uint32_t signature;
    uint8_t c;
    UINT bytes_read = 0;

    debug_printf( DBG_DEBUG, "imd_check_signature()\n" );

    f_rewind( fil );

    if ( FR_OK != ( fr = f_read( fil, &signature, sizeof(signature), &bytes_read ) ) )
    {
        debug_printf( DBG_ERROR, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
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
        debug_printf( DBG_ERROR, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
        return -1;
    }

    if ( 0x1A != c || signature != IMD_SIGNATURE )
    {
        debug_printf( DBG_ERROR, "Bad IMD header\n" );
        return -1;
    }

    return 0;

}

// disk->fil must be oppened
//
int imd_parse_disk_img( imd_disk_t *disk )
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

		if ( FR_OK != (fr = f_read( disk->fil, &disk->current_track.imd, sizeof(imd_data_t), &bytes_read ) ) )
        {
            debug_printf( DBG_ERROR, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
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
		    debug_printf( DBG_ERROR, "Tracks with different modes not supported\n" );
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
            debug_printf( DBG_ERROR, "Bad IMD file (truncated?): f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
            return -1;
        }

        // Skip to next track
		for ( int nsect = 0; nsect < disk->current_track.imd.data.sectors; ++nsect )
    	{
            static uint8_t stype;

            if ( FR_OK != ( fr = f_read( disk->fil, &stype, 1, &bytes_read ) ) )
            {
                debug_printf( DBG_ERROR, "Bad IMD file (truncated?): f_read error: %s (%d)\n", FRESULT_str(fr), fr );
			    return -1;
            }

            debug_printf( DBG_INSANE, "  sector: %2.2X, type: %2.2X\n", nsect, stype );

    		if ( stype == IMD_UNAVAILABLE )
	    	{
			    debug_printf( DBG_ERROR, "Bad or corrupt file, unsupported sector type: 0x%2.2X\n", stype );
                return -1;
			}

            ff = f_tell( disk->fil );

            if ( stype & IMD_TYPE_NORMAL_MASK )
            {
                // Advance sector size bytes
                ff += sizes[disk->current_track.imd.data.size];
            }
            else
            {
                // Compressed sector, advance 1 byte
                ++ff;

                if ( !disk->readonly )
                {
                    // And, for now, only read support
                    ++disk->readonly;
                    debug_printf( DBG_INFO, "Image contains compressed sector(s), mark as read-only\n" );
                }
            }            
                
            if ( FR_OK != ( fr = f_lseek ( disk->fil, ff ) ) )
            {
                debug_printf( DBG_ERROR, "Bad IMD file (truncated?): f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
                return -1;
            }  
		}

    }
    disk->cylinders = maxcyl + 1;
    disk->heads = maxhead + 1;

    return 0;
}

uint8_t imd_seek_track( imd_disk_t *disk, uint8_t head, uint8_t cyl )
{
    UINT bytes_read;
    FSIZE_t idx;
    FRESULT fr;
    bool sect_cyl_map, sect_head_map;

    debug_printf( DBG_DEBUG, "imd_seek_track(head %2.2X, cyl %2.2X)\n", head, cyl );
    
    if ( head >= disk->heads || cyl >= disk->cylinders || disk->fil == NULL )
    {
        return 0xFF;
    }

    if ( head == disk->current_track.imd.data.head && cyl == disk->current_track.imd.data.cylinder )
    {
        // Already there
        debug_printf( DBG_DEBUG, "Already there\n" );
        return cyl;
    }

    idx = disk->track_map[head][cyl];

    debug_printf( DBG_INSANE, "Track index: %8.8X\n", (uint32_t) idx );

    fr = f_lseek( disk->fil, idx );

    if ( FR_OK != fr )
    {
        debug_printf( DBG_ERROR, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
        return 0xFF;
    }

    // Load track info

    disk->current_track.track_index = (uint32_t) idx;

    fr = f_read( disk->fil, &disk->current_track.imd.data,
                                sizeof(imd_data_t),
                                &bytes_read );
    
    if ( FR_OK != fr )
    {
        debug_printf( DBG_ERROR, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
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
        debug_printf( DBG_ERROR, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
        return 0xFF;
    }

    // Load sector info (info is in track order, not sector order)
    // First, Skip sector cylinder and head maps if present 
	
    idx = f_tell( disk->fil ) + disk->current_track.imd.data.sectors * (sect_cyl_map ? 1 : 0 + sect_head_map ? 1 : 0 );

    disk->current_track.data_index = (uint32_t)idx;

    if ( FR_OK != f_lseek ( disk->fil, idx ) )
    {
        debug_printf( DBG_ERROR, "Bad IMD file (truncated?) SHOULD NOT OCCUR\n" );
        return 0xFF;
    }

    for ( int nsect = 0; nsect < disk->current_track.imd.data.sectors; ++nsect )
    {
        disk->current_track.imd.sector_info[nsect].index = (uint32_t)idx;

        if ( FR_OK != ( f_read( disk->fil, &disk->current_track.imd.sector_info[nsect].type, 1, &bytes_read ) ) )
        {
            debug_printf( DBG_ERROR, "Bad imd file (truncated?) SHOULD NOT OCCUR\n" );
		  	return 0xFF;
        }

        // Calculate and go to next sector index
	    idx = f_tell( disk->fil );

        if ( disk->current_track.imd.sector_info[nsect].type & IMD_TYPE_NORMAL_MASK )
        {
            idx += sizes[disk->current_track.imd.data.size];
        }
        else
        {
            ++idx;
        }

        if ( FR_OK != f_lseek ( disk->fil, idx ) )
        {
            debug_printf( DBG_ERROR, "Bad IMD file (truncated?) SHOULD NOT OCCUR\n" );
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

static int imd_get_sector_info_idx( imd_track_t *track, uint8_t sect )
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

bool imd_is_compatible_media( imd_track_t *track, bool is_mfm )
{
    if ( ( track->imd.data.mode != 0x00 && track->imd.data.mode != 0x03 )
            || ( track->imd.data.size > 0x03 ) 
            || ( track->imd.data.mode == 0x00 && is_mfm )
            || ( track->imd.data.mode == 0x03 && !is_mfm ) )
        return false;
    else
        return true;
}

// FIXME: TODO: Support continuing to next track
//
void imd_read_write_data(
    imd_disk_t *disk,
    uint8_t *buffer,
    upd765_cmd_t cmd,
    bool mt,
    bool mf,
    bool sk,
    uint8_t *result,
    uint8_t head,
    uint8_t cyl,
    uint8_t sect,
    uint8_t nbytes,
    uint8_t eot,
    uint8_t dtl,
    upd765_data_mode_t mode,
    void *dmamem,
    uint16_t max_dma_transfer,
    bool do_copy )
{
    debug_printf( DBG_DEBUG, "imd_read_write_data(cmd %d, mt %s, mf %s, sk %s, "
                                "head %2.2X, cyl %2.2X, sect %2.2X, nbytes %2.2X, eot %2.2X, dtl %2.2X, mode %2.2X\n)",
                            cmd, mt ? "true" : "false", mf ? "true" : "false", sk ? "true" : "false",
                            head, cyl, sect, nbytes, eot, dtl, mode );

    // FIXME: Only this is implemented
            // MT -> Multitrack bit                 0 <--- UNIMPLEMENTED
            // MF -> FM (0) / MFM (1) bit           1 <--- IMPLEMENTED
            // SK -> Skip deleted data address mark 0 <--- IMPLEMENTED

    if ( head == 1 && disk->heads == 1 )
    {
        result[0] |= ST0_ABNORMAL_TERM | ST0_NOT_READY;
        return;
    }

    if ( cyl != imd_seek_track( disk, head, cyl ) )
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
        
            return;
        }
    }
    else
    {
        trdata = sizes[nbytes];
    }

    // Get position of first requested sector

    int count = disk->current_track.imd.data.sectors;

    debug_printf( DBG_INSANE, "Sector count: %d\n", count);

    uint32_t bytes_rw = 0;
    FRESULT fr;
    UINT brw;
    int s;
    for ( s= sect; s < count; ++s )
    {
        bool skip = false;

        int sii = imd_get_sector_info_idx( &disk->current_track, sect );

        debug_printf( DBG_INSANE, "Reading/Writing to physical sector %d, soft sector %d\n", sii, s );

        if ( sii < 0 )
        {
            debug_printf( DBG_DEBUG, "Sector %d not present in head %d, cyl %d\n", sect, head, cyl );
            result[0] |= ST0_ABNORMAL_TERM;
            result[1] = ST1_ND;
            return;
        }

        result[5] = s;

        imd_sector_t *sector_info = &disk->current_track.imd.sector_info[sii];
    	
        if ( cmd == READ )
        {
            if (   mode == NORMAL_DATA && (sector_info->type == IMD_NORMAL_DEL || sector_info->type == IMD_NORMAL_DEL_ERR || sector_info->type == IMD_COMPRESSED_DEL || sector_info->type == IMD_COMPRESSED_DEL_ERR )
                || mode == DELETED_DATA &&  (sector_info->type == IMD_NORMAL || sector_info->type == IMD_NORMAL_ERR || sector_info->type == IMD_COMPRESSED || sector_info->type == IMD_COMPRESSED_ERR )
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

            if ( sector_info->type == IMD_NORMAL_DEL_ERR ||  sector_info->type == IMD_NORMAL_ERR || sector_info->type == IMD_COMPRESSED_DEL_ERR ||  sector_info->type == IMD_COMPRESSED_ERR)
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
                debug_printf( DBG_ERROR, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
                result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
                break;
            }

            if ( cmd == READ )
            {
                int rdsize = sector_info->type & IMD_TYPE_NORMAL_MASK ? sizes[nbytes]+1 : 2;

                // Reads sector type + sector data
                fr = f_read( disk->fil, buffer, rdsize, &brw );

                if ( FR_OK != fr || brw != rdsize )
                {
                    debug_printf( DBG_ERROR, "f_read error: %s (%d)\n", FRESULT_str(fr), fr );
                    result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
                    break;
                }

                debug_printf( DBG_INSANE, "Read %d bytes from sector %d\n", brw, s );

                if ( !(sector_info->type & IMD_TYPE_NORMAL_MASK) )
                {
                    // Fill the buffer with the repeating byte
                    for ( int i = 1; i < sizes[nbytes]; ++i )
                    {
                        buffer[i] = buffer[1];
                    }
                }

                // Discard sector type and transfer trdata bytes
                if ( do_copy && (bytes_rw + trdata <= max_dma_transfer ) )
                {
                    dmamem = disk_copy_to_memory( trdata, dmamem, buffer + 1 ); 
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
                buffer[0] = ( mode == NORMAL_DATA ) ? IMD_NORMAL : IMD_NORMAL_DEL;

                if ( do_copy && (bytes_rw + trdata <= max_dma_transfer ) )
                {
                    dmamem = disk_copy_from_memory( trdata, buffer + 1, dmamem ); 
                
                    // Writes sector type + sector data
                    fr = f_write( disk->fil, buffer, trdata+1, &brw );

                    if ( FR_OK != fr || brw != trdata + 1 )
                    {
                        debug_printf( DBG_ERROR, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
                        result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
                        break;
                    }
                    f_sync( disk->fil );

                    // Update sector type in current_track info
                    sector_info->type = buffer[0];

                    bytes_rw += trdata;
                }
            }
        }

        if ( eot == s )
        {
            // Reached EOT
            debug_printf( DBG_INSANE, "Found EOT, sector %d, soft sector %d\n", s, disk->current_track.imd.sector_map[s]);

            result[0] |= ST0_ABNORMAL_TERM;
            result[1] |= ST1_EN;

            break;
        }
    }

    if ( s == count )
    {
        // EOT sector not found in cylinder
        debug_printf( DBG_INSANE, "EOT sector not found in cylinder\n" );
        result[0] |= ST0_ABNORMAL_TERM;
        result[1] |= ST1_ND;
    }

    return;
}

void imd_format_track(
    imd_disk_t *disk,
    uint8_t *buffer,
    bool mf,
    uint8_t *result,
    uint8_t head,
    uint8_t cyl,
    uint8_t nsect,
    uint8_t nbytes,
    uint8_t filler,
    void *dmamem,
    uint16_t max_dma_transfer,
    bool do_copy )
{
    debug_printf( DBG_DEBUG, "imd_format_track(head %2.2X, cyl %2.2X), nsect %2.2X, nbytes %2.2X, filler %2.2X\n",
                head, cyl, nsect, nbytes, filler );
     
    if ( head == 1 && disk->heads == 1 )
    {
        result[0] |= ST0_ABNORMAL_TERM | ST0_NOT_READY;
        return;
    }

    if ( cyl != imd_seek_track( disk, head, cyl ) )
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
            debug_printf( DBG_ERROR, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
            break;
        }

        upd765_format_buf_t format_buf;

        // Copy the format information for the sector
        //
        if ( do_copy && (bytes_rw + sizeof(upd765_format_buf_t) <= max_dma_transfer ) )
        {
            dmamem = disk_copy_from_memory( sizeof(upd765_format_buf_t), (uint8_t *)&format_buf, dmamem ); 
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
        buffer[0] = IMD_NORMAL;
        for ( int i = 1; i <= trdata; ++i )
        {
            buffer[i] = filler;        // In case of cmd == FORMAT, this is the filler byte
        }

        // Write sector data to disk
        fr = f_write( disk->fil, buffer, trdata+1, &brw );

        if ( FR_OK != fr || brw != trdata + 1 )
        {
            debug_printf( DBG_ERROR, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
            break;
        }

        bytes_rw += sizeof(upd765_format_buf_t);

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
        fr = f_lseek( disk->fil, disk->current_track.track_index + sizeof(imd_data_t) );

        if ( FR_OK != fr )
        {
            debug_printf( DBG_ERROR, "f_seek error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
        }

        fr = f_write( disk->fil, disk->current_track.imd.sector_map, nsect, &brw );

        if ( FR_OK != fr || brw != nsect )
        {
            debug_printf( DBG_ERROR, "f_write error: %s (%d)\n", FRESULT_str(fr), fr );
            result[0] |= ST0_ABNORMAL_TERM | ST0_EC_MASK;
        }
        f_sync( disk->fil ); 
    }

    return;
}

// FIXME: Init all drives
// Array of MAX_DRIVES FIL
// Pass fdc and fdd_no

int imd_disk_mount( imd_sd_t *sd, int fdd_no )
{
    FRESULT fr;

    debug_printf( DBG_DEBUG, "disk_mount()\n" );

    imd_disk_t *disk = &sd->disks[fdd_no];
    FIL *filp = &filpa[fdd_no];

    if ( disk->fil != NULL )
    {
        // Already mounted
        return 0;
    }

    if ( *disk->imagename == '\0' )
    {
        // Not assigned
        return 0;
    }

    // Invalidate current track info so imd_seek_track() does not get confused
    disk->current_track.imd.data.head = 0xFF;

    while ( true )
    {
        fr = f_open( filp, disk->imagename, FA_READ | FA_WRITE | FA_OPEN_EXISTING );

        if ( FR_OK == fr )
        {
            disk->fil = filp;

            if ( !imd_parse_disk_img( disk ) )
            {
                debug_printf( DBG_INFO, "Mounted \"%s\", CYLS: %d, HEADS: %d\n", disk->imagename, disk->cylinders, disk->heads );
            }
            else
            {
                f_close( filp );
                disk->fil = NULL;
            }
            break;
        }
        else
        {
            if ( FR_NOT_ENABLED == fr )
            {
                debug_printf( DBG_ERROR, "SD Card is not mounted. Mounting and retrying...\n");

                if ( imd_mount_sd_card( sd ) )
                {
                    disk->fil = NULL;
                    break;
                }
            }
            else
            {
                debug_printf( DBG_ERROR, "f_open(%s) error: %s (%d)\n", disk->imagename, FRESULT_str( fr ), fr );
                f_close( filp );
                disk->fil = NULL;
                break;
            }
        }
    }

    return ( disk->fil ? 0 : -1);
}

// FIXME: Consider that the SD card may not be inserted at this time
//
int imd_mount_sd_card( imd_sd_t *sd )
{
    FRESULT fr;

    fr = f_mount( &fs, "0:", 1 );

    if ( FR_OK != fr )
    {
        debug_printf( DBG_ERROR, "f_mount error: %s (%d)\n", FRESULT_str( fr ), fr );
        sd->fs = NULL;
        return -1;
    }

    sd->fs = &fs;
    return 0;
}