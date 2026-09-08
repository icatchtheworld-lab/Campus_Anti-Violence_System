#ifndef __COMMON_H
#define __COMMON_H



#include "stm32f10x.h"



/******************************* 宏定义 ***************************/
#define            macNVIC_PriorityGroup_x                     NVIC_PriorityGroup_2



/********************************** 函数声明 ***************************************/
void                     USART_printf                       ( USART_TypeDef * USARTx, char * Data, ... );
	typedef _Bool			uint1;
    typedef unsigned char   uint8;
    typedef char			int8;
    typedef unsigned short  uint16;
    typedef short			int16;
    typedef unsigned int    uint32;
    typedef int				int32;
	typedef unsigned int	size_t;


#endif /* __COMMON_H */

