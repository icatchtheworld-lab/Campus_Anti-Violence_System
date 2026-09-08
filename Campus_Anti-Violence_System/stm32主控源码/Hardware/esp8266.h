#ifndef __ESP8266_H
#define	__ESP8266_H

#include "stm32f10x.h"
#include "Hardware/Common.h"
#include <stdio.h>
#include <stdbool.h>

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

#if defined ( __CC_ARM   )
#pragma anon_unions
#endif



/******************************* ESP8266 数据类型定义 ***************************/
typedef enum{
	STA,
  AP,
  STA_AP  
} ENUM_Net_ModeTypeDef;


typedef enum{
	 enumTCP,
	 enumUDP,
} ENUM_NetPro_TypeDef;
	

typedef enum{
	Multiple_ID_0 = 0,
	Multiple_ID_1 = 1,
	Multiple_ID_2 = 2,
	Multiple_ID_3 = 3,
	Multiple_ID_4 = 4,
	Single_ID_0 = 5,
} ENUM_ID_NO_TypeDef;
	

typedef enum{
	OPEN = 0,
	WEP = 1,
	WPA_PSK = 2,
	WPA2_PSK = 3,
	WPA_WPA2_PSK = 4,
} ENUM_AP_PsdMode_TypeDef;



/******************************* ESP8266 外部全局变量声明 ***************************/
#define RX_BUF_MAX_LEN     1024                                     //最大接收缓存字节数

extern struct  STRUCT_USARTx_Fram                                  //串口数据帧的处理结构体
{
	char  Data_RX_BUF [ RX_BUF_MAX_LEN ];
	
  union {
    __IO u16 InfAll;
    struct {
		  __IO u16 FramLength       :15;                               // 14:0 
		  __IO u16 FramFinishFlag   :1;                                // 15 
	  } InfBit;
  }; 
	
} strEsp8266_Fram_Record;



/******************************** ESP8266 连接引脚定义 ***********************************/
#define      macESP8266_CH_PD_APBxClock_FUN                   RCC_APB2PeriphClockCmd
#define      macESP8266_CH_PD_CLK                             RCC_APB2Periph_GPIOA  
#define      macESP8266_CH_PD_PORT                            GPIOA
#define      macESP8266_CH_PD_PIN                             GPIO_Pin_5

#define      macESP8266_RST_APBxClock_FUN                     RCC_APB2PeriphClockCmd
#define      macESP8266_RST_CLK                               RCC_APB2Periph_GPIOA
#define      macESP8266_RST_PORT                              GPIOA
#define      macESP8266_RST_PIN                               GPIO_Pin_6

 

#define      macESP8266_USART_BAUD_RATE                       115200

#define      macESP8266_USARTx                                USART2
#define      macESP8266_USART_APBxClock_FUN                   RCC_APB1PeriphClockCmd
#define      macESP8266_USART_CLK                             RCC_APB1Periph_USART2
#define      macESP8266_USART_GPIO_APBxClock_FUN              RCC_APB2PeriphClockCmd
#define      macESP8266_USART_GPIO_CLK                        RCC_APB2Periph_GPIOA    
#define      macESP8266_USART_TX_PORT                         GPIOA   
#define      macESP8266_USART_TX_PIN                          GPIO_Pin_2
#define      macESP8266_USART_RX_PORT                         GPIOA
#define      macESP8266_USART_RX_PIN                          GPIO_Pin_3
#define      macESP8266_USART_IRQ                             USART2_IRQn
#define      macESP8266_USART_INT_FUN                         USART2_IRQHandler



/*********************************************** ESP8266 函数宏定义 *******************************************/
#define     macESP8266_Usart( fmt, ... )           USART_printf ( macESP8266_USARTx, fmt, ##__VA_ARGS__ ) 
#define     macPC_Usart( fmt, ... )                printf ( fmt, ##__VA_ARGS__ )
//#define     macPC_Usart( fmt, ... )                

#define     macESP8266_CH_ENABLE()                 ((void)0)
#define     macESP8266_CH_DISABLE()                ((void)0)

#define     macESP8266_RST_HIGH_LEVEL()            ((void)0)
#define     macESP8266_RST_LOW_LEVEL()             ((void)0)



/****************************************** ESP8266 函数声明 ***********************************************/
void                     ESP8266_Init                        ( void );
void                     ESP8266_Rst                         ( void );
bool                     ESP8266_Cmd                         ( char * cmd, char * reply1, char * reply2, u32 waittime );
uint8_t                  ESP8266_WaitResponse                ( const char * reply1, const char * reply2, u32 waittime );
void                     ESP8266_AT_Test                     ( void );
bool                     ESP8266_Net_Mode_Choose             ( ENUM_Net_ModeTypeDef enumMode );
bool                     ESP8266_JoinAP                      ( char * pSSID, char * pPassWord );
bool                     ESP8266_BuildAP                     ( char * pSSID, char * pPassWord, ENUM_AP_PsdMode_TypeDef enunPsdMode );
bool                     ESP8266_Enable_MultipleId           ( FunctionalState enumEnUnvarnishTx );
bool                     ESP8266_Link_Server                 ( ENUM_NetPro_TypeDef enumE, char * ip, char * ComNum, ENUM_ID_NO_TypeDef id);
bool                     ESP8266_StartOrShutServer           ( FunctionalState enumMode, char * pPortNum, char * pTimeOver );
uint8_t                  ESP8266_Get_LinkStatus              ( void );
uint8_t                  ESP8266_Get_IdLinkStatus            ( void );
uint8_t                  ESP8266_Inquire_ApIp                ( char * pApIp, uint8_t ucArrayLength );
bool                     ESP8266_UnvarnishSend               ( void );
void                     ESP8266_ExitUnvarnishSend           ( void );
void                     ESP8266_SendRawBuffer               ( const uint8_t * pBuf, u32 ulLength );
bool                     ESP8266_SendBuffer                  ( const uint8_t * pBuf, u32 ulLength, ENUM_ID_NO_TypeDef ucId, u32 waittime );
bool                     ESP8266_SendString                  ( FunctionalState enumEnUnvarnishTx, char * pStr, u32 ulStrLength, ENUM_ID_NO_TypeDef ucId );
char *                   ESP8266_ReceiveString               ( FunctionalState enumEnUnvarnishTx );
int                           Month_Str_To_Num                    (char *month_str);
void                       ESP8266_Get_RealTime                (char *time_str);
bool                      ESP8266_Send_Alarm_Time               (void);
void                      ESP8266_Send_DivceID                   (void);
/********************************** 用户需要设置的参数**********************************/
#define      macUser_ESP8266_ApSsid                       "xiaohui"                //要连接的热点的名称"HiwonderESP"
#define      macUser_ESP8266_ApPwd                        "1122334455"           //要连接的热点的密钥"hiwonder"

//#define      macUser_ESP8266_TcpServer_IP                 "192.168.0.11"      //要连接的服务器的 IP
//#define      macUser_ESP8266_TcpServer_Port               "8080"               //要连接的服务器的端口


#define MQTT_USER "AT+MQTTUSERCFG=0,1,\"test1\",\"l4JQEioAnm\",\"version=2018-10-31&res=products%2Fl4JQEioAnm%2Fdevices%2Ftest1&et=1833956099&method=md5&sign=i9er2cmvssPmGPa4miz%2BSA%3D%3D\",0,0,\"\""
//  AT+MQTTCONN 完整指令（连接OneNET服务器）
#define MQTT_CONN_CMD         "AT+MQTTCONN=0,\"mqtts.heclouds.com\",1883,1"
// AT+MQTTSUB 完整指令（订阅主题）
#define MQTT_SUB_CMD          "AT+MQTTSUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post/reply\",0"
//订阅小程序对属性修改的主题
#define  MQTT_SUB_PROPERTY_SET "AT+MQTTSUB=0,\"$sys/l4JQEioAnm/test1/thing/property/set\",0"
//开启net时间
#define NTP_CONFIG_CMD "AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.pool.ntp.org\""
//返回当前时间
#define NTP_GET_TIME_CMD "AT+CIPSNTPTIME?"

/********************************** 外部全局变量 ***************************************/
extern volatile uint8_t ucTcpClosedFlag;
extern volatile uint8_t g_ESP8266RawBusy;
extern volatile uint8_t g_MqttConnected;
extern volatile uint8_t g_MqttSubRecvPending;
extern volatile uint8_t g_MqttTimRecvPending;
extern char g_MqttSubRecvBuf[RX_BUF_MAX_LEN];
extern char g_MqttTimeRecvBuf[RX_BUF_MAX_LEN];



/********************************** 测试函数声明 ***************************************/
void                     ESP8266_StaTcpClient  ( void );
void                     ProcessMQTTSubRecv    ( void );


#endif /* __ESP8266_H */




