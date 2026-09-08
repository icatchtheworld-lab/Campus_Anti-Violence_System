#include "tf_card.h"

#include "diskio.h"

static FATFS s_fs;
static uint8_t s_mounted = 0;

FRESULT TF_Card_Mount(void)
{
	FRESULT fr;

	if (s_mounted) {
		return FR_OK;
	}

	fr = f_mount(&s_fs, "0:", 1);
	if (fr == FR_OK) {
		s_mounted = 1;
	}
	return fr;
}

FRESULT TF_Card_Unmount(void)
{
	FRESULT fr;

	if (!s_mounted) {
		return FR_OK;
	}

	fr = f_mount(0, "0:", 1);
	if (fr == FR_OK || fr == FR_NOT_ENABLED) {
		s_mounted = 0;
		return FR_OK;
	}
	return fr;
}

FRESULT TF_Card_AppendFile(const char *path, const void *data, UINT len, UINT *written)
{
	FRESULT fr;
	FIL fil;

	if (written) {
		*written = 0;
	}

	fr = TF_Card_Mount();
	if (fr != FR_OK) {
		return fr;
	}

	fr = f_open(&fil, path, FA_OPEN_ALWAYS | FA_WRITE);
	if (fr != FR_OK) {
		return fr;
	}

	fr = f_lseek(&fil, f_size(&fil));
	if (fr != FR_OK) {
		(void)f_close(&fil);
		return fr;
	}

	fr = f_write(&fil, data, len, written);
	(void)f_close(&fil);
	return fr;
}
