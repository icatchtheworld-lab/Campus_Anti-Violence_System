# STM32 联网报警录音项目接手清单

> 依据压缩包内实际源码、Keil 工程文件与构建日志整理。该项目是 **STM32F103C8T6 + ESP8266 + OneNET + TF 卡 + OSS 音频上传** 的嵌入式固件项目，不是 SpringBoot/Web 后端项目。

---

## 1. 项目一句话概括

这是一个基于 STM32F103C8T6 的校园安全/报警类终端固件：设备通过按键或语音识别模块触发报警，把报警类型、报警描述、报警等级、时间等属性通过 ESP8266 以 MQTT 上报到 OneNET；同时 STM32 使用 ADC + DMA 采集约 15 秒音频，保存为 WAV 文件到 TF 卡，再通过 ESP8266 以 HTTP PUT 上传到阿里云 OSS，上传成功后再把 `audio_url` 通过 OneNET MQTT 上报。

核心闭环：

```text
按键/语音模块触发
        ↓
STM32 判断报警类型
        ↓
ESP8266 连接 WiFi、OneNET MQTT
        ↓
上报 eventType / desc / alarm_level / time
        ↓
ADC + DMA 录制 15 秒音频
        ↓
FatFs 写入 TF 卡 WAV 文件
        ↓
ESP8266 TCP 直连 OSS HTTP 80 端口上传 WAV
        ↓
上传成功后 MQTT 上报 audio_url
        ↓
云端/小程序/后台可根据 OneNET 属性读取报警信息与音频链接
```

---

## 2. 项目类型与运行环境

| 项目项 | 实际情况 |
|---|---|
| 工程类型 | Keil MDK-ARM 嵌入式 C 工程 |
| 工程文件 | `Project.uvprojx` |
| 目标芯片 | `STM32F103C8`，Cortex-M3 |
| 启动文件 | `Start/startup_stm32f10x_md.s` |
| 标准库 | STM32F10x Standard Peripheral Library |
| 编译器 | ARMCC 5.06 update 7 |
| IDE 日志版本 | µVision V5.43.1.0 / MDK-Lite 5.43.0.0 |
| 构建结果 | `0 Error(s), 2 Warning(s)` |
| 程序体积 | Code=24048, RO-data=4876, RW-data=128, ZI-data=15032 |
| 当前配置 | 未生成 HEX：`CreateHexFile=0` |
| 云连接 | ESP8266 AT 指令，MQTT 到 OneNET，HTTP PUT 到 OSS |
| 本地存储 | TF/SD 卡 + FatFs |
| 音频采集 | ADC1 CH1 + TIM3 触发 + DMA1 Channel1 |

---

## 3. 顶层目录结构说明

压缩包中存在一层中文目录，解压后在 Linux 下显示为 `#U8054#U7f51#U6d4b#U8bd5win/...`，语义应为“联网测试win”。真正工程根目录在第二层。

```text
工程根目录
├── Project.uvprojx              # Keil 主工程文件
├── Project.uvoptx               # Keil 用户/调试选项
├── User/
│   ├── main.c                   # 主业务入口：初始化、报警触发、录音、上传调度
│   ├── stm32f10x_it.c           # 中断：DMA、USART1、USART2/ESP8266
│   └── stm32f10x_conf.h         # 标准库配置
├── Hardware/
│   ├── esp8266.c/.h             # ESP8266 AT、WiFi、MQTT、NTP、TCP 上传
│   ├── Serial.c/.h              # USART3 语音模块串口
│   ├── usart.c/.h               # USART1 调试串口
│   ├── LED.c/.h                 # PA0 LED
│   ├── Key.c/.h                 # PB5/PB9 按键
│   ├── tf_card.c/.h             # FatFs 挂载与文件追加封装
│   ├── sd_spi.c/.h              # SPI1 驱动 SD/TF 卡底层读写
│   ├── OLED.c/.h                # OLED 驱动，当前主流程基本未使用
│   └── bsp_Alarm.c/.h           # 蜂鸣器/报警 GPIO，当前主流程基本未使用
├── Alarm/
│   ├── alarm.c                  # 报警类型、MQTT 上报、录音启动
│   └── alarm.h
├── System/
│   ├── Delay.c/.h               # SysTick 延时
│   └── bsp_timer.c/.h           # 定时器封装，当前主流程基本未使用
├── Start/                       # 启动文件、CMSIS、系统时钟
├── Library/                     # STM32F10x 标准外设库
├── Onenet/
│   ├── MqttKit.c/.h             # MQTT 工具代码，但主工程主要使用 ESP8266 AT MQTT
├── ff.c / ff.h / ffsystem.c     # FatFs 文件系统
├── diskio.c / diskio.h          # FatFs 与 SD_SPI 的适配层
├── oss_c_sdk/                   # 阿里云 OSS C SDK 源码，但未加入 Keil 主编译列表
├── Objects/                     # Keil 编译产物，不是源码重点
└── Listings/                    # map/listing 输出
```

---

## 4. 模块职责清单

### 4.1 `User/main.c`：主控制中心

主要职责：

1. 初始化硬件：
   - `Serial_Init()`：USART3，接语音识别模块。
   - `LED_Init()`：PA0 LED。
   - `Key_Init()`：PB5/PB9 按键。
   - `USART_Config()`：USART1 调试打印。
   - `ESP8266_Init()`：USART2 接 ESP8266。
   - `ESP8266_StaTcpClient()`：连接 WiFi、OneNET、订阅主题、配置 NTP。

2. 读取 STM32 唯一 ID：
   - `Get_STM32_UID()` 读取地址 `0x1FFFF7E8 / 0x1FFFF7EC / 0x1FFFF7F0`，拼成 96-bit UID 字符串。

3. 主循环处理：
   - 读取按键。
   - 非录音状态下允许进入 AT 透传模式、允许按键报警。
   - 监听 USART3 语音模块返回的命令字符串。
   - 根据命令 `1`~`17` 控制 LED 或触发不同报警。
   - 处理 OneNET property/set 下发。
   - 处理 DMA 半缓冲/全缓冲音频数据写入 WAV。

4. 音频采集与 WAV：
   - 8 kHz 采样率。
   - ADC1 通道 1，PA1 模拟输入。
   - TIM3 TRGO 定时触发 ADC。
   - DMA1 Channel1 循环搬运 ADC 数据。
   - 每半缓冲 400 个采样点写一次文件。
   - 录制目标为 15 秒。
   - 保存 WAV 后上传 OSS。

### 4.2 `Alarm/alarm.c`：报警业务封装

主要职责：

1. 定义报警类型与 OneNET 属性上报 AT 指令。
2. 将不同触发源转换成 OneNET 属性：
   - 按钮报警：`eventType = 按钮6`。
   - 语音报警：`eventType = 语音`。
   - 报警描述：`desc = 求救信息 / 着火 / 杀人 / 打架 / 绑架 / 爆炸 / 流血 / 晕倒 / 别打我 / 抢劫 / 救我 / 来人呀 / 群殴 / 别动 / 无`。
   - 报警等级：`alarm_level = 0` 表示高优先级，`1` 表示低优先级。
3. 触发后调用 `ESP8266_Send_Alarm_Time()` 上报当前时间。
4. 触发后调用 `App_TryStartAlarmRecording()` 开始录音。

### 4.3 `Hardware/esp8266.c/.h`：联网与云通信层

主要职责：

1. ESP8266 AT 通信：
   - USART2：PA2 TX，PA3 RX，115200。
   - 支持 AT 测试、设置 STA 模式、连接 WiFi、TCP 连接、透传发送。

2. WiFi：
   - SSID：`HiwonderESP`
   - Password：`hiwonder`

3. OneNET MQTT：
   - 使用 ESP8266 的 `AT+MQTTUSERCFG` / `AT+MQTTCONN` / `AT+MQTTPUB` / `AT+MQTTSUB`。
   - 连接地址：`mqtts.heclouds.com:1883`。
   - 产品 ID：`l4JQEioAnm`。
   - 设备名：`test1`。
   - 订阅：property post reply、property set。

4. NTP 时间：
   - 配置：`AT+CIPSNTPCFG=1,8,"ntp.aliyun.com","cn.pool.ntp.org"`。
   - 查询：`AT+CIPSNTPTIME?`。
   - 解析返回格式如：`+CIPSNTPTIME:Wed Feb 25 14:28:53 2026`。
   - 格式化为：`YYYY-MM-DD HH:MM:SS`。

5. 处理云端下发：
   - 监听 OneNET 的 property/set 主题。
   - 解析 `id`。
   - 回复 property/set_reply。
   - 如果包含 `deviceName`，会把 UTF-8 转成 `\uXXXX` 后重新上报 `deviceName`。

### 4.4 `Hardware/Serial.c/.h`：语音模块串口

主要职责：

- USART3，PB10 TX，PB11 RX，9600。
- 主循环里通过轮询 `USART_FLAG_RXNE` 读取语音模块返回。
- 以 `\r` 或 `\n` 作为一条命令结束。
- 命令内容是字符串数字，例如 `"3"` 触发 `Alarm_Voice()`。

### 4.5 `Hardware/Key.c/.h`：按键

| 按键 | GPIO | 返回值 | 当前用途 |
|---|---:|---:|---|
| 按键 1 | PB5 | 1 | 进入 ESP8266 AT 透传模式 |
| 按键 2 | PB9 | 2 | 按钮报警 |

注意：`Key_GetNum()` 是阻塞式按键读取，按住不放时会卡在等待松手逻辑中。

### 4.6 `Hardware/LED.c/.h`：LED

| 功能 | GPIO | 电平逻辑 |
|---|---:|---|
| LED0 | PA0 | 低电平点亮，高电平熄灭 |

### 4.7 `Hardware/tf_card.c` + `Hardware/sd_spi.c` + `diskio.c`：TF 卡与 FatFs

主要职责：

- `TF_Card_Mount()` 调用 `f_mount(&s_fs, "0:", 1)` 挂载 FatFs。
- `diskio.c` 把 FatFs 的 `disk_read/write/ioctl` 映射到 `SD_SPI_ReadBlocks/WriteBlocks`。
- `sd_spi.c` 使用 SPI1 驱动 SD/TF 卡。

SPI1 引脚：

| 功能 | GPIO |
|---|---:|
| CS | PA4 |
| SCK | PA5 |
| MISO | PA6 |
| MOSI | PA7 |

### 4.8 `User/stm32f10x_it.c`：中断入口

主要中断：

1. `DMA1_Channel1_IRQHandler()`
   - 半传输中断：置位 `g_AudioHalfReady = 1`。
   - 传输完成中断：置位 `g_AudioFullReady = 1`。

2. `USART1_IRQHandler()`
   - 调试串口接收。
   - 普通模式下回显到 USART1。
   - 透传模式下把 PC 输入转发到 ESP8266 USART2。

3. `USART2_IRQHandler()`
   - ESP8266 接收中断。
   - 每个字节转发到 USART1 打印。
   - 放入 `strEsp8266_Fram_Record.Data_RX_BUF`。
   - IDLE 中断判定一帧结束。
   - 解析 `+IPD` 作为简单网络测试指令。
   - 解析 `+MQTTSUBRECV` 并置位 `g_MqttSubRecvPending`。
   - 识别 `+MQTTCONNECTED`、TCP CLOSED 等状态。

---

## 5. 硬件接口清单

| 模块 | 外设/接口 | 引脚/参数 | 说明 |
|---|---|---|---|
| 调试串口 | USART1 | PA9 TX, PA10 RX, 115200 | printf 输出、PC 调试、透传入口 |
| ESP8266 | USART2 | PA2 TX, PA3 RX, 115200 | AT 指令、MQTT、TCP/HTTP 上传 |
| 语音模块 | USART3 | PB10 TX, PB11 RX, 9600 | 接收语音识别结果命令 |
| LED | GPIO | PA0 | 低电平亮 |
| 按键 1 | GPIO | PB5，上拉输入 | 进入 AT 透传模式 |
| 按键 2 | GPIO | PB9，上拉输入 | 按钮报警 |
| 音频采集 | ADC1 CH1 | PA1 | 8kHz 采样，模拟音频输入 |
| 采样定时 | TIM3 TRGO | 8kHz | 触发 ADC 转换 |
| 音频 DMA | DMA1 Channel1 | ADC1->DR 到内存 | 半缓冲/全缓冲中断 |
| TF 卡 | SPI1 | PA4/PA5/PA6/PA7 | FatFs 块设备 |
| OLED | GPIO 模拟 I2C/驱动文件存在 | 当前主流程未明显使用 | 可作为显示预留 |
| 蜂鸣器 | `bsp_Alarm` 定义 | 当前主流程未明显使用 | 可作为报警声预留 |

---

## 6. 对外接口与协议清单

这里的“对外接口”主要是设备对云端/模块的协议，不是 HTTP REST API。

### 6.1 OneNET MQTT 连接参数

| 项目 | 值 |
|---|---|
| MQTT 服务器 | `mqtts.heclouds.com` |
| 端口 | `1883` |
| 产品 ID | `l4JQEioAnm` |
| 设备名 | `test1` |
| MQTT Client ID | `test1` |
| 鉴权方式 | `AT+MQTTUSERCFG` 中写死 token/sign |
| 订阅属性上报回复 | `$sys/l4JQEioAnm/test1/thing/property/post/reply` |
| 订阅属性设置 | `$sys/l4JQEioAnm/test1/thing/property/set` |
| 属性设置回复 | `$sys/l4JQEioAnm/test1/thing/property/set_reply` |
| 属性上报 | `$sys/l4JQEioAnm/test1/thing/property/post` |

### 6.2 OneNET 属性上报字段

| 字段 | 类型/值 | 触发位置 | 含义 |
|---|---|---|---|
| `eventType` | `按钮6` | 按键 2 | 按钮报警事件 |
| `eventType` | `语音` | 语音命令 3~17 | 语音报警事件 |
| `desc` | `求救信息` | 命令 3 | 救命/求救 |
| `desc` | `着火` | 命令 4 | 火灾 |
| `desc` | `杀人` | 命令 5 | 严重暴力 |
| `desc` | `打架` | 命令 6 | 打架 |
| `desc` | `绑架` | 命令 7 | 绑架 |
| `desc` | `爆炸` | 命令 8 | 爆炸 |
| `desc` | `流血` | 命令 9 | 流血 |
| `desc` | `晕倒` | 命令 10 | 晕倒 |
| `desc` | `别打我` | 命令 11 | 暴力求助 |
| `desc` | `抢劫` | 命令 12 | 抢劫 |
| `desc` | `救我` | 命令 13 | 求救 |
| `desc` | `来人呀` | 命令 14 | 求助 |
| `desc` | `群殴` | 命令 15 | 群体打架 |
| `desc` | `别动` | 命令 16 | 威胁/抢劫场景 |
| `desc` | `无` | 按钮报警/一般报警 | 无具体语音内容 |
| `alarm_level` | `0` | 多数报警 | 高优先级 |
| `alarm_level` | `1` | 一般报警 | 低优先级 |
| `time` | `YYYY-MM-DD HH:MM:SS` | 每次报警 | NTP 当前时间 |
| `audio_url` | `http://bucket/objectKey` | OSS 上传成功后 | 录音文件 URL |
| `deviceName` | 设备名字符串 | 云端 property/set 后回传 | 设备名称 |

### 6.3 语音模块命令接口

语音模块通过 USART3 向 STM32 发送字符串命令，以回车或换行结束。

| 命令 | 处理逻辑 |
|---:|---|
| `1` | LED0_ON |
| `2` | LED0_OFF |
| `3` | `Alarm_Voice()`，desc=求救信息 |
| `4` | `Alarm_Fire()`，desc=着火 |
| `5` | `Alarm_Kill()`，desc=杀人 |
| `6` | `Alarm_Fight()`，desc=打架 |
| `7` | `Alarm_Kidnap()`，desc=绑架 |
| `8` | `Alarm_Explosion()`，desc=爆炸 |
| `9` | `Alarm_Blood()`，desc=流血 |
| `10` | `Alarm_Faint()`，desc=晕倒 |
| `11` | `Alarm_StopHit()`，desc=别打我 |
| `12` | `Alarm_Robbery()`，desc=抢劫 |
| `13` | `Alarm_HelpMe()`，desc=救我 |
| `14` | `Alarm_CallPeople()`，desc=来人呀 |
| `15` | `Alarm_GroupFight()`，desc=群殴 |
| `16` | `Alarm_DontMove()`，desc=别动 |
| `17` | `General_alarm()`，低优先级一般报警 |

### 6.4 OSS 上传接口

| 项目 | 值 |
|---|---|
| OSS Host | `test-record001.oss-cn-beijing.aliyuncs.com` |
| 端口 | `80` |
| 对象前缀 | `Record/Test/` |
| 上传方法 | ESP8266 TCP 透传，手写 HTTP PUT |
| Content-Type | `audio/wav` |
| 成功判断 | 等待 `HTTP/1.1 200` 或 `HTTP/1.1 201` |
| 分块读取 | 每次从 TF 卡读 256 字节发送 |
| URL 上报格式 | `http://test-record001.oss-cn-beijing.aliyuncs.com/Record/Test/<文件名>.wav` |

当前上传代码没有使用 `oss_c_sdk`，而是直接构造 HTTP 请求：

```http
PUT /Record/Test/<file>.wav HTTP/1.1
Host: test-record001.oss-cn-beijing.aliyuncs.com
Content-Type: audio/wav
Content-Length: <fileSize>
Connection: close

<binary wav data>
```

注意：这个 HTTP PUT 没有签名头，也没有 Authorization。能否成功取决于 OSS Bucket 是否允许匿名写入或是否有其他网关策略。正式项目中这属于高风险设计。

---

## 7. 运行原理详解

### 7.1 上电初始化流程

```text
main()
  ├─ Serial_Init()             # USART3 语音模块
  ├─ LED_Init()                # PA0 LED
  ├─ Key_Init()                # PB5/PB9
  ├─ USART_Config()            # USART1 调试串口
  ├─ Get_STM32_UID()           # 打印芯片唯一 ID
  ├─ ESP8266_Init()            # USART2 初始化
  ├─ ESP8266_StaTcpClient()
  │    ├─ AT 测试
  │    ├─ 设置 STA 模式
  │    ├─ 连接 WiFi
  │    ├─ 配置 MQTT 用户
  │    ├─ 连接 OneNET MQTT
  │    ├─ 订阅 post/reply
  │    ├─ 订阅 property/set
  │    ├─ 配置 NTP
  │    └─ 获取一次实时时间
  └─ while(1) 主循环
```

### 7.2 报警触发流程

#### 按键报警

```text
PB9 按下
  ↓
Key_GetNum() 返回 2
  ↓
Alarm_ButtomDown(2)
  ↓
MQTT 上报 eventType=按钮6
MQTT 上报 desc=无
MQTT 上报 alarm_level=0
MQTT 上报 time=当前 NTP 时间
  ↓
LED0_ON
  ↓
App_TryStartAlarmRecording()
```

#### 语音报警

```text
语音模块识别关键词
  ↓
USART3 发出数字命令，如 "4\r\n"
  ↓
main.c 收到命令并组成字符串
  ↓
调用对应 Alarm_xxx()，如 Alarm_Fire()
  ↓
MQTT 上报 eventType=语音
MQTT 上报 desc=着火
MQTT 上报 alarm_level=0
MQTT 上报 time=当前 NTP 时间
  ↓
LED0_ON
  ↓
开始录音
```

### 7.3 录音流程

```text
App_TryStartAlarmRecording()
  ↓
Wav_Start()
  ├─ TF_Card_Mount()
  ├─ Time_GenerateRecordPath()
  ├─ f_open(... FA_CREATE_NEW | FA_WRITE)
  ├─ 写入 44 字节 WAV 头占位
  ├─ f_lseek 到 44
  └─ Audio_Capture_Start()
        ├─ DMA1_Channel1_ADC1_Init()
        ├─ ADC1_CH1_Init()
        └─ TIM3_TRGO_Init(8000)
```

采集参数：

| 参数 | 值 |
|---|---:|
| 采样率 | 8000 Hz |
| 录音时长 | 15 秒 |
| 位深 | 16-bit PCM |
| 通道数 | 单声道 |
| 目标数据字节数 | `8000 * 15 * 2 = 240000 bytes` |
| WAV 总大小 | 约 `240044 bytes` |
| DMA 缓冲区 | 800 个 `uint16_t` ADC 样本 |
| 半缓冲 | 400 个样本 |

ADC 到 PCM 的转换逻辑：

```c
int32_t x = (int32_t)g_AudioAdcBuf[startIndex + i] - 2048;
x <<= 4;
if (x > 32767) x = 32767;
if (x < -32768) x = -32768;
pcm[i] = (int16_t)x;
```

含义：ADC 原始 12-bit 采样大致以 2048 为中心点，转换为 signed 16-bit PCM。

### 7.4 WAV 文件保存流程

1. 录音开始时先写入一个 44 字节 WAV 头。
2. 录音过程中不断追加 PCM 数据。
3. 达到 15 秒目标字节数后：
   - 停止 TIM3/ADC/DMA。
   - 回到文件开头重写 WAV 头，把真实 `dataBytes` 写进去。
   - `f_sync()`。
   - `f_close()`。
   - 调用 `Wav_UploadToOss()`。

文件名生成逻辑：

```text
0:/YYYY-MM-DD_HH_MM_SS_<index>.wav
```

代码中实际是先取 `g_MqttTimeRecvBuf`，然后把空格、冒号、斜杠替换成下划线。

示例：

```text
0:/2026-04-13_18_48_42_0.wav
```

如果没有拿到新时间，默认可能使用 `record_0.wav` 这类回退文件名。

### 7.5 OSS 上传流程

```text
Wav_UploadToOss(path)
  ├─ f_open(path, FA_READ)
  ├─ f_size()
  ├─ 拼 objectKey = Record/Test/<fileName>
  ├─ 拼 HTTP PUT 头
  ├─ ESP8266_Link_Server(TCP, OSS_HOST, 80)
  ├─ ESP8266_UnvarnishSend() 进入透传模式
  ├─ AT+CIPSEND
  ├─ 发送 HTTP Header
  ├─ 循环读取 TF 卡 256 字节并发送
  ├─ 等待 HTTP/1.1 200 或 201
  ├─ 退出透传模式
  ├─ CIPCLOSE
  └─ 上传成功后 MQTT 上报 audio_url
```

---

## 8. 关键全局变量清单

| 变量 | 所在文件 | 作用 |
|---|---|---|
| `flag` | `main.c` | 网络测试标志，来自 `+IPD` 数据，`a` 开灯，`c` 关灯 |
| `g_PassthroughMode` | `main.c` | AT 透传模式标志 |
| `uart_buffer[20]` | `main.c` | USART3 语音命令缓冲 |
| `g_AudioHalfReady` | `main.c` | DMA 半缓冲可写标志 |
| `g_AudioFullReady` | `main.c` | DMA 全缓冲可写标志 |
| `g_AudioAdcBuf[800]` | `main.c` | ADC DMA 循环缓冲区 |
| `g_WavRecording` | `main.c` | 是否正在录音 |
| `g_WavDataBytes` | `main.c` | 已写入 WAV 的 PCM 数据字节数 |
| `g_WavPath[64]` | `main.c` | 当前 WAV 文件路径 |
| `g_OssObjectKey[128]` | `main.c` | OSS 对象 key |
| `upload_url[1024]` | `main.c` | 上报 audio_url 的 MQTT AT 指令 |
| `strEsp8266_Fram_Record` | `esp8266.c` | ESP8266 接收帧缓冲 |
| `g_MqttConnected` | `esp8266.c` | MQTT 是否连接成功 |
| `g_MqttSubRecvPending` | `esp8266.c` | 是否有云端 property/set 待处理 |
| `g_MqttTimRecvPending` | `esp8266.c` | 是否有新 NTP 时间待用于文件命名 |
| `g_MqttTimeRecvBuf` | `esp8266.c` | 最近一次获取到的时间字符串 |

---

## 9. 技术栈清单

### 9.1 嵌入式基础

- STM32F103C8T6
- Cortex-M3 / CMSIS
- STM32F10x Standard Peripheral Library
- Keil MDK-ARM / ARMCC 5
- GPIO
- USART
- SPI
- ADC
- DMA
- TIM
- NVIC 中断
- SysTick 延时

### 9.2 文件系统与存储

- FatFs
- TF/SD 卡 SPI 模式
- WAV 文件格式
- 16-bit PCM 单声道音频

### 9.3 网络与云

- ESP8266 AT 固件
- WiFi STA 模式
- MQTT AT 指令
- OneNET 物模型主题
- NTP 时间同步
- TCP 透传
- HTTP/1.1 PUT
- 阿里云 OSS Bucket/Object URL

### 9.4 已包含但未实际主用的代码

- `oss_c_sdk/`：阿里云 OSS C SDK 源码存在，但未加入 Keil 工程文件的编译列表。
- `Onenet/MqttKit.c/.h`：工程中加入了该文件，但当前核心 MQTT 逻辑主要由 ESP8266 的 AT MQTT 命令完成。
- `OLED.c/.h`、`bsp_Alarm.c/.h`：驱动存在，但主流程中基本没有使用。

---

## 10. 可直接接手的开发入口

### 10.1 想改 WiFi

文件：`Hardware/esp8266.h`

```c
#define macUser_ESP8266_ApSsid "HiwonderESP"
#define macUser_ESP8266_ApPwd  "hiwonder"
```

### 10.2 想改 OneNET 产品/设备/鉴权

文件：`Hardware/esp8266.h`、`Hardware/esp8266.c`、`Alarm/alarm.c`、`User/main.c`

需要统一修改：

- `MQTT_USER`
- `MQTT_CONN_CMD`
- `MQTT_SUB_CMD`
- `MQTT_SUB_PROPERTY_SET`
- 所有 `AT+MQTTPUB` 中的主题：`$sys/l4JQEioAnm/test1/...`
- `UPLOAD_URL_TEMPLATE`
- `ALARM_xxx` 宏中的主题

当前主题被大量硬编码，不是集中配置式设计。

### 10.3 想新增语音关键词

修改两个地方：

1. `Alarm/alarm.c`
   - 新增一个 `ALARM_XXX_detect` 宏。
   - 新增一个 `Alarm_XXX()` 函数。

2. `User/main.c`
   - 在 USART3 命令判断中新增：

```c
else if (strcmp(uart_buffer, "18") == 0)
{
    Alarm_XXX();
}
```

### 10.4 想改录音时长/采样率

文件：`User/main.c`

```c
#define AUDIO_SAMPLE_RATE_HZ   8000
#define AUDIO_RECORD_SECONDS   15
```

注意：采样率改变会影响 TIM3 触发频率、WAV 头、文件大小、上传时间。

### 10.5 想改 OSS Bucket 或目录

文件：`User/main.c`

```c
#define OSS_HTTP_HOST      "test-record001.oss-cn-beijing.aliyuncs.com"
#define OSS_HTTP_PORT      "80"
#define OSS_OBJECT_PREFIX  "Record/Test/"
```

### 10.6 想改音频输入引脚

文件：`User/main.c`

当前：ADC1 Channel 1 / PA1。

```c
GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_239Cycles5);
```

如果改引脚，需要同步改 GPIO 和 ADC Channel。

### 10.7 想改按键引脚

文件：`Hardware/Key.c`

当前 PB5、PB9。

### 10.8 想生成 HEX

Keil 工程当前 `CreateHexFile=0`。接手烧录时通常需要在 Keil 中开启：

```text
Options for Target → Output → 勾选 Create HEX File
```

或者修改 `Project.uvprojx` 中对应配置。

---

## 11. 当前项目的主要问题与风险

### 11.1 云端参数全部硬编码

WiFi、OneNET 产品 ID、设备名、鉴权 token、MQTT topic、OSS Bucket 全部写死在 `.h/.c` 文件中。后续接手建议集中到一个 `config.h`，至少把以下内容统一管理：

- WiFi SSID/PWD
- OneNET Product ID
- Device Name
- MQTT Token/Sign
- OSS Host
- OSS Object Prefix
- 音频参数

### 11.2 OSS 上传没有鉴权

`Wav_UploadToOss()` 手写 HTTP PUT，未添加 OSS 签名头。除非 Bucket 放开匿名写入，否则真实环境会失败。即使能成功，匿名写入也有严重安全风险。

更合理的架构是：

```text
设备 → 后端请求临时上传凭证/预签名 URL → 设备 PUT 上传 → 后端/OneNET 记录 audio_url
```

或者：

```text
设备 → OneNET/后端上报事件 → 后端生成 OSS 上传策略 → 设备上传
```

### 11.3 MQTT AT 指令 JSON 转义复杂，维护成本高

大量 `AT+MQTTPUB` 把 JSON 写在宏字符串里，包含 `\"`、`\,`、Unicode 编码，极易出错。

建议封装一个函数：

```c
MQTT_PostProperty_String(const char *name, const char *value);
MQTT_PostProperty_Number(const char *name, int value);
```

由函数统一拼 JSON 和 topic。

### 11.4 录音期间会屏蔽新报警

代码中多处判断 `App_IsRecording()`，录音时不处理新的语音报警和按键报警。这能避免文件/网络状态冲突，但也意味着 15 秒内会丢弃后续事件。

### 11.5 ESP8266 接收缓冲是全局共享，容易被不同流程抢占

`strEsp8266_Fram_Record.Data_RX_BUF` 同时服务：

- AT 指令等待回复；
- MQTT 连接状态；
- MQTT 订阅消息；
- OSS 上传 HTTP 响应；
- NTP 时间响应；
- 调试透传。

当前代码用 `g_ESP8266RawBusy` 降低 OSS 上传时的解析干扰，但整体仍是共享状态机，复杂场景下容易串包。

### 11.6 TF 卡写文件与网络上传在主循环中同步执行

录音结束后上传 OSS 是同步阻塞式流程。上传期间：

- 主循环会被长时间占用；
- 新报警处理能力下降；
- ESP8266 状态异常时可能卡顿；
- 大文件上传稳定性取决于 WiFi 和 AT 响应。

### 11.7 按键读取是阻塞式

`Key_GetNum()` 按下后会等待松手。一般教学项目可用，正式项目建议改为中断或非阻塞消抖。

### 11.8 语音命令缓冲只有 20 字节

当前只接收数字命令足够，但如果后续语音模块直接发中文/长文本，需要扩大缓冲并做超长保护。

### 11.9 构建日志中有 2 个转义警告

`Hardware/esp8266.c` 第 191、218 行附近出现 `#192-D: unrecognized character escape sequence`。原因大概率是 AT+MQTTPUB 的 JSON 字符串中转义写法不规范。虽然当前能编译，但建议整理字符串拼接方式。

### 11.10 `oss_c_sdk` 目录可能误导接手者

项目包含 `oss_c_sdk/`，但 Keil 工程文件没有把它纳入核心编译列表，实际 OSS 上传不是用 SDK 完成的。接手时不要误以为已经接入阿里云 OSS SDK。

---

## 12. 建议的后续重构方向

### 12.1 第一阶段：不改变功能，先整理可维护性

1. 新增 `AppConfig.h`：统一 WiFi、OneNET、OSS、音频参数。
2. 新增 `Cloud_OneNET.c/.h`：封装属性上报。
3. 新增 `Cloud_OSS.c/.h`：从 `main.c` 拆出 OSS 上传。
4. 新增 `AudioRecorder.c/.h`：从 `main.c` 拆出 WAV/ADC/DMA。
5. 新增 `AlarmService.c/.h`：整理报警类型映射。
6. 删除或标注未使用模块，避免评审/接手者误判。

建议目标结构：

```text
User/
├── main.c
├── app_config.h
├── app_types.h
Services/
├── alarm_service.c/.h
├── audio_recorder.c/.h
├── cloud_onenet.c/.h
├── cloud_oss.c/.h
Drivers/
├── esp8266_at.c/.h
├── voice_serial.c/.h
├── sd_card.c/.h
├── led.c/.h
└── key.c/.h
```

### 12.2 第二阶段：增强稳定性

- 建立明确状态机：`IDLE / ALARM_POSTING / RECORDING / UPLOADING / ERROR_RETRY`。
- 把 ESP8266 AT 接收改成环形缓冲区。
- 给每个 AT 指令加错误码与重试策略。
- 上传失败时保留本地 WAV，后续重传。
- MQTT 上报失败时记录待补偿事件。
- 录音文件名增加设备 ID，避免多设备同名。

### 12.3 第三阶段：安全化云端上传

建议不要让设备直接匿名 PUT OSS。更安全方案：

```text
设备触发报警
  ↓
OneNET 上报事件
  ↓
后端收到事件或轮询 OneNET
  ↓
后端生成预签名 OSS 上传 URL
  ↓
设备使用临时 URL 上传音频
  ↓
后端记录完整报警事件
```

如果设备端资源有限，也可以让设备只上报事件，音频上传转为局域网/网关/后端代理处理。

---

## 13. 接手时优先阅读顺序

建议按这个顺序读代码：

1. `User/main.c`
   - 先看初始化和主循环。
   - 再看 WAV/ADC/DMA 录音函数。
   - 最后看 OSS 上传函数。

2. `Alarm/alarm.c`
   - 理解每种报警映射到哪些 OneNET 属性。

3. `Hardware/esp8266.h`
   - 看 WiFi、MQTT、NTP、topic、鉴权参数。

4. `Hardware/esp8266.c`
   - 看 AT 指令、MQTT 连接、NTP 解析、property/set 处理。

5. `User/stm32f10x_it.c`
   - 看 ESP8266 接收中断和 DMA 中断。

6. `Hardware/sd_spi.c`、`diskio.c`、`Hardware/tf_card.c`
   - 看 TF 卡和 FatFs 如何接起来。

---

## 14. 快速调试建议

### 14.1 串口调试

- USART1：115200，作为 printf 输出口。
- 建议使用 USB-TTL 接 PA9/PA10/GND。
- 启动时应该看到类似：

```text
USART1 OK
STM32 UID: xxxxxxxxxxxxxxxxxxxxxxxx
Configuring TCP ESP8266 ......
Configurating MQTT ESP8266 ......
```

### 14.2 WiFi 调试

检查 ESP8266 是否能收到：

```text
AT
AT+CWMODE=1
AT+CWJAP="HiwonderESP","hiwonder"
```

如果卡在 WiFi 连接，先确认热点名称密码。

### 14.3 OneNET 调试

重点看：

- MQTT 是否出现 `+MQTTCONNECTED`。
- `AT+MQTTSUB` 是否返回 OK。
- OneNET 物模型里是否能看到 `eventType/desc/alarm_level/time/audio_url`。

### 14.4 录音调试

重点看：

- TF 卡是否挂载成功。
- 是否打印 `WAV rec start: 0:/...wav`。
- 是否打印 `WAV saved, bytes=240000`。
- 文件是否能在电脑上作为 WAV 播放。

### 14.5 OSS 调试

重点看：

- 是否 TCP 连接到 OSS host 成功。
- 是否进入透传成功。
- 是否等待到 `HTTP/1.1 200` 或 `201`。
- 如果返回 403，大概率是 OSS 鉴权问题。

---

## 15. 当前版本的功能边界

已经实现/基本具备：

- STM32 基础初始化。
- ESP8266 WiFi 连接。
- OneNET MQTT 连接、属性上报、属性设置回复。
- 按键报警。
- 语音模块命令触发报警。
- NTP 时间获取。
- ADC + DMA 音频采集。
- WAV 文件生成。
- TF 卡保存。
- OSS HTTP PUT 上传。
- 上传成功后 audio_url 上报。
- 调试串口与 AT 透传模式。

未完整或存在疑问：

- OSS 正式鉴权机制未实现。
- 报警事件与音频 URL 没有本地结构化记录表，只靠 OneNET 属性分多次上报。
- 多次属性上报之间没有事件 ID 关联字段，云端需要按时间或最后状态拼接。
- 录音期间会屏蔽新报警。
- 网络异常重试和离线缓存机制较弱。
- OLED、蜂鸣器、OSS SDK 等模块存在但当前主链路未充分使用。

---

## 16. 给接手者的结论

这个项目不是一个完整的“后端系统”，而是一个已经具备核心闭环的 STM32 物联网终端固件。它的主线非常清晰：**触发报警、上报 OneNET、录音、存 TF 卡、上传 OSS、回传音频 URL**。

接手时不要先陷入所有文件，重点抓住四条线：

1. **业务线**：`main.c` + `alarm.c`。
2. **云通信线**：`esp8266.c/.h`。
3. **音频线**：`ADC1 + DMA1 + TIM3 + WAV`，主要在 `main.c`。
4. **存储线**：`tf_card.c + sd_spi.c + diskio.c + FatFs`。

如果要把它升级成正式比赛/工程作品，优先做三件事：

1. 把所有云端参数集中配置化。
2. 把 OneNET 上报和 OSS 上传封装成独立服务层。
3. 解决 OSS 鉴权、上传失败重试、事件 ID 关联这三个工程化问题。
