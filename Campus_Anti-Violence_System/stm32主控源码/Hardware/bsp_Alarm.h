#ifndef _BSP_FMQ_H
#define _BSP_FMQ_H

#include "stm32f10x.h"

#define FMQ_GPIO_PORT    	GPIOB			            /* GPIO端口 */
#define FMQ_GPIO_CLK 	    RCC_APB2Periph_GPIOB		/* GPIO端口时钟 */
#define FMQ_GPIO_PIN				GPIO_Pin_8			        /* 连接到SCL时钟线的GPIO */



void Alarm_Init(void);//蜂鸣器初始化函数
void Alarm_OFF();
void Alarm_ON();

#endif
