
 #ifndef __TIME_H
 #define __TIME_H

 #include "stm32f10x.h"
 #define RX_BUF_MAX_LEN     1024                                     //最大接收缓存字节数
  extern volatile uint8_t g_MqttTimRecvPending;
  extern char g_MqttSubRecvBuf[RX_BUF_MAX_LEN];
 uint8_t Time_GenerateRecordPath(char *path, uint32_t pathSize);

 #endif
