#include "stm32f10x.h"
#include "Time.h"
#include "Hardware/esp8266.h"
#include <stdio.h>
#include <string.h> 


uint8_t Time_GenerateRecordPath(char *path, uint32_t pathSize)
{
  static uint16_t record_index = 0;
  static char last_time_fmt[32] = "record";
  char time_fmt[32];
  uint8_t i;
  int written;

  if (path == 0 || pathSize == 0)//别给我空指针
  {
    return 0;
  }

	// 如果有新时间到达，更新一下索引策略
	if (g_MqttTimRecvPending)
	{
		g_MqttTimRecvPending = 0; // 清除标志
		record_index = 0; // 时间更新后，序号从0开始
		if (g_MqttTimeRecvBuf[0] != '\0')
		{
			strncpy(last_time_fmt, g_MqttTimeRecvBuf, sizeof(last_time_fmt) - 1);
			last_time_fmt[sizeof(last_time_fmt) - 1] = '\0';
		}
	}
	
	// 格式化时间字符串，替换无效字符
	strncpy(time_fmt, last_time_fmt, sizeof(time_fmt) - 1);
	time_fmt[sizeof(time_fmt) - 1] = '\0';
	for (i = 0; time_fmt[i] != '\0'; ++i)
	{
		if (time_fmt[i] == ' ' || time_fmt[i] == ':' || time_fmt[i] == '/' || time_fmt[i] == '\\')
		{
			time_fmt[i] = '_';
		}
	}
	
	// 拼接最终的文件名，格式如: "0:/Mon_Apr_13_18_48_42_2026_0.wav"
	written = snprintf(path, pathSize, "0:/%s_%u.wav", time_fmt, record_index);
	if (written < 0 || (uint32_t)written >= pathSize)
	{
		return 0;
	}
	
	record_index++; // 序号自增，用于处理重名文件
	return 1;
}

	




