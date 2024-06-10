// Credit: https://mcuapplab.blogspot.com/2022/12/raspberry-pi-pico-c-sdk-storage-ep-3.html
//

/*
This library is derived from ChaN's FatFs - Generic FAT Filesystem Module.
*/
#include "stdio.h"
#include "stdlib.h"
#include "pico/stdlib.h"
#include "spi_sdmmc.h"


#define SDMMC_CD 0 // card detect
#define SDMMC_WP 0 // write protected

static uint8_t dummy_block[512];


/* sdmmc spi port initialize*/
void sdmmc_spi_port_init(sdmmc_data_t *sdmmc)
{
	spi_init(sdmmc->spiPort, SPI_BAUDRATE_LOW);
	gpio_set_function(SDMMC_PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(sdmmc->csPin, GPIO_FUNC_SIO);
	gpio_set_function(SDMMC_PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(SDMMC_PIN_MOSI, GPIO_FUNC_SPI);
	gpio_set_dir(sdmmc->csPin, GPIO_OUT);
	gpio_put(sdmmc->csPin, 1); // deselect

	sdmmc->spiInit = true; // alreadily initialized
}

/* config spi dma*/
#ifdef __SPI_SDMMC_DMA
void config_spi_dma(sdmmc_data_t *sdmmc)
{
	sdmmc->read_dma_ch = dma_claim_unused_channel(true);
	sdmmc->write_dma_ch = dma_claim_unused_channel(true);
	sdmmc->dma_rc = dma_channel_get_default_config(sdmmc->read_dma_ch);
	sdmmc->dma_wc = dma_channel_get_default_config(sdmmc->write_dma_ch);
	channel_config_set_transfer_data_size(&(sdmmc->dma_rc), DMA_SIZE_8);
	channel_config_set_transfer_data_size(&(sdmmc->dma_wc), DMA_SIZE_8);
	channel_config_set_read_increment(&(sdmmc->dma_rc), false);
	channel_config_set_write_increment(&(sdmmc->dma_rc), true);
	channel_config_set_read_increment(&(sdmmc->dma_wc), true);
	channel_config_set_write_increment(&(sdmmc->dma_wc), false);
	channel_config_set_dreq(&(sdmmc->dma_rc), spi_get_dreq(sdmmc->spiPort, false));
	channel_config_set_dreq(&(sdmmc->dma_wc), spi_get_dreq(sdmmc->spiPort, true));

	for (int i = 0; i < 512; i++)
		dummy_block[i] = 0xFF;

	dma_channel_configure(sdmmc->read_dma_ch,
						  &(sdmmc->dma_rc),
						  NULL,
						  &spi_get_hw(sdmmc->spiPort)->dr,
						  sdmmc->sectSize, false);
	dma_channel_configure(sdmmc->write_dma_ch,
						  &(sdmmc->dma_wc),
						  &spi_get_hw(sdmmc->spiPort)->dr,
						  NULL,
						  sdmmc->sectSize, false);
	sdmmc->dmaInit = true;
}
#endif

/* set spi cs low (select)*/
void sdmmc_spi_cs_low(sdmmc_data_t *sdmmc)
{
	gpio_put(sdmmc->csPin, 0);
}
/* set spi cs high (deselect)*/
void sdmmc_spi_cs_high(sdmmc_data_t *sdmmc)
{
	gpio_put(sdmmc->csPin, 1);
}
/* Initialize SDMMC SPI interface */
static void sdmmc_init_spi(sdmmc_data_t *sdmmc)
{
	sdmmc_spi_port_init(sdmmc); // if not initialized, init it
#ifdef __SPI_SDMMC_DMA
	if (!sdmmc->dmaInit)
		config_spi_dma(sdmmc);
#endif

	sleep_ms(10);
}

/* Receive a sector data (512 bytes) */
static void sdmmc_read_spi_dma(
	BYTE *buff, /* Pointer to data buffer */
	UINT btr,	/* Number of bytes to receive (even number) */
	sdmmc_data_t *sdmmc)
{
#ifdef __SPI_SDMMC_DMA
	dma_channel_set_read_addr(sdmmc->write_dma_ch, dummy_block, false);
	dma_channel_set_trans_count(sdmmc->write_dma_ch, btr, false);

	dma_channel_set_write_addr(sdmmc->read_dma_ch, buff, false);
	dma_channel_set_trans_count(sdmmc->read_dma_ch, btr, false);

	dma_start_channel_mask((1u << (sdmmc->read_dma_ch)) | (1u << (sdmmc->write_dma_ch)));
	dma_channel_wait_for_finish_blocking(sdmmc->read_dma_ch);
#else
	spi_read_blocking(sdmmc->spiPort, 0xFF, buff, btr);
#endif
}

#if FF_FS_READONLY == 0
/* Send a sector data (512 bytes) */
static void sdmmc_write_spi_dma(
	const BYTE *buff, /* Pointer to the data */
	UINT btx,		  /* Number of bytes to send (even number) */
	sdmmc_data_t *sdmmc)
{
#ifdef __SPI_SDMMC_DMA
	dma_channel_set_read_addr(sdmmc->write_dma_ch, buff, false);
	dma_channel_set_trans_count(sdmmc->write_dma_ch, btx, false);
	dma_channel_start(sdmmc->write_dma_ch);
	dma_channel_wait_for_finish_blocking(sdmmc->write_dma_ch);
#else
	spi_write_blocking(sdmmc->spiPort, buff, btx);
#endif
}
#endif

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static int sdmmc_wait_ready(uint timeout, sdmmc_data_t *sdmmc)
{
	uint8_t dst;
	absolute_time_t timeout_time = make_timeout_time_ms(timeout);
	do
	{
		spi_read_blocking(sdmmc->spiPort, 0xFF, &dst, 1);
	} while (dst != 0xFF && 0 < absolute_time_diff_us(get_absolute_time(), timeout_time)); /* Wait for card goes ready or timeout */

	return (dst == 0xFF) ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/
static void sdmmc_deselect(sdmmc_data_t *sdmmc)
{
	uint8_t src = 0xFF;
	sdmmc_spi_cs_high(sdmmc);
	spi_write_blocking(sdmmc->spiPort, &src, 1);
}

/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/
static int sdmmc_select(sdmmc_data_t *sdmmc) /* 1:OK, 0:Timeout */
{
	uint8_t src = 0xFF;
	sdmmc_spi_cs_low(sdmmc);
	spi_write_blocking(sdmmc->spiPort, &src, 1);
	if (sdmmc_wait_ready(500, sdmmc))
		return 1; /* Wait for card ready */
	sdmmc_deselect(sdmmc);
	return 0; /* Timeout */
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/
// static
int sdmmc_read_datablock(			 /* 1:OK, 0:Error */
						 BYTE *buff, /* Data buffer */
						 UINT btr,	 /* Data block length (byte) */
						 sdmmc_data_t *sdmmc)
{
	BYTE token;
	absolute_time_t timeout_time = make_timeout_time_ms(200);
	do
	{ /* Wait for DataStart token in timeout of 200ms */
		spi_read_blocking(sdmmc->spiPort, 0xFF, &token, 1);
	} while ((token == 0xFF) && 0 < absolute_time_diff_us(get_absolute_time(), timeout_time));
	if (token != 0xFE)
		return 0; /* Function fails if invalid DataStart token or timeout */

	sdmmc_read_spi_dma(buff, btr, sdmmc);
	// Discard CRC
	spi_read_blocking(sdmmc->spiPort, 0xFF, &token, 1);
	spi_read_blocking(sdmmc->spiPort, 0xFF, &token, 1);
	return 1; // Function succeeded
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to the MMC                                         */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
// static
int sdmmc_write_datablock(					/* 1:OK, 0:Failed */
						  const BYTE *buff, /* Ponter to 512 byte data to be sent */
						  BYTE token,		/* Token */
						  sdmmc_data_t *sdmmc)
{
	BYTE resp;
	if (!sdmmc_wait_ready(500, sdmmc))
		return 0; /* Wait for card ready */
	// Send token : 0xFE--single block, 0xFC -- multiple block write start, 0xFD -- StopTrans
	spi_write_blocking(sdmmc->spiPort, &token, 1);
	if (token != 0xFD)
	{										   /* Send data if token is other than StopTran */
		sdmmc_write_spi_dma(buff, 512, sdmmc); /* Data */

		token = 0xFF;
		spi_write_blocking(sdmmc->spiPort, &token, 1); // Dummy CRC
		spi_write_blocking(sdmmc->spiPort, &token, 1);

		spi_read_blocking(sdmmc->spiPort, 0xFF, &resp, 1);
		// receive response token: 0x05 -- accepted, 0x0B -- CRC error, 0x0C -- Write Error
		if ((resp & 0x1F) != 0x05)
			return 0; /* Function fails if the data packet was not accepted */
	}
	return 1;
}
#endif

/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/
//static
BYTE sdmmc_send_cmd(			  /* Return value: R1 resp (bit7==1:Failed to send) */
						   BYTE cmd,  /* Command index */
						   DWORD arg, /* Argument */
						   sdmmc_data_t *sdmmc)
{
	BYTE n, res;
	BYTE tcmd[5];

	if (cmd & 0x80)
	{ /* Send a CMD55 prior to ACMD<n> */
		cmd &= 0x7F;
		res = sdmmc_send_cmd(CMD55, 0, sdmmc);
		if (res > 1)
			return res;
	}

	/* Select the card and wait for ready except to stop multiple block read */
	if (cmd != CMD12)
	{
		sdmmc_deselect(sdmmc);
		if (!sdmmc_select(sdmmc))
			return 0xFF;
	}

	/* Send command packet */
	tcmd[0] = 0x40 | cmd;		 // 0 1 cmd-index(6) --> 01xxxxxx(b)
	tcmd[1] = (BYTE)(arg >> 24); // 32 bits argument
	tcmd[2] = (BYTE)(arg >> 16);
	tcmd[3] = (BYTE)(arg >> 8);
	tcmd[4] = (BYTE)arg;
	spi_write_blocking(sdmmc->spiPort, tcmd, 5);
	n = 0x01; /* Dummy CRC + Stop */
	if (cmd == CMD0)
		n = 0x95; /* Valid CRC for CMD0(0) */
	if (cmd == CMD8)
		n = 0x87; /* Valid CRC for CMD8(0x1AA) */

	spi_write_blocking(sdmmc->spiPort, &n, 1);

	/* Receive command resp */
	if (cmd == CMD12)
		spi_read_blocking(sdmmc->spiPort, 0xFF, &res, 1); /* Diacard following one byte when CMD12 */
	n = 10;												  /* Wait for response (10 bytes max) */
	do
	{
		spi_read_blocking(sdmmc->spiPort, 0xFF, &res, 1);
	} while ((res & 0x80) && --n);

	return res; /* Return received response */
}

/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/
DSTATUS sdmmc_init(sdmmc_data_t *sdmmc)
{
	BYTE n, cmd, ty, src, ocr[4];

	sdmmc->Stat = STA_NOINIT;
	// low baudrate
	spi_set_baudrate(sdmmc->spiPort, SPI_BAUDRATE_LOW);
	src = 0xFF;
	sdmmc_spi_cs_low(sdmmc);
	for (n = 10; n; n--)
		spi_write_blocking(sdmmc->spiPort, &src, 1); // Send 80 dummy clocks
	sdmmc_spi_cs_high(sdmmc);

	ty = 0;
	if (sdmmc_send_cmd(CMD0, 0, sdmmc) == 1)
	{ /* Put the card SPI/Idle state, R1 bit0=1*/
		absolute_time_t timeout_time = make_timeout_time_ms(1000);
		if (sdmmc_send_cmd(CMD8, 0x1AA, sdmmc) == 1)
		{													 /* SDv2? */
			spi_read_blocking(sdmmc->spiPort, 0xFF, ocr, 4); // R7(5 bytes): R1 read by sdmmc_send_cmd, Get the other 32 bit return value of R7 resp
			if (ocr[2] == 0x01 && ocr[3] == 0xAA)
			{ /* Is the card supports vcc of 2.7-3.6V? */
				while ((0 < absolute_time_diff_us(get_absolute_time(), timeout_time)) && sdmmc_send_cmd(ACMD41, 1UL << 30, sdmmc))
					; /* Wait for end of initialization with ACMD41(HCS) */
				if ((0 < absolute_time_diff_us(get_absolute_time(), timeout_time)) && sdmmc_send_cmd(CMD58, 0, sdmmc) == 0)
				{ /* Check CCS bit in the OCR */
					spi_read_blocking(sdmmc->spiPort, 0xFF, ocr, 4);
					ty = (ocr[0] & 0x40) ? CT_SDC2 | CT_BLOCK : CT_SDC2; /* Card id SDv2 */
				}
			}
		}
		else
		{ /* Not SDv2 card */
			if (sdmmc_send_cmd(ACMD41, 0, sdmmc) <= 1)
			{ /* SDv1 or MMC? */
				ty = CT_SDC1;
				cmd = ACMD41; /* SDv1 (ACMD41(0)) */
			}
			else
			{
				ty = CT_MMC3;
				cmd = CMD1; /* MMCv3 (CMD1(0)) */
			}
			while ((0 < absolute_time_diff_us(get_absolute_time(), timeout_time)) && sdmmc_send_cmd(cmd, 0, sdmmc))
				;																										   /* Wait for end of initialization */
			if (!(0 < absolute_time_diff_us(get_absolute_time(), timeout_time)) || sdmmc_send_cmd(CMD16, 512, sdmmc) != 0) /* Set block length: 512 */
				ty = 0;
		}
	}
	sdmmc->cardType = ty; /* Card type */
	sdmmc_deselect(sdmmc);
	if (ty)
	{ /* OK */
		// high baudrate
		printf("\nThe actual baudrate(SD/MMC):%d\n",spi_set_baudrate(sdmmc->spiPort, SPI_BAUDRATE_HIGH)); // speed high
		sdmmc->sectSize = 512;
		sdmmc->Stat &= ~STA_NOINIT; /* Clear STA_NOINIT flag */
	}
	else
	{ /* Failed */
		sdmmc->Stat = STA_NOINIT;
	}

	return sdmmc->Stat;
}

/* sdmmc_disk_initizlize, sdmmc_disk_read, sdmmc_disk_write, sdmmc_disk_status, sdmmc_disk_ioctl*/

/* sdmmc_disk_initialize*/
DSTATUS sdmmc_disk_initialize(sdmmc_data_t *sdmmc)
{
	if (!sdmmc->spiInit)
		sdmmc_init_spi(sdmmc); /* Initialize SPI */
	DSTATUS stat = sdmmc_init(sdmmc);

	return stat;
}
/* sdmmc disk status*/
DSTATUS sdmmc_disk_status(sdmmc_data_t *sdmmc)
{
	return sdmmc->Stat;
}

/* sdmmc disk read*/
DSTATUS sdmmc_disk_read(
	BYTE *buff,	  /* Pointer to the data buffer to store read data */
	LBA_t sector, /* Start sector number (LBA) */
	UINT count,	  /* Number of sectors to read (1..128) */
	sdmmc_data_t *sdmmc)
{
	DWORD sect = (DWORD)(sector);

	if (!count)
		return RES_PARERR; /* Check parameter */
	if (sdmmc->Stat & STA_NOINIT)
		return RES_NOTRDY; /* Check if drive is ready */

	if (!(sdmmc->cardType & CT_BLOCK))
		sect *= 512; /* LBA ot BA conversion (byte addressing cards) */
	if (count == 1)
	{												  /* Single sector read */
		if ((sdmmc_send_cmd(CMD17, sect, sdmmc) == 0) /* READ_SINGLE_BLOCK */
			&& sdmmc_read_datablock(buff, 512, sdmmc))
		{
			count = 0;
		}
	}
	else
	{ /* Multiple sector read */
		if (sdmmc_send_cmd(CMD18, sect, sdmmc) == 0)
		{ /* READ_MULTIPLE_BLOCK */
			do
			{
				if (!sdmmc_read_datablock(buff, 512, sdmmc))
					break;
				buff += 512;
			} while (--count);
			sdmmc_send_cmd(CMD12, 0, sdmmc); /* STOP_TRANSMISSION */
		}
	}
	sdmmc_deselect(sdmmc); // sdmmc_select() is called in function sdmmc_send_cmd()

	return count ? RES_ERROR : RES_OK; /* Return result */
}

DSTATUS sdmmc_disk_write(
	const BYTE *buff, /* Ponter to the data to write */
	LBA_t sector,	  /* Start sector number (LBA) */
	UINT count,		  /* Number of sectors to write (1..128) */
	sdmmc_data_t *sdmmc)
{
	DWORD sect = (DWORD)sector;
	if (!count)
		return RES_PARERR; /* Check parameter */
	if (sdmmc->Stat & STA_NOINIT)
		return RES_NOTRDY; /* Check drive status */
	if (sdmmc->Stat & STA_PROTECT)
		return RES_WRPRT; /* Check write protect */

	if (!(sdmmc->cardType & CT_BLOCK))
		sect *= 512; /* LBA ==> BA conversion (byte addressing cards) */

	if (count == 1)
	{												  /* Single sector write */
		if ((sdmmc_send_cmd(CMD24, sect, sdmmc) == 0) /* WRITE_BLOCK */
			&& sdmmc_write_datablock(buff, 0xFE, sdmmc))
		{
			count = 0;
		}
	}
	else
	{ /* Multiple sector write */
		if (sdmmc->cardType & CT_SDC)
			sdmmc_send_cmd(ACMD23, count, sdmmc); /* Predefine number of sectors */
		if (sdmmc_send_cmd(CMD25, sect, sdmmc) == 0)
		{ /* WRITE_MULTIPLE_BLOCK */
			do
			{
				if (!sdmmc_write_datablock(buff, 0xFC, sdmmc))
					break;
				buff += 512;
			} while (--count);
			if (!sdmmc_write_datablock(0, 0xFD, sdmmc))
				count = 1; /* STOP_TRAN token */
		}
	}
	sdmmc_deselect(sdmmc); // sdmmc_select() is called in function sdmmc_send_cmd

	return count ? RES_ERROR : RES_OK; /* Return result */
}

/* sdmmc disk ioctl*/
DSTATUS sdmmc_disk_ioctl(
	BYTE cmd,	/* Control command code */
	void *buff, /* Pointer to the conrtol data */
	sdmmc_data_t *sdmmc)
{
	DRESULT res;
	BYTE n, csd[16];
	DWORD st, ed, csize;
	LBA_t *dp;

	BYTE src = 0xFF;

	if (sdmmc->Stat & STA_NOINIT)
		return RES_NOTRDY; /* Check if drive is ready */

	res = RES_ERROR;
	switch (cmd)
	{
	case CTRL_SYNC: /* Wait for end of internal write process of the drive */
		if (sdmmc_select(sdmmc))
			res = RES_OK;
		break;
	case GET_SECTOR_COUNT: /* Get drive capacity in unit of sector (DWORD) */
		if ((sdmmc_send_cmd(CMD9, 0, sdmmc) == 0) && sdmmc_read_datablock(csd, 16, sdmmc))
		{
			if ((csd[0] >> 6) == 1)
			{ /* SDC CSD ver 2 */
				csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
				*(LBA_t *)buff = csize << 10;
			}
			else
			{ /* SDC CSD ver 1 or MMC */
				n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
				csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
				*(LBA_t *)buff = csize << (n - 9);
			}
			res = RES_OK;
		}
		break;
	case GET_SECTOR_SIZE: // FF_MAX_SS != FX_MIN_SS
		//*(WORD*)buff=512; // SDHC, SDXC sector size is 512
		*(WORD *)buff = sdmmc->sectSize;

		res = RES_OK;
		break;
	case GET_BLOCK_SIZE: /* Get erase block size in unit of sector (DWORD) */
		if (sdmmc->cardType & CT_SDC2)
		{ /* SDC ver 2+ */
			if (sdmmc_send_cmd(ACMD13, 0, sdmmc) == 0)
			{ /* Read SD status */
				spi_write_blocking(sdmmc->spiPort, &src, 1);
				if (sdmmc_read_datablock(csd, 16, sdmmc))
				{ /* Read partial block */
					for (n = 64 - 16; n; n--)
						spi_write_blocking(sdmmc->spiPort, &src, 1); // xchg_spi(0xFF);	/* Purge trailing data */
					*(DWORD *)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		}
		else
		{ /* SDC ver 1 or MMC */
			if ((sdmmc_send_cmd(CMD9, 0, sdmmc) == 0) && sdmmc_read_datablock(csd, 16, sdmmc))
			{ /* Read CSD */
				if (sdmmc->cardType & CT_SDC1)
				{ /* SDC ver 1.XX */
					*(DWORD *)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				}
				else
				{ /* MMC */
					*(DWORD *)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;

	case CTRL_TRIM: /* Erase a block of sectors (used when _USE_ERASE == 1) */
		if (!(sdmmc->cardType & CT_SDC))
			break; /* Check if the card is SDC */
		if (sdmmc_disk_ioctl(MMC_GET_CSD, csd, sdmmc))
			break; /* Get CSD */
		if (!(csd[10] & 0x40))
			break; /* Check if ERASE_BLK_EN = 1 */
		dp = buff;
		st = (DWORD)dp[0];
		ed = (DWORD)dp[1]; /* Load sector block */
		if (!(sdmmc->cardType & CT_BLOCK))
		{
			st *= 512;
			ed *= 512;
		}
		if (sdmmc_send_cmd(CMD32, st, sdmmc) == 0 && sdmmc_send_cmd(CMD33, ed, sdmmc) == 0 && sdmmc_send_cmd(CMD38, 0, sdmmc) == 0 && sdmmc_wait_ready(30000, sdmmc))
		{				  /* Erase sector block */
			res = RES_OK; /* FatFs does not check result of this command */
		}
		break;

		/* Following commands are never used by FatFs module */

	case MMC_GET_TYPE: /* Get MMC/SDC type (BYTE) */
		*(BYTE *)buff = sdmmc->cardType;
		res = RES_OK;
		break;

	case MMC_GET_CSD: /* Read CSD (16 bytes) */
		if (sdmmc_send_cmd(CMD9, 0, sdmmc) == 0 && sdmmc_read_datablock((BYTE *)buff, 16, sdmmc))
		{ /* READ_CSD */
			res = RES_OK;
		}
		break;

	case MMC_GET_CID: /* Read CID (16 bytes) */
		if (sdmmc_send_cmd(CMD10, 0, sdmmc) == 0 && sdmmc_read_datablock((BYTE *)buff, 16, sdmmc))
		{ /* READ_CID */
			res = RES_OK;
		}
		break;

	case MMC_GET_OCR: /* Read OCR (4 bytes) */
		if (sdmmc_send_cmd(CMD58, 0, sdmmc) == 0)
		{ /* READ_OCR */
			for (n = 0; n < 4; n++)
				*(((BYTE *)buff) + n) = spi_write_blocking(sdmmc->spiPort, &src, 1); // xchg_spi(0xFF);
			res = RES_OK;
		}
		break;

	case MMC_GET_SDSTAT: /* Read SD status (64 bytes) */
		if (sdmmc_send_cmd(ACMD13, 0, sdmmc) == 0)
		{ /* SD_STATUS */
			spi_write_blocking(sdmmc->spiPort, &src, 1);
			if (sdmmc_read_datablock((BYTE *)buff, 64, sdmmc))
				res = RES_OK;
		}
		break;

	default:
		res = RES_PARERR;
	}

	sdmmc_deselect(sdmmc); // sdmmc_select() is called in function sdmmc_send_cmd()

	return res;
}
