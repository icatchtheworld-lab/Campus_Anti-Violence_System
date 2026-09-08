#ifndef TF_CARD_H
#define TF_CARD_H

#include "stm32f10x.h"
#include "ff.h"

FRESULT TF_Card_Mount(void);
FRESULT TF_Card_Unmount(void);
FRESULT TF_Card_AppendFile(const char *path, const void *data, UINT len, UINT *written);

#endif
