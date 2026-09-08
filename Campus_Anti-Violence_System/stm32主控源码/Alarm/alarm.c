#include "stm32f10x.h"   
#include "alarm.h"
#include "Hardware/Key.h"
#include "Hardware/esp8266.h"
#include "Hardware/LED.h"

static void Alarm_StartRecordAfterSend(void)
{
    (void)App_TryStartAlarmRecording();
}

//报警类型
#define ALARM_Button  "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"eventType\\\":{\\\"value\\\":\\\"\\u6309\\u94ae6\\\"}}}\",0,0"//按钮报警
#define ALARM_Voic "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"eventType\\\":{\\\"value\\\":\\\"\\u8bed\\u97f3\\\"}}}\",0,0"//语音报警
#define ALARM_None "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u65e0\\\"}}}\",0,0"//无报警信息

//语音求救报警
#define ALARM_Voice_detect   "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6c42\\u6551\\u4fe1\\u606f\\\"}}}\",0,0"//救命
#define ALARM_Fire_detect   "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u7740\\u706b\\\"}}}\",0,0"//着火
#define ALARM_Kill_detect   "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6740\\u4eba\\\"}}}\",0,0"//虾仁
#define ALARM_Fight_detect   "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6253\\u67b6\\\"}}}\",0,0"//打架
// 其他语音求救触发词（AI添加）
#define ALARM_Kidnap_detect        "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u7ed1\\u67b6\\\"}}}\",0,0"//绑架
#define ALARM_Explosion_detect     "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u7206\\u70b8\\\"}}}\",0,0"//爆炸
#define ALARM_Blood_detect         "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6d41\\u8840\\\"}}}\",0,0"//流血
#define ALARM_Faint_detect         "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6655\\u5012\\\"}}}\",0,0"//晕倒
#define ALARM_StopHit_detect       "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u522b\\u6253\\u6211\\\"}}}\",0,0"//别打我
#define ALARM_Robbery_detect       "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u62a2\\u52ab\\\"}}}\",0,0"//抢劫
#define ALARM_HelpMe_detect        "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6551\\u6211\\\"}}}\",0,0"//救我
#define ALARM_CallPeople_detect    "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u6765\\u4eba\\u5440\\\"}}}\",0,0"//来人呀
#define ALARM_GroupFight_detect    "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u7fa4\\u6bb4\\\"}}}\",0,0"//群殴
#define ALARM_DontMove_detect      "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"\\u522b\\u52a8\\\"}}}\",0,0"//别动

//发送报警位置信息

//报警优先级
#define ALARM_Voice_High_level "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"alarm_level\\\":{\\\"value\\\":0}}}\",0,0"//高优先级
#define ALARM_Voice_Low_level "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"alarm_level\\\":{\\\"value\\\":1}}}\",0,0"//低优先级

//一般报警 移除 我们只需要优先级就可以
#define Gen_alarm  "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"desc\\\":{\\\"value\\\":\\\"is anybody there\\\"\\}}}\",0,0"

void Alarm_ButtomDown(uint8_t key)
{
    if (key == 2)
    {
        if (App_IsRecording())
        {
            return;
        }
        bool status;
        status = ESP8266_Cmd(ALARM_Button, "OK", NULL, 1500);//按钮事件类型
        ESP8266_Cmd(ALARM_None, "OK", NULL, 1500);//无事件类型
        ESP8266_Cmd(ALARM_Voice_High_level,"OK",NULL,1500);//高优先级
        ESP8266_Send_Alarm_Time();//实时时间
       // ESP8266_Send_DivceID();//设备标识符
      
        if (status)
        {
            LED0_ON();
        }
        Alarm_StartRecordAfterSend();
    }
}

void Alarm_Voice(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 =ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);//语音事件类型
    ESP8266_Cmd(ALARM_Voice_detect, "OK", NULL, 1500);//语音内容
    ESP8266_Send_Alarm_Time();//实时时间
    ESP8266_Cmd(ALARM_Voice_High_level,"OK",NULL,1500);//高优先级
   // ESP8266_Send_DivceID();//设备标识符
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Kidnap(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_Kidnap_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Explosion(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_Explosion_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Blood(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_Blood_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Faint(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_Faint_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_StopHit(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_StopHit_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Robbery(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_Robbery_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_HelpMe(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_HelpMe_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_CallPeople(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_CallPeople_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_GroupFight(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_GroupFight_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK", NULL, 1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_DontMove(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 = ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);
    ESP8266_Cmd(ALARM_DontMove_detect, "OK", NULL, 1500);
    ESP8266_Send_Alarm_Time();
    ESP8266_Cmd(ALARM_Voice_High_level, "OK",NULL,1500);
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Fire(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 =ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);//语音事件类型
    ESP8266_Cmd(ALARM_Fire_detect, "OK", NULL, 1500);//语音内容
    ESP8266_Send_Alarm_Time();//实时时间
    ESP8266_Cmd(ALARM_Voice_High_level,"OK",NULL,1500);//高优先级
    //ESP8266_Send_DivceID();//设备标识符
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Kill(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 =ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);//语音事件类型
    ESP8266_Cmd(ALARM_Kill_detect, "OK", NULL, 1500);//语音内容
    ESP8266_Send_Alarm_Time();//实时时间
    ESP8266_Cmd(ALARM_Voice_High_level,"OK",NULL,1500);//高优先级
    //ESP8266_Send_DivceID();//设备标识符
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void Alarm_Fight(void)
{
    if (App_IsRecording())
    {
        return;
    }
    bool status1;
    status1 =ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);//语音事件类型
    ESP8266_Cmd(ALARM_Fight_detect, "OK", NULL, 1500);//语音内容
    ESP8266_Send_Alarm_Time();//实时时间
    ESP8266_Cmd(ALARM_Voice_High_level,"OK",NULL,1500);//高优先级
    //ESP8266_Send_DivceID();//设备标识符
    if (status1)
    {
        LED0_ON();
    }
    Alarm_StartRecordAfterSend();
}

void General_alarm(void)
{
    if (App_IsRecording())
    {
        return;
    }
    ESP8266_Cmd(ALARM_Voic, "OK", NULL, 1500);//语音事件类型
     ESP8266_Cmd(ALARM_None, "OK", NULL, 1500);//一般语音内容
    ESP8266_Cmd(ALARM_Voice_Low_level,"OK",NULL,1500);//低优先级
    ESP8266_Send_Alarm_Time();//实时时间
    Alarm_StartRecordAfterSend();
}
