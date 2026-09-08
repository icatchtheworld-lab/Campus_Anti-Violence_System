#ifndef __ALARM_H
#define __ALARM_H

#include "stm32f10x.h"

uint8_t App_IsRecording(void);
uint8_t App_TryStartAlarmRecording(void);

void Alarm_ButtomDown(uint8_t key);  
void Alarm_Voice(void);
void Alarm_Fire(void);
void Alarm_Kill(void);
void Alarm_Fight(void);
void General_alarm(void);
void Alarm_Kidnap(void);
void Alarm_Explosion(void);
void Alarm_Blood(void);
void Alarm_Faint(void);
void Alarm_StopHit(void);
void Alarm_Robbery(void);
void Alarm_HelpMe(void);
void Alarm_CallPeople(void);
void Alarm_GroupFight(void);
void Alarm_DontMove(void);
#endif
