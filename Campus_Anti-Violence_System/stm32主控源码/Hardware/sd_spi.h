#ifndef SD_SPI_H
#define SD_SPI_H

#include "stm32f10x.h"

typedef enum {
  SD_SPI_OK = 0,
  SD_SPI_ERR = 1,
  SD_SPI_TIMEOUT = 2
} SD_SPI_Status;

SD_SPI_Status SD_SPI_Init(void);
SD_SPI_Status SD_SPI_ReadBlocks(uint32_t lba, uint8_t *buf, uint32_t count);
SD_SPI_Status SD_SPI_WriteBlocks(uint32_t lba, const uint8_t *buf, uint32_t count);
uint32_t SD_SPI_GetSectorCount(void);

#endif
