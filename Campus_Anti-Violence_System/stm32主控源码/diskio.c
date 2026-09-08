/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#include "Hardware/sd_spi.h"

/* Definitions of physical drive number for each drive */
#define DEV_MMC			0	/* Map MMC/SD card to physical drive 0 */

static volatile DSTATUS s_sd_stat = STA_NOINIT;


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* 用于标识驱动器的物理驱动器号 */
)
{
	switch (pdrv) {
		case DEV_MMC:
			return s_sd_stat;
		default:
			return STA_NOINIT;
	}
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	switch (pdrv) {
		case DEV_MMC:
			if (!(s_sd_stat & STA_NOINIT)) {
				return s_sd_stat;
			}
			if (SD_SPI_Init() == SD_SPI_OK) {
				s_sd_stat &= (DSTATUS)~STA_NOINIT;
			} else {
				s_sd_stat = STA_NOINIT;
			}
			return s_sd_stat;
		default:
			return STA_NOINIT;
	}
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (!count) {
		return RES_PARERR;
	}
	if (pdrv != DEV_MMC) {
		return RES_PARERR;
	}
	if (s_sd_stat & STA_NOINIT) {
		return RES_NOTRDY;
	}

	return (SD_SPI_ReadBlocks((uint32_t)sector, buff, (uint32_t)count) == SD_SPI_OK) ? RES_OK : RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0
 
DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	if (!count) {
		return RES_PARERR;
	}
	if (pdrv != DEV_MMC) {
		return RES_PARERR;
	}
	if (s_sd_stat & STA_NOINIT) {
		return RES_NOTRDY;
	}

	return (SD_SPI_WriteBlocks((uint32_t)sector, buff, (uint32_t)count) == SD_SPI_OK) ? RES_OK : RES_ERROR;
}
 
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	if (pdrv != DEV_MMC) {
		return RES_PARERR;
	}
	if (s_sd_stat & STA_NOINIT) {
		return RES_NOTRDY;
	}

	switch (cmd) {
		case CTRL_SYNC:
			return RES_OK;

		case GET_SECTOR_COUNT:
			*(DWORD *)buff = (DWORD)SD_SPI_GetSectorCount();
			return RES_OK;

		case GET_SECTOR_SIZE:
			*(WORD *)buff = 512;
			return RES_OK;

		case GET_BLOCK_SIZE:
			*(DWORD *)buff = 1;
			return RES_OK;

		default:
			return RES_PARERR;
	}
}


