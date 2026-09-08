

/*****************辰哥单片机设计******************
											STM32
 * 项目			:	ESP8266模块通信实验                     
 * 版本			: V1.0
 * 日期			: 2024.9.30
 * MCU			:	STM32F103C8T6
 * 接口			:	串口2					
 * BILIBILI	:	辰哥单片机设计
 * CSDN			:	辰哥单片机设计
 * 作者			:	辰哥 

**********************BEGIN***********************/

#include "esp8266.h"
#include "common.h"
#include <stdio.h>  
#include <string.h>  
#include <stdbool.h>
#include "System/Delay.h"

#define ESP8266_GetTime  "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"time\\\":{\\\"value\\\":\\\"%s\\\"\\}\}}\",0,0"
#define ESP8266_GetDevID "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"deviceName\\\":{\\\"value\\\":\\\"%s\\\"\\}\}}\",0,0"
void Get_STM32_UID(char *uid_str);


//修改设备名称
//#define Modify_DevName "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"deviceName\\\":{\\\"value\\\":\\\"\\u522b\\u52a8\\\"\\}\\}}\",0,0"

volatile uint8_t ucTcpClosedFlag = 0;
volatile uint8_t g_ESP8266RawBusy = 0;
volatile uint8_t g_MqttConnected = 0;
volatile uint8_t g_MqttSubRecvPending = 0;//标记接收回复状态
volatile uint8_t  g_MqttTimRecvPending =0;//标记接收时间状态
char g_MqttSubRecvBuf[RX_BUF_MAX_LEN] = { 0 };
char g_MqttTimeRecvBuf[RX_BUF_MAX_LEN] = { 0 };
static char g_MqttReplyCmdBuf[512] = { 0 };
static char g_MqttPostCmdBuf[512] = { 0 };
static char g_MqttMsgIdBuf[32] = { 0 };
static char g_MqttDeviceNameBuf[128] = { 0 };
static char g_MqttDeviceNameUnicodeBuf[256] = { 0 };
static char g_MqttDeviceIdCmdBuf[512] = { 0 };
static char g_MqttAlarmTimeCmdBuf[350] = { 0 };
static char g_MqttTimeStrBuf[32] = { 0 };

char cStr [ 1500 ] = { 0 };

//static void                   ESP8266_GPIO_Config                 ( void );
static void                   ESP8266_USART_Config                ( void );
static void                   ESP8266_USART_NVIC_Configuration    ( void );
static void                   UTF8_To_UnicodeEscaped              ( const char * src, char * dst, size_t dst_size );
static void                   ESP8266_SendRawBytes                ( const uint8_t * pBuf, u32 ulLength );
static uint8_t                ESP8266_WaitRxQuiet                 ( u32 quiettime, u32 waittime );
static bool                   ESP8266_MQTTConnectReady            ( u32 waittime );
struct  STRUCT_USARTx_Fram strEsp8266_Fram_Record = { 0 };

/**
  * @brief  ESP8266英文月份转数字月份
  * @param  month_str 英文月份字符串
  * @retval 数字月份
  */
int Month_Str_To_Num(char *month_str)
{
if(strcmp(month_str,"Jan")==0)return 1;
else if(strcmp(month_str,"Feb")==0)return 2;
else if(strcmp(month_str,"Mar")==0)return 3;
else if(strcmp(month_str,"Apr")==0)return 4;
else if(strcmp(month_str,"May")==0)return 5;
else if(strcmp(month_str,"Jun")==0)return 6;
else if(strcmp(month_str,"Jul")==0)return 7;
else if(strcmp(month_str,"Aug")==0)return 8;
else if(strcmp(month_str,"Sep")==0)return 9;
else if(strcmp(month_str,"Oct")==0)return 10;
else if(strcmp(month_str,"Nov")==0)return 11;
else if(strcmp(month_str,"Dec")==0)return 12;
return 0;

}

static void UTF8_To_UnicodeEscaped ( const char * src, char * dst, size_t dst_size )
{
	size_t out_len = 0;
	unsigned int codepoint;

	if ( dst_size == 0 )
		return;

	while ( *src && out_len + 1 < dst_size )
	{
		unsigned char ch = ( unsigned char ) *src;

		if ( ch < 0x80 )
		{
			codepoint = ch;
			src += 1;
		}
		else if ( ( ch & 0xE0 ) == 0xC0 && src [ 1 ] != '\0' )
		{
			codepoint = ( ( unsigned int ) ( ch & 0x1F ) << 6 ) |
						( ( unsigned int ) ( ( unsigned char ) src [ 1 ] & 0x3F ) );
			src += 2;
		}
		else if ( ( ch & 0xF0 ) == 0xE0 && src [ 1 ] != '\0' && src [ 2 ] != '\0' )
		{
			codepoint = ( ( unsigned int ) ( ch & 0x0F ) << 12 ) |
						( ( unsigned int ) ( ( unsigned char ) src [ 1 ] & 0x3F ) << 6 ) |
						( ( unsigned int ) ( ( unsigned char ) src [ 2 ] & 0x3F ) );
			src += 3;
		}
		else
		{
			codepoint = '?';
			src += 1;
		}

		if ( codepoint <= 0xFFFF )
		{
			int written;

			written = snprintf ( dst + out_len, dst_size - out_len, "\\u%04X", codepoint );
			if ( written <= 0 || ( size_t ) written >= ( dst_size - out_len ) )
				break;
			out_len += ( size_t ) written;
		}
	}

	dst [ out_len ] = '\0';
}

/**
  * @brief   获取实时时间字符串，格式：YYYY-MM-DD HH:MM:SS
  * @param  time_str 时间字符串指针
  * @retval 无
  */
void ESP8266_Get_RealTime(char *time_str)
{
 int retry=3;//最多尝试3次
 if (time_str != 0)
 {
	time_str[0] = '\0';
 }
 while(retry--)
 {
	//1清空缓存区，准备接收新的数据
	memset(strEsp8266_Fram_Record.Data_RX_BUF,0,sizeof(strEsp8266_Fram_Record.Data_RX_BUF));
	strEsp8266_Fram_Record.InfBit.FramLength=0;

	//2发送获取时间指令
	if(ESP8266_Cmd(NTP_GET_TIME_CMD,"OK",NULL,2000))
	{
		//解析esp8266返回的时间格式 格式是：+CIPSNTPTIME:Wed Feb 25 14:28:53 2026
		char *p=strstr((char*)strEsp8266_Fram_Record.Data_RX_BUF,"+CIPSNTPTIME:");
		if(p)
		{
			p+=strlen("+CIPSNTPTIME:");//跳过"+CIPSNTPTIME:"
			//解析格式
			char month_str[4];
			int year,month,day,hour,minute,second;
			if (sscanf(p,"%*s %3s %d %d:%d:%d %d",month_str, &day, &hour, &minute, &second, &year) != 6)
			{
				continue;
			}
			month=Month_Str_To_Num(month_str);
			if (month <= 0)
			{
				continue;
			}
			//格式化时间字符串
			sprintf(time_str, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
			strncpy(g_MqttTimeRecvBuf, time_str, RX_BUF_MAX_LEN - 1);
			g_MqttTimeRecvBuf[RX_BUF_MAX_LEN - 1] = '\0';
			g_MqttTimRecvPending = 1;
			return;
		}
	}
	Delay_ms(500);//等待500ms
 }
printf("get time error \r\n");
}

/**
  * @brief   发送设备标识符
  * @retval 无
  */
void ESP8266_Send_DivceID(void)
{
char device_id[32]={0};
 Get_STM32_UID(device_id);
 sprintf(g_MqttDeviceIdCmdBuf,
	ESP8266_GetDevID,
	device_id);
	if(ESP8266_Cmd(g_MqttDeviceIdCmdBuf,"OK",NULL,2000))
	{
		printf("Send deviceID was successful.");
	}
	else
	{
		printf("Send deviceID failed");
	}
}

/**
 * @brief  发送带实时时间的MQTT指令
 */
bool ESP8266_Send_Alarm_Time(void)
{    
	   //获取实时时间
	   ESP8266_Get_RealTime(g_MqttTimeStrBuf);
	   if (g_MqttTimeStrBuf[0] == '\0')
	   {
		   printf("Send time failed");
		   return false;
	   }
  printf("Current Time:%s\r\n",g_MqttTimeStrBuf);
	   //拼接mqtt指令
	       sprintf(g_MqttAlarmTimeCmdBuf, 
          ESP8266_GetTime,
	            g_MqttTimeStrBuf);
			if(ESP8266_Cmd(g_MqttAlarmTimeCmdBuf,"OK",NULL,2000))
			{
				printf("Send time was successful.");
        return true;
			}
			else
			{
				printf("Send time failed");
        return false;
			}   
}

/**
  * @brief  ESP8266 （Sta Tcp Client）透传
  * @param  无
  * @retval 无
  */
void ESP8266_StaTcpClient ( void )
{
//	uint8_t ucStatus;
//	char cStr [ 100 ] = { 0 };
  printf ( "\r\n Configuring TCP ESP8266 ......\r\n" );

	//macESP8266_CH_ENABLE();
	
	ESP8266_AT_Test ();//发送AT指令测试
	
	ESP8266_Net_Mode_Choose ( STA );//AT+CWMODE=1

  while ( ! ESP8266_JoinAP ( macUser_ESP8266_ApSsid, macUser_ESP8266_ApPwd ) );	//自己设置的wifi账号和密码
	//ESP8266_Cmd ( "AT+CIFSR", "OK", 0, 1000 );//查询IP地址
	//ESP8266_Cmd ( "AT+CIPMUX=1", "OK", 0, 1000 );//开启多连接模式
	//ESP8266_Cmd ( "AT+CIPSERVER=1,8288", "OK", 0, 1000 );//创建一个TCP连接通道
	//printf( "\r\n ESP8266 TCP Setup Completed\r\n" ); 
	
  
  
    printf ( "\r\n Configurating MQTT ESP8266 ......\r\n" );

    // 配置MQTT用户参数
    ( void ) ESP8266_Cmd ( MQTT_USER, "OK", 0, 1000 );
    if ( ! ESP8266_MQTTConnectReady ( 10000 ) )
      printf ( "MQTT conn fail\r\n" );
    else
    {
      if ( ! ESP8266_Cmd ( MQTT_SUB_CMD, "OK", 0, 1000 ) )
        printf ( "MQTT sub reply fail\r\n" );
      if ( ! ESP8266_Cmd ( MQTT_SUB_PROPERTY_SET, "OK", 0, 1000 ) )
        printf ( "MQTT sub set fail\r\n" );
    }
    //开启时间
    if ( ! ESP8266_Cmd ( NTP_CONFIG_CMD, "OK", NULL, 1000 ) )
      printf ( "NTP cfg fail\r\n" );
    //获取时间
   // ESP8266_Cmd(NTP_GET_TIME_CMD,"OK",NULL,2000);
    //将时间存储到time_str
    char time_str[32];
    ESP8266_Get_RealTime(time_str);
    //测试指令
   // ESP8266_Cmd(ALARM_Button,"OK",NULL,500);
   
}


/**
  * @brief  ESP8266初始化函数
  * @param  无
  * @retval 无
  */
void ESP8266_Init ( void )
{
//	ESP8266_GPIO_Config (); 
	
	ESP8266_USART_Config (); 
	
	
//	macESP8266_RST_HIGH_LEVEL();

//	macESP8266_CH_DISABLE();
	
	
}


/**
  * @brief  初始化ESP8266用到的GPIO引脚
  * @param  无
  * @retval 无
  */



/**
  * @brief  初始化ESP8266用到的 USART
  * @param  无
  * @retval 无
  */
static void ESP8266_USART_Config ( void )
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	
	
	/* config USART clock */
	macESP8266_USART_APBxClock_FUN ( macESP8266_USART_CLK, ENABLE );
	macESP8266_USART_GPIO_APBxClock_FUN ( macESP8266_USART_GPIO_CLK, ENABLE );
	
	/* USART GPIO config */
	/* Configure USART Tx as alternate function push-pull */
	GPIO_InitStructure.GPIO_Pin =  macESP8266_USART_TX_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(macESP8266_USART_TX_PORT, &GPIO_InitStructure);  
  
	/* Configure USART Rx as input floating */
	GPIO_InitStructure.GPIO_Pin = macESP8266_USART_RX_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(macESP8266_USART_RX_PORT, &GPIO_InitStructure);
	
	/* USART1 mode config */
	USART_InitStructure.USART_BaudRate = macESP8266_USART_BAUD_RATE;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No ;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(macESP8266_USARTx, &USART_InitStructure);
	
	
	/* 中断配置 */
	USART_ITConfig ( macESP8266_USARTx, USART_IT_RXNE, ENABLE ); //使能串口接收中断 
	USART_ITConfig ( macESP8266_USARTx, USART_IT_IDLE, ENABLE ); //使能串口总线空闲中断 	

	ESP8266_USART_NVIC_Configuration ();
	USART_Cmd(macESP8266_USARTx, ENABLE);
	
}


/**
  * @brief  配置 ESP8266 USART 的 NVIC 中断
  * @param  无
  * @retval 无
  */
static void ESP8266_USART_NVIC_Configuration ( void )
{
	NVIC_InitTypeDef NVIC_InitStructure; 
	
	
	/* Configure the NVIC Preemption Priority Bits */  
	NVIC_PriorityGroupConfig ( macNVIC_PriorityGroup_x );

	/* Enable the USART2 Interrupt */
	NVIC_InitStructure.NVIC_IRQChannel = macESP8266_USART_IRQ;	 
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

}


/*
 * 函数名：ESP8266_Rst
 * 描述  ：重启WF-ESP8266模块
 * 输入  ：无
 * 返回  : 无
 * 调用  ：被 ESP8266_AT_Test 调用
 */
void ESP8266_Rst ( void )
{
	#if 0
	 ESP8266_Cmd ( "AT+RST", "OK", "ready", 2500 );   	
	
	#else
	 macESP8266_RST_LOW_LEVEL();
	 Delay_ms ( 500 ); 
	 macESP8266_RST_HIGH_LEVEL();
	#endif

}


/*
 * 函数名：ESP8266_Cmd
 * 描述  ：对WF-ESP8266模块发送AT指令
 * 输入  ：cmd，待发送的指令
 *         reply1，reply2，期待的响应，为NULL表不需响应，两者为或逻辑关系
 *         waittime，等待响应的时间
 * 返回  : 1，指令发送成功
 *         0，指令发送失败
 * 调用  ：被外部调用
 */
bool ESP8266_Cmd ( char * cmd, char * reply1, char * reply2, u32 waittime )
{
    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    //将接收到的数据通过uart2发送给esp8266
    macESP8266_Usart("%s\r\n", cmd);
  
    if ( ( reply1 == 0 ) && ( reply2 == 0 ) )
        return true;
  
    if (!ESP8266_WaitResponse(reply1, reply2, waittime))
        return false;

    strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength] = '\0';
    return true;
}


static uint8_t ESP8266_WaitRxQuiet ( u32 quiettime, u32 waittime )
{
	u32 elapsed = 0;
	u32 quiet = 0;
	u16 lastLength = strEsp8266_Fram_Record.InfBit.FramLength;

	while ( 1 )
	{
		u16 currentLength = strEsp8266_Fram_Record.InfBit.FramLength;

		strEsp8266_Fram_Record.Data_RX_BUF [ currentLength ] = '\0';

		if ( currentLength != lastLength )
		{
			lastLength = currentLength;
			quiet = 0;
		}
		else if ( quiet >= quiettime )
		{
			return 1;
		}
		else
		{
			quiet++;
		}

		if ( elapsed >= waittime )
			break;

		Delay_ms ( 1 );
		elapsed++;
	}

	return ( quiet >= quiettime ) ? 1 : 0;
}


static bool ESP8266_MQTTConnectReady ( u32 waittime )
{
	u32 elapsed = 0;

	if ( g_MqttConnected )
		return true;

	strEsp8266_Fram_Record.InfBit.FramLength = 0;
	strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;

	macESP8266_Usart ( "%s\r\n", MQTT_CONN_CMD );

	while ( elapsed < waittime )
	{
		strEsp8266_Fram_Record.Data_RX_BUF [ strEsp8266_Fram_Record.InfBit.FramLength ] = '\0';
		if ( g_MqttConnected || strstr ( strEsp8266_Fram_Record.Data_RX_BUF, "+MQTTCONNECTED" ) )
		{
			g_MqttConnected = 1;
			break;
		}
		Delay_ms ( 1 );
		elapsed++;
	}

	if ( ! g_MqttConnected )
		return false;

	( void ) ESP8266_WaitRxQuiet ( 50, 200 );

	strEsp8266_Fram_Record.Data_RX_BUF [ strEsp8266_Fram_Record.InfBit.FramLength ] = '\0';
	return true;
}


uint8_t ESP8266_WaitResponse ( const char * reply1, const char * reply2, u32 waittime )
{
	u32 elapsed = 0;
	while ( 1 )
	{
		strEsp8266_Fram_Record .Data_RX_BUF [ strEsp8266_Fram_Record .InfBit .FramLength ] = '\0';

		if ( ( reply1 != 0 ) && strstr ( strEsp8266_Fram_Record .Data_RX_BUF, reply1 ) )
			return 1;//这里Data_RX_BUF不能单独用匹配回复的方式 来判断    尤其是MQTT_CONN_CMD对这条指令的解析 这条指令的耗时很久5s
    //正是因为各个AT指令回复时间不一致，导致....
    //明天重新单独调试esp8266模块 先给它重置  确定每一条指令的时间   单独为各个指令确定好合适的时间 避免把栈压爆
    //至于录音出问题是上下文/共享状态/栈占用更容易出问题
		if ( ( reply2 != 0 ) && strstr ( strEsp8266_Fram_Record .Data_RX_BUF, reply2 ) )
			return 1;

		if ( elapsed >= waittime )
			break;

		Delay_ms ( 1 );
		elapsed++;
	}
 
	strEsp8266_Fram_Record .Data_RX_BUF [ strEsp8266_Fram_Record .InfBit .FramLength ] = '\0';
	return 0;
 }


/*
 * 函数名：ESP8266_AT_Test
 * 描述  ：对WF-ESP8266模块进行AT测试启动
 * 输入  ：无
 * 返回  : 无
 * 调用  ：被外部调用
 */
void ESP8266_AT_Test ( void )
{
	ESP8266_Cmd ( "AT", "OK", 0, 500 );
}


/*
 * 函数名：ESP8266_Net_Mode_Choose
 * 描述  ：WF-ESP8266模块选择工作模式
 * 输入  ：enumMode，工作模式
 * 返回  : 1，选择成功
 *         0，选择失败
 * 调用  ：被外部调用
 */
bool ESP8266_Net_Mode_Choose ( ENUM_Net_ModeTypeDef enumMode )
{
	switch ( enumMode )
	{
		case STA:
			return ESP8266_Cmd ( "AT+CWMODE=1", "OK", 0, 500 );
		
		case AP:
			return ESP8266_Cmd ( "AT+CWMODE=2", "OK", 0, 500 );
		
		case STA_AP:
			return ESP8266_Cmd ( "AT+CWMODE=3", "OK", 0, 500 );
		
		default:
			return false;
	}
}


/*
 * 函数名：ESP8266_JoinAP
 * 描述  ：WF-ESP8266模块连接外部WiFi热点
 * 输入  ：pSSID，WiFi名称字符串
 *       ：pPassWord，WiFi密码字符串
 * 返回  : 1，连接成功
 *         0，连接失败
 * 调用  ：被外部调用
 */
bool ESP8266_JoinAP ( char * pSSID, char * pPassWord )
{
	char cCmd [120];

	sprintf ( cCmd, "AT+CWJAP=\"%s\",\"%s\"", pSSID, pPassWord );
	
	return ESP8266_Cmd ( cCmd, "OK", 0, 5000 );
}


/*
 * 函数名：ESP8266_Link_Server
 * 描述  ：WF-ESP8266模块连接外部服务器
 * 输入  ：enumE，网络协议
 *       ：ip，服务器IP字符串
 *       ：ComNum，服务器端口字符串
 *       ：id，模块连接服务器的ID
 * 返回  : 1，连接成功
 *         0，连接失败
 * 调用  ：被外部调用
 */
bool ESP8266_Link_Server ( ENUM_NetPro_TypeDef enumE, char * ip, char * ComNum, ENUM_ID_NO_TypeDef id )
{
	char cStr [100] = { 0 }, cCmd [120];

  switch (  enumE )
  {
		case enumTCP:
		  sprintf ( cStr, "\"%s\",\"%s\",%s", "TCP", ip, ComNum );
		  break;
		
		case enumUDP:
		  sprintf ( cStr, "\"%s\",\"%s\",%s", "UDP", ip, ComNum );
		  break;
		
		default:
			break;
  }

  if ( id < 5 )
    sprintf ( cCmd, "AT+CIPSTART=%d,%s", id, cStr);

  else
	  sprintf ( cCmd, "AT+CIPSTART=%s", cStr );

	return ESP8266_Cmd ( cCmd, "OK", "ALREAY CONNECT", 4000 );
}


static void ESP8266_SendRawBytes ( const uint8_t * pBuf, u32 ulLength )
{
	u32 ulIndex;

	for ( ulIndex = 0; ulIndex < ulLength; ulIndex ++ )
	{
 		USART_SendData ( macESP8266_USARTx, pBuf [ ulIndex ] );
 		while ( USART_GetFlagStatus ( macESP8266_USARTx, USART_FLAG_TXE ) == RESET );
 	}
 }
 
 
/*
 * 函数名：ESP8266_SendRawBuffer
 * 描述  ：发送原始数据缓冲区
 * 输入  ：pBuf，待发送的数据缓冲区
 *         ulLength，待发送的数据长度
 * 返回  : 无
 * 调用  ：被外部调用
 */
void ESP8266_SendRawBuffer ( const uint8_t * pBuf, u32 ulLength )
{
	if ( ( pBuf == 0 ) || ( ulLength == 0 ) )
		return;

	ESP8266_SendRawBytes ( pBuf, ulLength );
 }
 
 
/*
 * 函数名：ESP8266_SendBuffer
 * 描述  ：发送数据缓冲区
 * 输入  ：pBuf，待发送的数据缓冲区
 *         ulLength，待发送的数据长度
 *         ucId，模块连接服务器的ID
 *         waittime，等待响应的时间
 * 返回  : 1，发送成功
 *         0，发送失败
 * 调用  ：被外部调用
 */
bool ESP8266_SendBuffer ( const uint8_t * pBuf, u32 ulLength, ENUM_ID_NO_TypeDef ucId, u32 waittime )
{
	char cCmd [ 32 ];
 
	if ( ( pBuf == 0 ) || ( ulLength == 0 ) )
		return false;

	if ( ucId < 5 )
		sprintf ( cCmd, "AT+CIPSEND=%d,%lu", ucId, ulLength );
	else
		sprintf ( cCmd, "AT+CIPSEND=%lu", ulLength );

	strEsp8266_Fram_Record .InfBit .FramLength = 0;
	strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
	macESP8266_Usart ( "%s\r\n", cCmd );

	if ( ! ESP8266_WaitResponse ( ">", 0, waittime ) )
	{
		printf ( "ESP send wait prompt timeout, len=%lu\r\n", (unsigned long)ulLength );
		printf ( "%s\r\n", strEsp8266_Fram_Record .Data_RX_BUF );
		return false;
	}

	strEsp8266_Fram_Record .InfBit .FramLength = 0;
	strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
	ESP8266_SendRawBytes ( pBuf, ulLength );

	if ( ! ESP8266_WaitResponse ( "SEND OK", "SEND FAIL", waittime ) )
	{
		printf ( "ESP send wait sendok timeout, len=%lu\r\n", (unsigned long)ulLength );
		printf ( "%s\r\n", strEsp8266_Fram_Record .Data_RX_BUF );
		return false;
	}

	if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "SEND FAIL" ) )
	{
		printf ( "ESP send fail, len=%lu\r\n", (unsigned long)ulLength );
		return false;
	}

	return ( bool ) strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "SEND OK" );
 }
 
 
/*
 * 函数名：ESP8266_UnvarnishSend
 * 描述  ：配置WF-ESP8266模块进入透传发送
 * 输入  ：无
 * 返回  : 1，配置成功
 *         0，配置失败
 * 调用  ：被外部调用
 */
bool ESP8266_UnvarnishSend ( void )
{
	return ESP8266_Cmd ( "AT+CIPMODE=1", "OK", 0, 1000 );
}
 
 
/*
 * 函数名：ESP8266_ExitUnvarnishSend
 * 描述  ：配置WF-ESP8266模块退出透传模式
 * 输入  ：无
 * 返回  : 无
 * 调用  ：被外部调用
 */
void ESP8266_ExitUnvarnishSend ( void )
{
	Delay_ms( 1000 );
	
	macESP8266_Usart ( "+++" );
	
	Delay_ms( 500 ); 
	
}


/*
 * 函数名：ESP8266_SendString
 * 描述  ：WF-ESP8266模块发送字符串
 * 输入  ：enumEnUnvarnishTx，声明是否已使能了透传模式
 *       ：pStr，要发送的字符串
 *       ：ulStrLength，要发送的字符串的字节数
 *       ：ucId，哪个ID发送的字符串
 * 返回  : 1，发送成功
 *         0，发送失败
 * 调用  ：被外部调用
 */
bool ESP8266_SendString ( FunctionalState enumEnUnvarnishTx, char * pStr, u32 ulStrLength, ENUM_ID_NO_TypeDef ucId )
{
	char cStr [20];
	bool bRet = false;
	
		
	if ( enumEnUnvarnishTx )
	{
		macESP8266_Usart ( "%s", pStr );
		
		bRet = true;
		
	}

	else
	{
		if ( ucId < 5 )
			sprintf ( cStr, "AT+CIPSEND=%d,%d", ucId, ulStrLength + 2 );

		else
			sprintf ( cStr, "AT+CIPSEND=%d", ulStrLength + 2 );
		
		ESP8266_Cmd ( cStr, "> ", 0, 1000 );

		bRet = ESP8266_Cmd ( pStr, "SEND OK", 0, 1000 );
  }
	
	return bRet;

}


/*
 * 函数名：ESP8266_ReceiveString
 * 描述  ：WF-ESP8266模块接收字符串
 * 输入  ：enumEnUnvarnishTx，声明是否已使能了透传模式
 * 返回  : 接收到的字符串首地址
 * 调用  ：被外部调用
 */
char * ESP8266_ReceiveString ( FunctionalState enumEnUnvarnishTx )
{
	char * pRecStr = 0;
	
	
	strEsp8266_Fram_Record .InfBit .FramLength = 0;
	strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
	
	while ( ! strEsp8266_Fram_Record .InfBit .FramFinishFlag );
	strEsp8266_Fram_Record .Data_RX_BUF [ strEsp8266_Fram_Record .InfBit .FramLength ] = '\0';
	
	if ( enumEnUnvarnishTx )
		pRecStr = strEsp8266_Fram_Record .Data_RX_BUF;
	
	else 
	{
		if ( strstr ( strEsp8266_Fram_Record .Data_RX_BUF, "+IPD" ) )
			pRecStr = strEsp8266_Fram_Record .Data_RX_BUF;

	}

	return pRecStr;
	
}

/*
 * 函数名：ProcessMQTTSubRecv
 * 描述  ：处理MQTT订阅消息，特别是property/set消息
 * 输入  ：无
 * 返回  : 无
 * 调用  ：被串口中断处理函数调用
 */
void ProcessMQTTSubRecv(void)
{
	char *pMsg = g_MqttSubRecvBuf;
	char *pPayload = NULL;
	char *pId = NULL;
	char *pIdEnd = NULL;
	char *pName = NULL;
	char *pNameEnd = NULL;
	size_t id_len;
	size_t name_len;

	if (g_MqttSubRecvPending == 0)
		return;

	if (strstr(pMsg, "+MQTTSUBRECV:0,\"$sys/l4JQEioAnm/test1/thing/property/set\"") == NULL)
		goto EXIT_PROCESS;

	pPayload = strchr(pMsg, '{');
	if (pPayload == NULL)
		goto EXIT_PROCESS;

	pId = strstr(pPayload, "\"id\":\"");
	if (pId == NULL)
		goto EXIT_PROCESS;

	pId += 6;
	pIdEnd = strchr(pId, '"');
	if (pIdEnd == NULL)
		goto EXIT_PROCESS;

	id_len = (size_t)(pIdEnd - pId);
	if (id_len >= sizeof(g_MqttMsgIdBuf))
		id_len = sizeof(g_MqttMsgIdBuf) - 1;

	memcpy(g_MqttMsgIdBuf, pId, id_len);
	g_MqttMsgIdBuf[id_len] = '\0';

	snprintf(g_MqttReplyCmdBuf, sizeof(g_MqttReplyCmdBuf),
		"AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/set_reply\",\"{\\\"id\\\":\\\"%s\\\"\\,\\\"version\\\":\\\"1.0\\\"\\,\\\"code\\\":200\\,\\\"message\\\":\\\"success\\\"}\",0,0",
		g_MqttMsgIdBuf);
	//printf("Send property set reply: %s\r\n", reply_cmd);

	ESP8266_Cmd(g_MqttReplyCmdBuf, "OK", NULL, 2000);
	printf("send ok");
	pName = strstr(pPayload, "\"deviceName\":\"");
	if (pName == NULL)
		goto EXIT_PROCESS;

	pName += 14;
	pNameEnd = strchr(pName, '"');
	if (pNameEnd == NULL)
		goto EXIT_PROCESS;

	name_len = (size_t)(pNameEnd - pName);
	if (name_len >= sizeof(g_MqttDeviceNameBuf))
		name_len = sizeof(g_MqttDeviceNameBuf) - 1;

	memcpy(g_MqttDeviceNameBuf, pName, name_len);
	g_MqttDeviceNameBuf[name_len] = '\0';
	UTF8_To_UnicodeEscaped(g_MqttDeviceNameBuf, g_MqttDeviceNameUnicodeBuf, sizeof(g_MqttDeviceNameUnicodeBuf));

	snprintf(g_MqttPostCmdBuf, sizeof(g_MqttPostCmdBuf),
		"AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"deviceName\\\":{\\\"value\\\":\\\"%s\\\"\\}}}\",0,0",
		g_MqttDeviceNameUnicodeBuf);
	//printf("Send deviceName post: %s\r\n", post_cmd);
	printf("send ok2");
	Delay_ms(100);
	ESP8266_Cmd(g_MqttPostCmdBuf, "OK", NULL, 2000);

EXIT_PROCESS:
	g_MqttSubRecvBuf[0] = '\0';
	g_MqttSubRecvPending = 0;
}
