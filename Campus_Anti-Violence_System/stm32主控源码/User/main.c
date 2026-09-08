#include "stm32f10x.h"                  // Device header
#include "System/Delay.h"
#include "Hardware/Serial.h"
#include "Hardware/esp8266.h"
#include "Hardware/usart.h"
#include "Hardware/Key.h"
#include "Hardware/LED.h"
#include "Alarm/alarm.h"
#include "Hardware/tf_card.h"
#include "Time.h"
#include "Library/stm32f10x_adc.h"
#include "Library/stm32f10x_dma.h"
#include "Library/stm32f10x_tim.h"
#include "Library/misc.h"
#include <string.h>
#include <stdio.h>



volatile uint8_t flag;//网络测试
uint8_t g_PassthroughMode = 0;//穿透模式

char uart_buffer[20];//uart3的缓冲区
uint8_t uart_index = 0;

#define AUDIO_SAMPLE_RATE_HZ        8000//采样率8kHZ
#define AUDIO_DMA_BUF_SIZE          800//DMA缓冲区大小 ：800个样本
#define AUDIO_DMA_HALF_SIZE         (AUDIO_DMA_BUF_SIZE / 2)
#define AUDIO_RECORD_SECONDS        15
#define AUDIO_WAV_BYTES_TARGET      (AUDIO_SAMPLE_RATE_HZ * AUDIO_RECORD_SECONDS * 2U)
#define OSS_HTTP_HOST               "test-record001.oss-cn-beijing.aliyuncs.com"
#define OSS_HTTP_PORT               "80"
#define OSS_OBJECT_PREFIX           "Record/Test/"
#define OSS_UPLOAD_CHUNK_SIZE       256U
// 原宏是固定字符串，现在改成格式化模板，用%s作为upload_url的占位符
#define UPLOAD_URL_TEMPLATE "AT+MQTTPUB=0,\"$sys/l4JQEioAnm/test1/thing/property/post\",\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{\\\"audio_url\\\":{\\\"value\\\":\\\"http://%s/%s\\\"}}}\",0,0"

volatile uint8_t g_AudioHalfReady = 0;//半缓冲区就绪标志
volatile uint8_t g_AudioFullReady = 0;//全缓冲区就绪标志
uint16_t g_AudioAdcBuf[AUDIO_DMA_BUF_SIZE];//音频数据缓冲区

static uint8_t g_WavRecording = 0;//是否在记录当中
static uint32_t g_WavDataBytes = 0; //记录已经写入到 WAV 文件中的数据总字节数
static FIL g_WavFile;
static FIL g_OssUploadFile;
static char g_WavPath[64];//录音文件的命名
static uint8_t g_OssUploadBuf[OSS_UPLOAD_CHUNK_SIZE];
static char g_OssObjectKey[128];
static char g_OssHttpHead[256];
static uint8_t g_AudioCaptureRunning = 0;//开没开启捕获
static void Audio_Capture_Start(void);
static void Audio_Capture_Stop(void);
static void Wav_Start(void);
static uint8_t Wav_BuildOssObjectKey(const char *wavPath, char *objectKey, uint32_t objectKeySize);
static uint8_t Wav_UploadToOss(const char *wavPath);
char upload_url[1024] = {0};

static FRESULT Wav_WriteHeader(FIL *fp, uint32_t dataBytes)
{
	uint8_t hdr[44];
	uint32_t riffSize;
	uint32_t byteRate;
	uint16_t blockAlign;
	UINT bw;
	FRESULT fr;

	riffSize = dataBytes + 36U;
	byteRate = AUDIO_SAMPLE_RATE_HZ * 2U;
	blockAlign = 2U;

	hdr[0]  = 'R'; hdr[1]  = 'I'; hdr[2]  = 'F'; hdr[3]  = 'F';
	hdr[4]  = (uint8_t)(riffSize);
	hdr[5]  = (uint8_t)(riffSize >> 8);
	hdr[6]  = (uint8_t)(riffSize >> 16);
	hdr[7]  = (uint8_t)(riffSize >> 24);
	hdr[8]  = 'W'; hdr[9]  = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
	hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
	hdr[16] = 16;  hdr[17] = 0;   hdr[18] = 0;   hdr[19] = 0;
	hdr[20] = 1;   hdr[21] = 0;
	hdr[22] = 1;   hdr[23] = 0;
	hdr[24] = (uint8_t)(AUDIO_SAMPLE_RATE_HZ);
	hdr[25] = (uint8_t)(AUDIO_SAMPLE_RATE_HZ >> 8);
	hdr[26] = (uint8_t)(AUDIO_SAMPLE_RATE_HZ >> 16);
	hdr[27] = (uint8_t)(AUDIO_SAMPLE_RATE_HZ >> 24);
	hdr[28] = (uint8_t)(byteRate);
	hdr[29] = (uint8_t)(byteRate >> 8);
	hdr[30] = (uint8_t)(byteRate >> 16);
	hdr[31] = (uint8_t)(byteRate >> 24);
	hdr[32] = (uint8_t)(blockAlign);
	hdr[33] = (uint8_t)(blockAlign >> 8);
	hdr[34] = 16;  hdr[35] = 0;
	hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
	hdr[40] = (uint8_t)(dataBytes);
	hdr[41] = (uint8_t)(dataBytes >> 8);
	hdr[42] = (uint8_t)(dataBytes >> 16);
	hdr[43] = (uint8_t)(dataBytes >> 24);

	fr = f_lseek(fp, 0);
	if (fr != FR_OK) {
		printf("WAV header seek fail fr=%d\r\n", (int)fr);
		return fr;
	}

	fr = f_write(fp, hdr, sizeof(hdr), &bw);
	if (fr != FR_OK || bw != sizeof(hdr)) {
		printf("WAV header write fail fr=%d, bw=%u\r\n", (int)fr, (unsigned)bw);
		return (fr != FR_OK) ? fr : FR_DISK_ERR;
	}

	return FR_OK;
}

static uint8_t Wav_BuildOssObjectKey(const char *wavPath, char *objectKey, uint32_t objectKeySize)
{
	const char *fileName;
	int written;

	if ((wavPath == 0) || (objectKey == 0) || (objectKeySize == 0U)) {
		return 0;
	}

	fileName = wavPath;
	if ((strlen(wavPath) > 2U) && (wavPath[1] == ':') && ((wavPath[2] == '/') || (wavPath[2] == '\\'))) {
		fileName = wavPath + 3;
	} else if ((wavPath[0] == '/') || (wavPath[0] == '\\')) {
		fileName = wavPath + 1;
	}

	written = snprintf(objectKey, objectKeySize, "%s%s", OSS_OBJECT_PREFIX, fileName);
	if ((written < 0) || ((uint32_t)written >= objectKeySize)) {
		return 0;
	}
	
	return 1;
}

static uint8_t Wav_UploadToOss(const char *wavPath)
{
	FRESULT fr;
	UINT br;
	uint32_t fileSize;
	uint32_t sentBytes;
	int headerLen;
	uint8_t uploadOk;
	uint8_t streamStarted;
	uint8_t transparentModeEnabled;

	if ((wavPath == 0) || (wavPath[0] == '\0')) {
		return 0;
	}

	fr = f_open(&g_OssUploadFile, wavPath, FA_READ);
	if (fr != FR_OK) {
		printf("OSS open fail fr=%d\r\n", (int)fr);
		return 0;
	}

	fileSize = (uint32_t)f_size(&g_OssUploadFile);
	if (fileSize == 0U) {
		printf("OSS file empty\r\n");
		(void)f_close(&g_OssUploadFile);
		return 0;
	}

	if (!Wav_BuildOssObjectKey(wavPath, g_OssObjectKey, sizeof(g_OssObjectKey))) {
		printf("OSS key build fail\r\n");
		(void)f_close(&g_OssUploadFile);
		return 0;
	}

	headerLen = snprintf(g_OssHttpHead, sizeof(g_OssHttpHead),
		"PUT /%s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Content-Type: audio/wav\r\n"
		"Content-Length: %lu\r\n"
		"Connection: close\r\n"
		"\r\n",
		g_OssObjectKey,
		OSS_HTTP_HOST,
		(unsigned long)fileSize);
	if ((headerLen < 0) || ((uint32_t)headerLen >= sizeof(g_OssHttpHead))) {
		printf("OSS head build fail\r\n");
		(void)f_close(&g_OssUploadFile);
		return 0;
	}

	uploadOk = 0;
	sentBytes = 0U;
	streamStarted = 0U;
	transparentModeEnabled = 0U;
	ucTcpClosedFlag = 0;
	g_ESP8266RawBusy = 1;

	if (!ESP8266_Link_Server(enumTCP, OSS_HTTP_HOST, OSS_HTTP_PORT, Single_ID_0)) {
		printf("OSS link fail\r\n");
		(void)f_close(&g_OssUploadFile);
		return 0;
	}

	if (!ESP8266_UnvarnishSend()) { 
		printf("OSS transparent mode fail\r\n");
		goto EXIT_UPLOAD;
	}
	transparentModeEnabled = 1U;

	if (!ESP8266_Cmd("AT+CIPSEND", ">", 0, 5000)) {
		printf("OSS stream start fail\r\n");
		goto EXIT_UPLOAD;
	}
	streamStarted = 1U;
	ESP8266_SendRawBuffer((const uint8_t *)g_OssHttpHead, (u32)headerLen);

	while (1) {
		fr = f_read(&g_OssUploadFile, g_OssUploadBuf, sizeof(g_OssUploadBuf), &br);
		if (fr != FR_OK) {
			printf("OSS read fail fr=%d\r\n", (int)fr);
			goto EXIT_UPLOAD;
		}

		if (br == 0U) {
			break;
		}

		ESP8266_SendRawBuffer(g_OssUploadBuf, (u32)br);

		sentBytes += (uint32_t)br;
		if (((sentBytes & 0x3FFFU) == 0U) || (sentBytes >= fileSize)) {
			printf("OSS sent %lu/%lu\r\n", (unsigned long)sentBytes, (unsigned long)fileSize);
		}
	}

	if (!ESP8266_WaitResponse("HTTP/1.1 200", "HTTP/1.1 201", 20000)) {
		if (ucTcpClosedFlag) {
			printf("OSS http closed\r\n");
		} else {
		printf("OSS http timeout\r\n");
		}
		goto EXIT_UPLOAD;
	}

	if ((strstr((char *)strEsp8266_Fram_Record.Data_RX_BUF, "HTTP/1.1 200") == 0) &&
		(strstr((char *)strEsp8266_Fram_Record.Data_RX_BUF, "HTTP/1.1 201") == 0)) {
		printf("OSS http reject\r\n");
		goto EXIT_UPLOAD;
	}

	uploadOk = 1;
  
EXIT_UPLOAD:
	g_ESP8266RawBusy = 0;
	if (streamStarted) {
		ESP8266_ExitUnvarnishSend();
	}
	(void)ESP8266_Cmd("AT+CIPCLOSE", "OK", "ERROR", 2000);
	if (transparentModeEnabled) {
		(void)ESP8266_Cmd("AT+CIPMODE=0", "OK", 0, 1000);
    
	}
 	(void)f_close(&g_OssUploadFile);
 	if (uploadOk) {
 		Delay_ms(100);
 		printf("OSS upload ok\r\n");
 		printf("OSS upload url: http://%s/%s\r\n", OSS_HTTP_HOST, g_OssObjectKey);
		headerLen = snprintf(upload_url, sizeof(upload_url),
			UPLOAD_URL_TEMPLATE, OSS_HTTP_HOST, g_OssObjectKey);
		if ((headerLen <= 0) || ((uint32_t)headerLen >= sizeof(upload_url))) {
			printf("MQTT audio_url cmd too long\r\n");
		} else if (!ESP8266_Cmd(upload_url, "OK", NULL, 3000)) {
			printf("MQTT audio_url post fail\r\n");
		} else {
			printf("MQTT audio_url post ok\r\n");
		}
 	}
 	return uploadOk;
 }

static void Wav_Stop(void)
{
	FRESULT fr;
	if (!g_WavRecording) {
		return;
}

	g_WavRecording = 0;
	Audio_Capture_Stop();

	fr = Wav_WriteHeader(&g_WavFile, g_WavDataBytes);
	if (fr != FR_OK) {
		printf("WAV finalize header fail fr=%d\r\n", (int)fr);
	}
	(void)f_sync(&g_WavFile);
	(void)f_close(&g_WavFile);
	printf("WAV saved, bytes=%lu\r\n", (unsigned long)g_WavDataBytes);
	if (fr == FR_OK) {
		if (!Wav_UploadToOss(g_WavPath)) {
			printf("WAV upload fail\r\n");
		}
	}
	
}

static void Wav_Start(void)
{
	FRESULT fr;
	FRESULT fr2;
	uint8_t tryCount;

	if (g_WavRecording) {
		return;
	}

	fr = TF_Card_Mount();//挂载
	if (fr != FR_OK) {
		printf("TF mount fail fr=%d\r\n", (int)fr);
		return;
	}
	fr = FR_INVALID_NAME;
	for (tryCount = 0; tryCount < 10U; tryCount++) {
		if (!Time_GenerateRecordPath(g_WavPath, sizeof(g_WavPath))) {
			printf("WAV path gen fail\r\n");
			return;
		}

		fr = f_open(&g_WavFile, g_WavPath, FA_CREATE_NEW | FA_WRITE);
		if (fr == FR_OK) {
			break;
		}
		if (fr != FR_EXIST) {
			break;
		}
	}
	if (fr != FR_OK) {
		printf("WAV open fail fr=%d\r\n", (int)fr);
		return;
	}

	fr = Wav_WriteHeader(&g_WavFile, 0);
	if (fr != FR_OK) {
		(void)f_close(&g_WavFile);
		return;
	}

	fr2 = f_lseek(&g_WavFile, 44);//定位
	if (fr2 != FR_OK) {
		printf("WAV seek fail fr=%d\r\n", (int)fr2);
		(void)f_close(&g_WavFile);
		return;
	}

	Audio_Capture_Start();

	g_WavDataBytes = 0;
	g_WavRecording = 1;
	printf("WAV rec start: %s\r\n", g_WavPath);
}


uint8_t App_IsRecording(void)
{
	return g_WavRecording;
}


uint8_t App_TryStartAlarmRecording(void)
{
	if (g_WavRecording) {
		return 0;
	}

	Wav_Start();
	return g_WavRecording;
}

static void ADC1_CH1_Init(void)
{
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);//设置分频系数  系数是6 模拟频率不大于14  所以72/6=12 
  //GPIOA和ADC1都在APB2总线上
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);//开启时钟

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;//输入模式设置为模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;//PA1
    GPIO_Init(GPIOA, &GPIO_InitStructure);    
    
  ADC_DeInit(ADC1);//复位ADC1
    
    ADC_InitTypeDef ADC_InitStructure;
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;//独立模式
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;//不扫描
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;//非连续转换
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;//触发源是tim3  tim3发送一次信号ADC转换一次
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;//数据右对齐 常用
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
    
  //配置规则通道 ADC1  通道1 顺序1 采样时间239.5个周期
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_239Cycles5);

    ADC_DMACmd(ADC1, ENABLE);//开启dma 
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);//使能外部触发
    ADC_Cmd(ADC1, ENABLE);//开启ADC
    Delay_ms(2);
    //校准
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

static void TIM3_TRGO_Init(uint32_t sampleRateHz)
{
    uint32_t timerClockHz;//定时器工作时钟频率
    uint16_t prescaler;//预分频系数
    uint16_t period;//自动重装载值

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);//使能tim3时钟

    timerClockHz = 1000000;
    prescaler = (uint16_t)(SystemCoreClock / timerClockHz) - 1;
    period = (uint16_t)((timerClockHz / sampleRateHz) - 1);
    
  
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
    TIM_TimeBaseStructure.TIM_Prescaler = prescaler;//
    TIM_TimeBaseStructure.TIM_Period = period;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);//选择更新事件作为触发源
    TIM_Cmd(TIM3, ENABLE);
}

static void DMA1_Channel1_ADC1_Init(uint16_t *buf, uint16_t size)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);//启动DMA1时钟

    DMA_InitTypeDef DMA_InitStructure;//DMA结构体
    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;//ADC1数据寄存器
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)buf;//全局缓冲区
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;//传输方向 从外设到内存
    DMA_InitStructure.DMA_BufferSize = size;//缓冲区大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;//外设地址是固定
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;//内存地址是自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;//数据宽度半字
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;//数据宽度半字
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;//模式是循环模式 持续采集
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;//优先级：高
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;//是外设到内存 所以m2m关闭
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT, ENABLE);//使能半传输中断
    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);//使能传输完成中断
  //中断相关设置
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    DMA_Cmd(DMA1_Channel1, ENABLE);//启动DMA
}

static void Audio_Capture_Start(void)
{
	if (g_AudioCaptureRunning) {
		return;
	}
	g_AudioHalfReady = 0;
	g_AudioFullReady = 0;
	DMA1_Channel1_ADC1_Init(g_AudioAdcBuf, AUDIO_DMA_BUF_SIZE);//初始化DMA通道1，用于将ADC1采集的数据直接传输到内存缓冲区
    ADC1_CH1_Init();//开启ADC
    TIM3_TRGO_Init(AUDIO_SAMPLE_RATE_HZ);//提供精确的定时触发信号 确保ADC以预设的采样频率进行音频数据采集
	g_AudioCaptureRunning = 1;
}

static void Audio_Capture_Stop(void)
{
	if (!g_AudioCaptureRunning) {
		return;
	}

	TIM_Cmd(TIM3, DISABLE);
	ADC_ExternalTrigConvCmd(ADC1, DISABLE);
	ADC_DMACmd(ADC1, DISABLE);
	DMA_Cmd(DMA1_Channel1, DISABLE);
	ADC_Cmd(ADC1, DISABLE);
	DMA_ClearITPendingBit(DMA1_IT_HT1 | DMA1_IT_TC1);
	g_AudioHalfReady = 0;
	g_AudioFullReady = 0;
	g_AudioCaptureRunning = 0;
}

static void Audio_ToWav_WriteHalf(uint16_t startIndex)//确保录制15s 通过存储大小来推算时间
{
	FRESULT fr;
	UINT bw;
	uint16_t i;
	int16_t pcm[AUDIO_DMA_HALF_SIZE];

	if (!g_WavRecording) {
		return;
	}

	if (g_WavDataBytes >= AUDIO_WAV_BYTES_TARGET) {
		Wav_Stop();
		return;
	}

	for (i = 0; i < AUDIO_DMA_HALF_SIZE; i++) {
		int32_t x = (int32_t)g_AudioAdcBuf[startIndex + i] - 2048;
		x <<= 4;
		if (x > 32767) x = 32767;
		if (x < -32768) x = -32768;
		pcm[i] = (int16_t)x;
	}

	fr = f_write(&g_WavFile, (const void *)pcm, (UINT)(AUDIO_DMA_HALF_SIZE * 2U), &bw);
	if (fr != FR_OK || bw != (UINT)(AUDIO_DMA_HALF_SIZE * 2U)) {
		printf("WAV write fail fr=%d, bw=%u\r\n", (int)fr, (unsigned)bw);
		Wav_Stop();
		return;
	}

	g_WavDataBytes += (uint32_t)bw;
	if (g_WavDataBytes >= AUDIO_WAV_BYTES_TARGET) {
		Wav_Stop();
	}
}


void Open_Penetmode(uint8_t key)//透传模式
{
    if ((key == 1)  && (g_PassthroughMode == 0))
    {
        g_PassthroughMode = 1;
        printf("Enter AT passthrough mode\r\n");
    }
}


void Get_STM32_UID(char *uid_str)//stm32唯一标识符
{
	//UID是96为 12字节 设置为3个32为的数组
	uint32_t uid[3];
	uid[0] = *(uint32_t *)0x1FFFF7E8;
	uid[1] = *(uint32_t *)0x1FFFF7EC;
	uid[2] = *(uint32_t *)0x1FFFF7F0;
	sprintf(uid_str, "%08lX%08lX%08lX", uid[0], uid[1], uid[2]);

}


int main(void)
{
	char uid_str[25];
	Serial_Init();				//语音串口  初始化PB10 PB11 9600  将接收到的信息通过串口发送stm32上
    LED_Init();     
    Key_Init();
	USART_Config();          //设置串口中断 打印wifi收发信息 占用PA9 PA10 115200 将接收到的信息通过串口发送stm32上
  Get_STM32_UID(uid_str);
	printf("USART1 OK\r\n");
	printf("STM32 UID: %s\r\n", uid_str);
	
	ESP8266_Init ();         //设置通信串口 初始化WiFi模块使用的接口和外设 PA2  PA3 115200  
	ESP8266_StaTcpClient (); //WiFi模块设置   让ESP8266可以通过wifi访问外网
	Serial_SendString("test");	//语音串口发送字符串
	
	while (1)
	{
		uint8_t key;
		uint8_t controlBlocked;
    key = Key_GetNum();
		controlBlocked = App_IsRecording();
    if (!controlBlocked)
    {
      Open_Penetmode(key);//开启串口1和2的穿透模式
      Alarm_ButtomDown(key);
    }
		if (g_PassthroughMode == 0)//在没有进入穿透模式下
		{
			if(USART_GetFlagStatus(USART3,USART_FLAG_RXNE)==SET)//语音模块的交互
			{
				uint8_t data = USART_ReceiveData(USART3);
				if (App_IsRecording())
				{
					uart_index = 0;
				}
				else
				{
	      //点亮PA0的LEDa
				if (data == '\r' || data == '\n')
				{
					if (uart_index > 0U)
					{
						uart_buffer[uart_index] = '\0';
						printf("Received command: %s\r\n", uart_buffer);

						if (App_IsRecording())
						{
							uart_index = 0;
						}
						else if (strcmp(uart_buffer, "1") == 0)
						{
							LED0_ON();
						}
						else if (strcmp(uart_buffer, "2") == 0)
						{
							LED0_OFF();
						}
						else if (strcmp(uart_buffer, "3") == 0)
						{
							Alarm_Voice();
						}
						else if (strcmp(uart_buffer, "4") == 0)
						{
							 Alarm_Fire();
						}
						else if (strcmp(uart_buffer, "5") == 0)
						{
							Alarm_Kill();
						}
						else if (strcmp(uart_buffer, "6") == 0)
						{
							Alarm_Fight();
						}
						else if (strcmp(uart_buffer, "7") == 0)
						{
							Alarm_Kidnap();
						}
						else if (strcmp(uart_buffer, "8") == 0)
						{
							Alarm_Explosion();
						}
						else if (strcmp(uart_buffer, "9") == 0)
						{
							Alarm_Blood();
						}
						else if (strcmp(uart_buffer, "10") == 0)
						{
							Alarm_Faint();
						}
						else if (strcmp(uart_buffer, "11") == 0)
						{
							Alarm_StopHit();
						}
						else if (strcmp(uart_buffer, "12") == 0)
						{
							Alarm_Robbery();
						}
						else if (strcmp(uart_buffer, "13") == 0)
						{
							Alarm_HelpMe();
						}
						else if (strcmp(uart_buffer, "14") == 0)
						{
							Alarm_CallPeople();
						}
						else if (strcmp(uart_buffer, "15") == 0)
						{
							Alarm_GroupFight();
						}
						else if (strcmp(uart_buffer, "16") == 0)
						{
							Alarm_DontMove();
						}
						else if (strcmp(uart_buffer, "17") == 0)
						{
							General_alarm();
						}
						uart_index = 0;
					}
				}
				else
				{
					if (uart_index < (sizeof(uart_buffer) - 1U))
					{
						uart_buffer[uart_index++] = (char)data;
					}
					else
					{
						uart_index = 0;
					}
				}
				}
			}	
			if (!App_IsRecording())
			{
				switch(flag)//网络环境的测试
			{
				case 'a': LED0_ON();printf("LED turn ON"); flag = 0; break;//开灯
				
				case 'c':  LED0_OFF(); printf("LED turn OFF");flag = 0; break;//关灯
				default: break; // 无操作
			}	
			if (g_MqttSubRecvPending)
			{
				ProcessMQTTSubRecv();
			}
			}

			if (g_AudioHalfReady)//DMA完成前半部分的填充
			{
				g_AudioHalfReady = 0;  //清除标志位 准备下一次中断
				Audio_ToWav_WriteHalf(0);//关键 将前半部分数据写入WAV文件 参数是0表示是从0开始
			}
			if (g_AudioFullReady)//DAM完成后半部填充
			{
				g_AudioFullReady = 0;//清除标志位
				Audio_ToWav_WriteHalf(AUDIO_DMA_HALF_SIZE);//参数从400开始
			}
		}

	}
}
