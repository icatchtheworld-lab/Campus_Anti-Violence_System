#include "stm32f10x.h"   
#include "Key.h"
#include "esp8266.h"
#include "LED.h"
extern KeyNum;

//报警类型
//按键报警
#define ALARM_Button AT+MQTTPUB=0,"$sys/l4JQEioAnm/test1/thing/property/post","{\"id\":\"123\"\,\"params\":{\"eventType\":{\"value\":\"Button\"\}}}",0,0

void ButtomDown(void)
{
  if(KeyNum==2)
  {
    bool status;
    status=ESP8266_Cmd(ALARM_Button,"OK",NULL,500);
    if(status)
    {
    LED0_Turn();
    }   
  }
}