// Credit: https://mcuapplab.blogspot.com/2022/12/raspberry-pi-pico-c-sdk-storage-ep-3.html
//

/*
This library is derived from ChaN's FatFs - Generic FAT Filesystem Module.
*/
#ifndef SPI_SDMMC_H
#define SPI_SDMMC_H
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "ff.h"
#include "diskio.h"

//#define __SPI_SDMMC_DMA

#define SPI_BAUDRATE_LOW    (100 * 1000)        /* 100KHz */
#define SPI_BAUDRATE_HIGH   (50 * 1000 * 1000)  /* 50MHz */

/* SDMMC SPI pins*/
#define SDMMC_SPI_PORT spi0
#define SDMMC_PIN_MISO 16
#define SDMMC_PIN_CS   17
#define SDMMC_PIN_SCK  18
#define SDMMC_PIN_MOSI 19
/* ====================== */

/* MMC/SD command */
#define CMD0 (0)		   /* GO_IDLE_STATE */
#define CMD1 (1)		   /* SEND_OP_COND (MMC) */
#define ACMD41 (0x80 + 41) /* SEND_OP_COND (SDC) */
#define CMD8 (8)		   /* SEND_IF_COND */
#define CMD9 (9)		   /* SEND_CSD */
#define CMD10 (10)		   /* SEND_CID */
#define CMD12 (12)		   /* STOP_TRANSMISSION */
#define ACMD13 (0x80 + 13) /* SD_STATUS (SDC) */
#define CMD16 (16)		   /* SET_BLOCKLEN */
#define CMD17 (17)		   /* READ_SINGLE_BLOCK */
#define CMD18 (18)		   /* READ_MULTIPLE_BLOCK */
#define CMD23 (23)		   /* SET_BLOCK_COUNT (MMC) */
#define ACMD23 (0x80 + 23) /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24 (24)		   /* WRITE_BLOCK */
#define CMD25 (25)		   /* WRITE_MULTIPLE_BLOCK */
#define CMD32 (32)		   /* ERASE_ER_BLK_START */
#define CMD33 (33)		   /* ERASE_ER_BLK_END */
#define CMD38 (38)		   /* ERASE */
#define CMD55 (55)		   /* APP_CMD */
#define CMD58 (58)		   /* READ_OCR */


typedef struct {
    spi_inst_t *spiPort;
    bool spiInit;
    uint csPin;
    BYTE cardType;
    uint16_t sectSize;
#ifdef __SPI_SDMMC_DMA
    uint read_dma_ch;
    uint write_dma_ch;
    dma_channel_config dma_rc;
    dma_channel_config dma_wc;
    bool dmaInit;
#endif
    DRESULT Stat;
}sdmmc_data_t;


//BYTE sdmmc_init(sdmmc_data_t *sdmmc);
DSTATUS sdmmc_disk_initialize(sdmmc_data_t *sdmmc);
DSTATUS sdmmc_disk_status(sdmmc_data_t *sdmmc);
DSTATUS sdmmc_disk_read(BYTE *buff, LBA_t sector,	UINT count, sdmmc_data_t *sdmmc); 
DSTATUS sdmmc_disk_write(const BYTE *buff, LBA_t sector, UINT count, sdmmc_data_t *sdmmc);
DSTATUS sdmmc_disk_ioctl ( BYTE cmd, void *buff, sdmmc_data_t *sdmmc);

//static 
int sdmmc_read_datablock (BYTE *buff, UINT btr, sdmmc_data_t *sdmmc);
//static 
int sdmmc_write_datablock (const BYTE *buff, BYTE token, sdmmc_data_t *sdmmc);
//static 
BYTE sdmmc_send_cmd(BYTE cmd,  DWORD arg, sdmmc_data_t *sdmmc);

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC3		0x01		/* MMC ver 3 */
#define CT_MMC4		0x02		/* MMC ver 4+ */
#define CT_MMC		0x03		/* MMC */
#define CT_SDC1		0x02		/* SDC ver 1 */
#define CT_SDC2		0x04		/* SDC ver 2+ */
#define CT_SDC		0x0C		/* SDC */
#define CT_BLOCK	0x10		/* Block addressing */
#endif
