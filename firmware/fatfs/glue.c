// Credit: https://mcuapplab.blogspot.com/2022/12/raspberry-pi-pico-c-sdk-storage-ep-3.html
//

#include "stdio.h"
#include "stdlib.h"
#include "ff.h"
#include "diskio.h"
#include "spi_sdmmc.h"
#include "hardware/rtc.h"
#include "inttypes.h"

#define SDMMC_DRV_0     0

sdmmc_data_t *pSDMMC=NULL;

//==================//
DSTATUS disk_initialize (BYTE drv){
    DSTATUS stat;
    if (SDMMC_DRV_0 == drv) {
        if (pSDMMC == NULL) {
            pSDMMC = (sdmmc_data_t*)malloc(sizeof(sdmmc_data_t));
            pSDMMC->csPin = SDMMC_PIN_CS;
            pSDMMC->spiPort = SDMMC_SPI_PORT;
            pSDMMC->spiInit=false;
            pSDMMC->sectSize=512;
        }
        stat = sdmmc_disk_initialize(pSDMMC);
        return stat;
    }
    return STA_NOINIT;
 }

/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status (BYTE drv) {
    DSTATUS stat;
    if (SDMMC_DRV_0 == drv) {
        stat=  sdmmc_disk_status(pSDMMC); /* Return disk status */
        return stat;
    }
    return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read (
	BYTE drv,		/* Physical drive number (0) */
	BYTE *buff,		/* Pointer to the data buffer to store read data */
	LBA_t sector,	/* Start sector number (LBA) */
	UINT count		/* Number of sectors to read (1..128) */
)
{
    DSTATUS stat;
    if (SDMMC_DRV_0 == drv) {
        stat = sdmmc_disk_read(buff, sector, count, pSDMMC);
        return stat;
    }
	return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write (
	BYTE drv,			/* Physical drive number (0) */
	const BYTE *buff,	/* Ponter to the data to write */
	LBA_t sector,		/* Start sector number (LBA) */
	UINT count			/* Number of sectors to write (1..128) */
)
{
    DSTATUS stat = STA_NODISK;
    if (SDMMC_DRV_0 == drv) {
        stat = sdmmc_disk_write(buff, sector, count, pSDMMC);
        return stat;
    }
	return RES_PARERR;
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE drv,		/* Physical drive number (0) */
	BYTE cmd,		/* Control command code */
	void *buff		/* Pointer to the conrtol data */
)
{
    DSTATUS stat;
    if (SDMMC_DRV_0 == drv) {
        stat = sdmmc_disk_ioctl(cmd, buff, pSDMMC);
        return stat;
    }
	return RES_PARERR;
}

DWORD get_fattime(void) {
    datetime_t t = {0, 0, 0, 0, 0, 0, 0};
    bool rc = rtc_get_datetime(&t);
    if (!rc) return 0;

    DWORD fattime = 0;
    // bit31:25
    // Year origin from the 1980 (0..127, e.g. 37 for 2017)
    uint8_t yr = t.year - 1980;
    fattime |= (0b01111111 & yr) << 25;
    // bit24:21
    // Month (1..12)
    uint8_t mo = t.month;
    fattime |= (0b00001111 & mo) << 21;
    // bit20:16
    // Day of the month (1..31)
    uint8_t da = t.day;
    fattime |= (0b00011111 & da) << 16;
    // bit15:11
    // Hour (0..23)
    uint8_t hr = t.hour;
    fattime |= (0b00011111 & hr) << 11;
    // bit10:5
    // Minute (0..59)
    uint8_t mi = t.min;
    fattime |= (0b00111111 & mi) << 5;
    // bit4:0
    // Second / 2 (0..29, e.g. 25 for 50)
    uint8_t sd = t.sec / 2;
    fattime |= (0b00011111 & sd);
    return fattime;
}
