#include "sd_spi.h"

#define SD_SPI                SPI1
#define SD_SPI_RCC            RCC_APB2Periph_SPI1

#define SD_GPIO_RCC           RCC_APB2Periph_GPIOA
#define SD_GPIO               GPIOA

#define SD_CS_PIN             GPIO_Pin_4
#define SD_SCK_PIN            GPIO_Pin_5
#define SD_MISO_PIN           GPIO_Pin_6
#define SD_MOSI_PIN           GPIO_Pin_7

#define SD_CS_HIGH()          GPIO_SetBits(SD_GPIO, SD_CS_PIN)
#define SD_CS_LOW()           GPIO_ResetBits(SD_GPIO, SD_CS_PIN)

#define SD_DUMMY_BYTE         0xFF

typedef enum {
  SD_TYPE_UNKNOWN = 0,
  SD_TYPE_SDSC = 1,
  SD_TYPE_SDHC = 2
} SD_Type;

static SD_Type s_sd_type = SD_TYPE_UNKNOWN;
static uint32_t s_sector_count = 0;

volatile uint8_t g_sd_cmd0_r1;
volatile uint32_t g_sd_cmd0_tmr;
volatile uint8_t g_sd_wait_ready_last;
volatile uint8_t g_sd_select_ok;
volatile uint8_t g_sd_sendcmd_cmd;
volatile uint8_t g_sd_sendcmd_res;

static void SD_SPI_LowSpeed(void);
static void SD_SPI_HighSpeed(void);

static void SD_SPI_SetPrescaler(uint16_t prescaler)
{
  SPI_Cmd(SD_SPI, DISABLE);
  SD_SPI->CR1 &= (uint16_t)~SPI_CR1_BR;
  SD_SPI->CR1 |= prescaler;
  SPI_Cmd(SD_SPI, ENABLE);
}

static uint8_t spi_txrx(uint8_t data)
{
  while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_TXE) == RESET) {}
  SPI_I2S_SendData(SD_SPI, data);
  while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_RXNE) == RESET) {}
  return (uint8_t)SPI_I2S_ReceiveData(SD_SPI);
}

static void sd_send_dummy_clocks(uint8_t n)
{
  uint8_t i;
  SD_CS_HIGH();
  for (i = 0; i < n; i++) {
    spi_txrx(SD_DUMMY_BYTE);
  }
}

static uint8_t sd_wait_ready(uint32_t timeout)
{
  uint8_t r;
  do {
    r = spi_txrx(SD_DUMMY_BYTE);
    g_sd_wait_ready_last = r;
    if (r == 0xFF) return 1;
  } while (timeout--);
  return 0;
}

static uint8_t sd_select(void)
{
  SD_CS_LOW();
  spi_txrx(SD_DUMMY_BYTE);
  g_sd_select_ok = sd_wait_ready(0xFFFF);
  return g_sd_select_ok;
}

static void sd_deselect(void)
{
  SD_CS_HIGH();
  spi_txrx(SD_DUMMY_BYTE);
}

static uint8_t sd_recv_datablock(uint8_t *buff, uint32_t btr)
{
  uint8_t token;
  uint32_t tmr = 0xFFFFF;

  do {
    token = spi_txrx(SD_DUMMY_BYTE);
  } while ((token == 0xFF) && tmr--);

  if (token != 0xFE) {
    return 0;
  }

  while (btr--) {
    *buff++ = spi_txrx(SD_DUMMY_BYTE);
  }

  spi_txrx(SD_DUMMY_BYTE);
  spi_txrx(SD_DUMMY_BYTE);

  return 1;
}

static uint8_t sd_xmit_datablock(const uint8_t *buff, uint8_t token)
{
  uint8_t resp;
  uint16_t wc;

  if (!sd_wait_ready(0xFFFFF)) {
    return 0;
  }

  spi_txrx(token);

  if (token != 0xFD) {
    wc = 512;
    do {
      spi_txrx(*buff++);
    } while (--wc);

    spi_txrx(SD_DUMMY_BYTE);
    spi_txrx(SD_DUMMY_BYTE);

    resp = spi_txrx(SD_DUMMY_BYTE);
    if ((resp & 0x1F) != 0x05) {
      return 0;
    }

    if (!sd_wait_ready(0xFFFFF)) {
      return 0;
    }
  }

  return 1;
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg)
{
  uint8_t n, res;
  uint8_t buf[6];

  g_sd_sendcmd_cmd = cmd;

  if (cmd & 0x80) {
    cmd &= 0x7F;
    res = sd_send_cmd(55, 0);
    if (res > 1) return res;
  }

  buf[0] = 0x40 | cmd;
  buf[1] = (uint8_t)(arg >> 24);
  buf[2] = (uint8_t)(arg >> 16);
  buf[3] = (uint8_t)(arg >> 8);
  buf[4] = (uint8_t)(arg);
  buf[5] = 0x01;

  if (cmd == 0) buf[5] = 0x95;
  if (cmd == 8) buf[5] = 0x87;

  sd_deselect();
  if (!sd_select()) {
    g_sd_sendcmd_res = 0xFF;
    return 0xFF;
  }

  for (n = 0; n < 6; n++) {
    spi_txrx(buf[n]);
  }

  n = 10;
  do {
    res = spi_txrx(SD_DUMMY_BYTE);
  } while ((res & 0x80) && --n);

  g_sd_sendcmd_res = res;

  return res;
}

static uint32_t sd_get_sector_count_from_csd(void)
{
  uint8_t csd[16];
  uint32_t csize;
  uint32_t n;

  if (sd_send_cmd(9, 0) != 0) {
    sd_deselect();
    return 0;
  }
  if (!sd_recv_datablock(csd, 16)) {
    sd_deselect();
    return 0;
  }
  sd_deselect();

  if ((csd[0] >> 6) == 1) {
    csize = (uint32_t)(csd[9] | ((uint32_t)csd[8] << 8) | ((uint32_t)(csd[7] & 0x3F) << 16));
    return (csize + 1U) << 10;
  }

  n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
  csize = (uint32_t)((csd[8] >> 6) | ((uint32_t)csd[7] << 2) | ((uint32_t)(csd[6] & 3) << 10)) + 1;
  return (csize << (n - 9));
}

static void SD_SPI_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(SD_GPIO_RCC, ENABLE);
  RCC_APB2PeriphClockCmd(SD_SPI_RCC, ENABLE);

  GPIO_InitStructure.GPIO_Pin = SD_SCK_PIN | SD_MOSI_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(SD_GPIO, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = SD_MISO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(SD_GPIO, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = SD_CS_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(SD_GPIO, &GPIO_InitStructure);

  SD_CS_HIGH();
}

static void SD_SPI_Peripheral_Init(void)
{
  SPI_InitTypeDef SPI_InitStructure;

  SPI_I2S_DeInit(SD_SPI);

  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
  SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
  SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
  SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init(SD_SPI, &SPI_InitStructure);

  SPI_NSSInternalSoftwareConfig(SD_SPI, SPI_NSSInternalSoft_Set);

  SPI_Cmd(SD_SPI, ENABLE);
}

static void SD_SPI_LowSpeed(void)
{
  SD_SPI_SetPrescaler(SPI_BaudRatePrescaler_256);
}

static void SD_SPI_HighSpeed(void)
{
  SD_SPI_SetPrescaler(SPI_BaudRatePrescaler_16);
}

SD_SPI_Status SD_SPI_Init(void)
{
  volatile uint8_t r1;
  uint8_t ocr[4];
  volatile uint32_t tmr;
  uint8_t retry;

  SD_SPI_GPIO_Init();
  SD_SPI_Peripheral_Init();
  SD_SPI_LowSpeed();

  s_sd_type = SD_TYPE_UNKNOWN;
  s_sector_count = 0;

  r1 = 0xFF;
  for (retry = 0; retry < 3; retry++) {
    sd_send_dummy_clocks(10);
    tmr = 0xFFFF;
    do {
      r1 = sd_send_cmd(0, 0);
      g_sd_cmd0_r1 = r1;
      g_sd_cmd0_tmr = tmr;
    } while ((r1 != 1) && --tmr);
    if (r1 == 1) break;
  }

  if (r1 != 1) {
    sd_deselect();
    return SD_SPI_TIMEOUT;
  }

  r1 = sd_send_cmd(8, 0x1AA);
  if (r1 == 1) {
    ocr[0] = spi_txrx(SD_DUMMY_BYTE);
    ocr[1] = spi_txrx(SD_DUMMY_BYTE);
    ocr[2] = spi_txrx(SD_DUMMY_BYTE);
    ocr[3] = spi_txrx(SD_DUMMY_BYTE);

    if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
      tmr = 0xFFFFF;
      do {
        r1 = sd_send_cmd(0x80 | 41, 1UL << 30);
      } while (r1 && --tmr);

      if (tmr && sd_send_cmd(58, 0) == 0) {
        ocr[0] = spi_txrx(SD_DUMMY_BYTE);
        ocr[1] = spi_txrx(SD_DUMMY_BYTE);
        ocr[2] = spi_txrx(SD_DUMMY_BYTE);
        ocr[3] = spi_txrx(SD_DUMMY_BYTE);
        s_sd_type = (ocr[0] & 0x40) ? SD_TYPE_SDHC : SD_TYPE_SDSC;
      }
    }
  } else {
    tmr = 0xFFFFF;
    do {
      r1 = sd_send_cmd(0x80 | 41, 0);
    } while (r1 && --tmr);

    if (tmr) {
      s_sd_type = SD_TYPE_SDSC;
      sd_send_cmd(16, 512);
    }
  }

  sd_deselect();

  if (s_sd_type == SD_TYPE_UNKNOWN) {
    return SD_SPI_ERR;
  }

  SD_SPI_HighSpeed();

  s_sector_count = sd_get_sector_count_from_csd();

  return SD_SPI_OK;
}

SD_SPI_Status SD_SPI_ReadBlocks(uint32_t lba, uint8_t *buf, uint32_t count)
{
  uint32_t addr;
  uint8_t retry;

  if (s_sd_type == SD_TYPE_UNKNOWN) return SD_SPI_ERR;

  while (count--) {
    addr = (s_sd_type == SD_TYPE_SDHC) ? lba : (lba * 512U);

    for (retry = 0; retry < 3; retry++) {
      if (sd_send_cmd(17, addr) != 0) {
        sd_deselect();
        continue;
      }
      if (sd_recv_datablock(buf, 512)) {
        break;
      }
      sd_deselect();
    }
    if (retry >= 3) {
      return SD_SPI_ERR;
    }

    sd_deselect();

    buf += 512;
    lba++;
  }

  return SD_SPI_OK;
}

SD_SPI_Status SD_SPI_WriteBlocks(uint32_t lba, const uint8_t *buf, uint32_t count)
{
  uint32_t addr;
  uint8_t retry;

  if (s_sd_type == SD_TYPE_UNKNOWN) return SD_SPI_ERR;

  while (count--) {
    addr = (s_sd_type == SD_TYPE_SDHC) ? lba : (lba * 512U);

    for (retry = 0; retry < 3; retry++) {
      if (sd_send_cmd(24, addr) != 0) {
        sd_deselect();
        continue;
      }

      if (sd_xmit_datablock(buf, 0xFE)) {
        break;
      }

      sd_deselect();
    }

    if (retry >= 3) {
      sd_deselect();
      return SD_SPI_ERR;
    }

    sd_deselect();

    buf += 512;
    lba++;
  }

  return SD_SPI_OK;
}

uint32_t SD_SPI_GetSectorCount(void)
{
  return s_sector_count;
}
