/*
 * K-1013 Floppy Disc Controller emulation for the KIM-1 Programmable Memory Board
 *   https://github.com/eduardocasino/kim-1-programmable-memory-card
 *
 * IMD support
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

#ifndef IMD_H
#define IMD_H

#include <stdint.h>
#include <stddef.h>

#include "ff.h"
#include "upd765.h"

#define IMD_SIGNATURE                       0x20444D49

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

#define MAX_HEADS                2
#define MAX_CYLINDERS_PER_DISK  80
#define MAX_SECTORS_PER_TRACK   32      // Should cover all 5,25 and 8 inches formats

#define MAX_SECTOR_SIZE 8192

#define MAX_FILE_NAME_LEN       63

typedef struct {
    uint8_t     type;
    uint32_t    index;  // Beginning of sector data in the image file
} imd_sector_t;

typedef struct  {
    uint8_t mode;
    uint8_t cylinder;
    uint8_t head;
    uint8_t sectors;
    uint8_t size;
} imd_data_t;

typedef struct {
    imd_data_t data;
    uint8_t sector_map[MAX_SECTORS_PER_TRACK];
    imd_sector_t sector_info[MAX_SECTORS_PER_TRACK];
} imd_t;

typedef struct {
    imd_t       imd;            // IMD track data
    uint32_t    track_index;    // Position of track in the image file
    uint32_t    data_index;     // Beginning of track data in the image file
} imd_track_t;

typedef struct {

    // TODO: ADD REGISTER WITH DISK SIGNALS FOR SENSE DRIVE!!
    //
    FIL         *fil;           // Image file descriptor
    char        imagename[MAX_FILE_NAME_LEN+1];
    bool        readonly;
    uint8_t     cylinders;
    uint8_t     heads;
    
    // I don't know if the track ordering in an IMD file is always the same, so I will
    // assume it isn't. Hence, we need a "track map" so we can easily and quickly jump
    // to any track in the image. Each entry marks the position (index) of the track
    // in the image file
    //
    uint32_t track_map[MAX_HEADS][MAX_CYLINDERS_PER_DISK];

    imd_track_t current_track;

    uint8_t status;             // Disk status

} imd_disk_t;

typedef struct {
    DIR dir;
    bool is_open;
} dir_t;

typedef struct {
    FATFS   *fs;
    imd_disk_t disks[MAX_DRIVES];
    dir_t dir;
} imd_sd_t;

int imd_parse_disk_img( imd_disk_t *disk );
uint8_t imd_seek_track( imd_disk_t *disk, uint8_t head, uint8_t cyl );
void imd_read_id( imd_disk_t *disk, bool mf, uint8_t *result );
void imd_read_data(
    imd_disk_t *disk,
    uint8_t *buffer,
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
    bool do_copy );
void imd_write_data(
    imd_disk_t *disk,
    uint8_t *buffer,
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
    bool do_copy );
void imd_format_track(
    imd_disk_t *disk,
    uint8_t *buffer,
    bool mf,
    uint8_t *result,
    uint8_t head,
    uint8_t nsect,
    uint8_t nbytes,
    uint8_t filler,
    void *dmamem,
    uint16_t max_dma_transfer,
    bool do_copy );

void imd_mount_defaults( imd_sd_t * sd );
void imd_disk_mount( imd_sd_t *sd, int fdd_no, uint8_t *result, char *imagename, bool ro );
void imd_disk_unmount( imd_sd_t *sd, int fdd_no, uint8_t *result );
int imd_mount_sd_card( imd_sd_t *sd );
bool imd_disk_is_drive_mounted( imd_sd_t *sd, int fdd_no );
bool imd_disk_is_image_mounted( imd_sd_t *sd, char *imagename );
char *imd_disk_get_imagename( imd_sd_t *sd, int fdd_no );
bool imd_disk_is_ro( imd_sd_t *sd, int fdd_no );
void imd_image_erase( imd_sd_t *sd, uint8_t *result, char *imagename );
void imd_image_rename( imd_sd_t *sd, uint8_t *result, char *source, char *dest );
void imd_image_copy( imd_sd_t *sd, uint8_t *result, uint8_t *buffer, size_t bufsiz, char *source, char *dest, bool owrite );
void imd_new(
    imd_sd_t *sd,
    uint8_t *result,
    uint8_t *buffer,
    size_t bufsiz,
    char *filename,
    uint8_t tracks,
    uint8_t sect,
    uint8_t bps,
    uint8_t filler,
    bool packed );
void imd_init_dir_listing( imd_sd_t *sd, uint8_t *result );
void imd_next_dir_entry( imd_sd_t *sd, uint8_t *result, void *dmamem );
void imd_save_mounts( imd_sd_t *sd, uint8_t *result );

#endif /* IMD_H */
