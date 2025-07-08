#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "iot_gpio_ex.h"
#include "iot_adc.h"
#include "iot_errno.h"
#include "app_demo_aht20.h"
#include "app_demo_mq2.h"
#include "body_hw.h"
#include "rain.h"
#include "iot_pwm.h"
#include "dht11.h"
#include <hi_task.h>
#include <string.h>
#include <hi_wifi_api.h>
#include <hi_mux.h>
#include <hi_io.h>
#include <hi_gpio.h>
#include "iot_config.h"
#include "iot_log.h"
#include "iot_main.h"
#include "iot_profile.h"
#include <cJSON.h>
#include "hi_event.h"
#include <hi_wifi_api.h>
#include <lwip/ip_addr.h>
#include <lwip/netifapi.h>
#include <hi_types_base.h>
#include <hi_task.h>
#include <hi_mem.h>
#include "wifi_device.h"
#include "cmsis_os2.h"
#include "wifi_device_config.h"
#include "lwip/api_shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/tcp.h" 
#include "lwip/inet.h"
#include "lwip/netifapi.h"
#include <hi_io.h>
#include <hi_gpio.h>
#include <hi_task.h>
#include "udp_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 互斥锁定义 */
static osMutexId_t g_sensor_mutex;
static osMutexId_t g_motor_mutex;
static osMutexId_t g_udp_mutex;
static osMutexId_t g_cloud_mutex;

/* 线程ID定义 */
static osThreadId_t sensor_thread_id;
static osThreadId_t motor_thread_id;
static osThreadId_t udp_thread_id;
static osThreadId_t cloud_thread_id;

/* 共享数据结构 */
typedef struct {
    float light_intensity;
    float temperature;
    float humidity;
    float combustible_gas;
    uint8_t human_presence;
    uint16_t rain_value;
    int motor_state;
    int c_motor_state;
    int window_position;
    int curtain_position;
} SharedData;

static SharedData g_shared_data;

int const T_MAX = 180,ang_MAX = 180;
int t = 0;
int16_t ang=0;
int i = 0, j = 0 ;

int Window_P = 0, Curten_P = 0;;

/* attribute initiative to report */
#define TAKE_THE_INITIATIVE_TO_REPORT
#define ONE_SECOND                          (1000)
/* oc request id */
#define CN_COMMADN_INDEX                    "commands/request_id="
#define WECHAT_SUBSCRIBE_LIGHT              "light"
#define WECHAT_SUBSCRIBE_LIGHT_ON_STATE     "1"
#define WECHAT_SUBSCRIBE_LIGHT_OFF_STATE    "0"

int g_ligthStatus = -1;
typedef void (*FnMsgCallBack)(hi_gpio_value val);

typedef struct FunctionCallback {
    hi_bool  stop;
    hi_u32 conLost;
    hi_u32 queueID;
    hi_u32 iotTaskID;
    FnMsgCallBack    msgCallBack;
}FunctionCallback;
FunctionCallback g_functinoCallback;

static void DeviceConfigInit(hi_gpio_value val)
{
    hi_io_set_func(HI_IO_NAME_GPIO_9, HI_IO_FUNC_GPIO_9_GPIO);
    hi_gpio_set_dir(HI_GPIO_IDX_9, HI_GPIO_DIR_OUT);
    hi_gpio_set_ouput_val(HI_GPIO_IDX_9, val);
}

static int  DeviceMsgCallback(FnMsgCallBack msgCallBack)
{
    g_functinoCallback.msgCallBack = msgCallBack;
    return 0;
}

static void wechatControlDeviceMsg(hi_gpio_value val)
{
    DeviceConfigInit(val);
}

#define LED_INTERVAL_TIME_US 300000
#define LED_TASK_STACK_SIZE 512
#define LED_TASK_PRIO 25

// 定义方向引脚
#define DIR_PIN         12  // 方向控制引脚

static int g_iState = 0;
#define IOT_PWM_PORT_PWM0   0
#define IOT_PWM_BEEP        7
#define IOT_GPIO_KEY        5

// 定义电机状态枚举
enum MotorState {
    MOTOR_STOP = 0,     // 停止状态
    MOTOR_CW,           // 顺时针旋转
    MOTOR_CCW           // 逆时针旋转
};

// 全局状态变量
static enum MotorState g_motorState = MOTOR_STOP;
int c_motorState = 0;

/* 系统配置参数 */
#define SENSOR_READ_INTERVAL_MS 200  // 传感器读取间隔(ms)
#define TASK_STACK_SIZE 4096          // 任务栈大小
#define PRIORITY_NORMAL osPriorityNormal

/* 全局数据结构 */
typedef struct {
    float light_intensity;        // 光照强度百分比
    float temperature;            // 温度(℃)
    float humidity;               // 湿度(%)
    float combustible_gas;        // 可燃气体浓度(ppm)
    uint8_t human_presence;       // 人体存在状态(0/1)
    uint16_t rain_value;          // 雨量值(0-100%)
} SensorData;

/* 全局变量 */
static SensorData g_sensor_data;
static osMutexId_t g_i2c_mutex;   // I2C总线互斥锁

/* 光敏传感器读取函数 */
static float ReadLightSensor(void) {
    unsigned short data = 0;
    const float R2 = 10.0f;          // 固定电阻值10kΩ
    const float MAX_VOLTAGE = 3.3f;   // 传感器供电电压
    const float ADC_REF = 1.8f;       // ADC参考电压
    const int ADC_RES = 4096;         // 12位ADC分辨率
    
    if (AdcRead(IOT_ADC_CHANNEL_4, &data, 
               IOT_ADC_EQU_MODEL_4, 
               IOT_ADC_CUR_BAIS_DEFAULT, 0xff) != IOT_SUCCESS) {
        printf("[ERROR] Light sensor read failed!\n");
        return -1.0f;
    }
    
    float adc_voltage = (float)data * ADC_REF / ADC_RES;
    float actual_voltage = adc_voltage * (1.0f + R2);
    float intensity = 100.0f * (MAX_VOLTAGE - actual_voltage) / MAX_VOLTAGE;

    return adc_voltage;
}

/* 华为云属性上报函数 - 完全匹配您的格式 */
static void ReportToHuaweiCloud(int DHT11_T, int DHT11_H, 
                               int HW, int Rain,
                               float Light, float MQ2,
                               int Motor,int Window_P,int Curten_P)
{
    // 构造符合要求的JSON消息
    char payload[256];
    snprintf(payload, sizeof(payload),
            "{\"services\":[{\"service_id\":\"hi3861\",\"properties\":{"
            "\"DHT11_T\":%d,"    // 温度
            "\"DHT11_H\":%d,"    // 湿度
            "\"HW\":%d,"         // 人体存在
            "\"Rain\":%d,"       // 雨量值
            "\"Light\":%.2f,"    // 光照强度
            "\"MQ2\":%.2f,"      // 可燃气体浓度
            "\"Motor\":%d,"      // 电机状态（整数）
            "\"Window_P\":%d,"   // 窗户位置（整数）
            "\"Curten_P\":%d"    // 窗帘位置（整数）
            "}}]}",              
        DHT11_T,
        DHT11_H,
        HW,
        Rain,
        Light,
        MQ2,
        Motor,
        Window_P,
        Curten_P);
    
    // 华为云特定的主题
    const char *huawei_topic = "$oc/devices/6863a00ad582f2001836ef74_dev1/sys/properties/report";
    
    // 通过MQTT上报
    IotSendMsg(0, huawei_topic, payload);
    printf("Reported to Huawei Cloud: %s\n", payload);
}

#define WECHAT_SUBSCRIBE_MOTOR "motor"       // 电机命令标识
#define WECHAT_SUBSCRIBE_MOTOR_CW "m1"       // 开启命令
#define WECHAT_SUBSCRIBE_MOTOR_CWW "m2"
#define WECHAT_SUBSCRIBE_MOTOR_OFF "off"     // 关闭命令

static void DemoMsgRcvCallBack(int qos, const char *topic, const char *payload)
{
    IOT_LOG_DEBUG("RCVMSG:QOS:%d TOPIC:%s PAYLOAD:%s\r\n", qos, topic, payload);
    /* 电机控制逻辑 */
    if (strstr(payload, WECHAT_SUBSCRIBE_MOTOR) != NULL) {
        if (strstr(payload, WECHAT_SUBSCRIBE_MOTOR_CW) != NULL) {
            // 执行电机开启操作
            printf("Start ON!!!@@@");
            g_motorState = MOTOR_CW;
        } else if(strstr(payload, WECHAT_SUBSCRIBE_MOTOR_CWW) != NULL){
            printf("Start ON!!!@@@");
            g_motorState = MOTOR_CCW;
        }
        else if (strstr(payload, WECHAT_SUBSCRIBE_MOTOR_OFF) != NULL) {
            // 执行电机关闭操作
            printf("Motor Off!!!@@@");
            g_motorState = MOTOR_STOP;
        }
    }
    
    return; // 原返回HI_NULL有误，修正为void返回
}


//udp接收消息
#define INVAILD_SOCKET          (-1)
#define FREE_CPU_TIME_20MS      (20)
#define INVALID_VALUE           "202.202.202.202"

#define NATIVE_IP_ADDRESS       "192.168.137.25" // 用户查找本地IP后需要进行修改
#define WECHAT_MSG_LIGHT_ON     "_light_on"
#define WECHAT_MSG_LIGHT_OFF    "_light_off"
#define DEVICE_MSG_LIGHT_ON     "device_light_on"
#define DEVICE_MSG_LIGHT_OFF    "device_light_off"
#define WECHAT_MSG_UNLOAD_PAGE  "UnoladPage"
#define RECV_DATA_FLAG_OTHER    (2)
#define HOST_PORT               (5566)
#define DEVICE_PORT             (6655)

#define UDP_RECV_LEN (255)

typedef void (*FnMsgCallBack)(hi_gpio_value val);

void WeChatControlDeviceMsg(hi_gpio_value val)
{
    DeviceConfigInit(val);
}

int UdpTransportInit(struct sockaddr_in serAddr, struct sockaddr_in remoteAddr)
{
    int sServer = socket(AF_INET, SOCK_DGRAM, 0);
    if (sServer == INVAILD_SOCKET) {
        printf("create server socket failed\r\n");
        close(sServer);
    }
    // 本地主机ip和端口号
    serAddr.sin_family = AF_INET;
    serAddr.sin_port = htons(HOST_PORT);
    serAddr.sin_addr.s_addr = inet_addr(NATIVE_IP_ADDRESS);
	printf("sServer:%d\r\n", sServer);
    hi_sleep(100);
    if (bind(sServer, (struct sockaddr*)&serAddr, sizeof(serAddr)) == -1) {
        printf("bind socket failed\r\n");
        close(sServer);
    }
    // 对方ip和端口号
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(DEVICE_PORT);
    serAddr.sin_addr.s_addr = htons(INADDR_ANY);


    printf("init successfully\n");
    return sServer;
}

int extract_integer(const char* input) {
    // 跳过前缀（"m1"或"m2"）
    const char* ptr = input;
    
    // 检查并跳过"m"
    if (*ptr != 'm' && *ptr != 'c') {
        printf("error:input 'm' head!\n");
        return 0;
    }
    ptr++;
    
    // 检查并跳过数字（1或2）
    if (*ptr != '1' && *ptr != '2') {
        printf("错误：'m'后应跟'1'或'2'\n");
        return 0;
    }
    ptr++;
    
    // 跳过空格
    while (*ptr == ' ') {
        ptr++;
    }
    
    // 检查是否有数字部分
    if (*ptr == '\0') {
        printf("错误：缺少数字部分\n");
        return 0;
    }
    
    // 提取数字
    char num_str[32] = {0}; // 存储数字部分的缓冲区
    int i = 0;
    
    // 复制数字字符（包括可能的负号）
    if (*ptr == '-') {
        num_str[i++] = '-';
        ptr++;
    }
    
    while (isdigit((unsigned char)*ptr)) {
        num_str[i++] = *ptr++;
    }
    
    // 检查是否提取到有效数字
    if (i == 0 || (i == 1 && num_str[0] == '-')) {
        printf("错误：无效的数字格式\n");
        return 0;
    }
    
    // 检查是否有多余字符
    if (*ptr != '\0') {
        printf("警告：输入字符串有多余字符 '%s'\n", ptr);
    }
    
    // 转换为整数
    return atoi(num_str);
}

/* 传感器读取线程 */
static void SensorTask(void *arg) {
    (void)arg;
    // 初始化各传感器
    BODY_HW_Init();   // 人体红外
    RAIN_Init();      // 雨滴传感器

    
    while(DHT11_Init())	//DHT11初始化	
	{
		printf("DHT11 Init Error!!\r\n");
 		usleep(100000);
	}	

    u8 temperature = 0;
    u8 humidity = 0;

    while (1) {
        /* 读取传感器数据 */
        float light = ReadLightSensor();
        
        osMutexAcquire(g_sensor_mutex, osWaitForever);
        Mq2GetData();
        float gas = GetCombuSensorValue();
        osMutexRelease(g_sensor_mutex);
        
        uint8_t human = BODY_HW_GetData();
        uint16_t rain = RAIN_GetData();
        DHT11_Read_Data(&temperature,&humidity);
        
        if (1) {
            // 数据归一化处理
            light = light * 100;
            light = (90 - light) / 0.5;
            rain = (rain - 55) / 0.35;

            /* 更新共享数据 */
            //osMutexAcquire(g_sensor_mutex, osWaitForever);
            g_shared_data.light_intensity = light;
            g_shared_data.combustible_gas = gas;
            g_shared_data.human_presence = human;
            g_shared_data.rain_value = rain;
            g_shared_data.temperature = temperature;
            g_shared_data.humidity = humidity;
            //osMutexRelease(g_sensor_mutex);
        }

        /* 打印传感器数据 */
        printf("\n===== Sensor Data =====\n");
        printf("Light: %.2f\n", light);
        printf("Temperature: %d\n", temperature);
        printf("Humidity: %d\n", humidity);
        printf("Gas: %.2fppm\n", gas);
        printf("Human: %s\n", human ? "Detected" : "None");
        printf("Rain: %d%%\n", rain);
        printf("======================\n");
        
        //osDelay(SENSOR_READ_INTERVAL_MS * );
        int DHT11_T = g_shared_data.temperature;
        int DHT11_H = g_shared_data.humidity;
        int HW = g_shared_data.human_presence;
        int Rain = g_shared_data.rain_value;
        float Light = g_shared_data.light_intensity;
        float MQ2 = g_shared_data.combustible_gas;
        int Motor = g_shared_data.motor_state;
        int Window_P = g_shared_data.window_position;
        int Curten_P = g_shared_data.curtain_position;
        
        /* 上报到华为云 */
        ReportToHuaweiCloud(DHT11_T, DHT11_H, HW, Rain, Light, MQ2, 
                           Motor, Window_P, Curten_P);
        osDelay(50);

    }
}

/* 电机控制线程 */
static void MotorControlTask(void *arg) {
    (void)arg;

    // 初始化PWM引脚
    IoTGpioInit(IOT_PWM_BEEP);
    IoSetFunc(IOT_PWM_BEEP, 5); /* 设置IO7的功能为PWM */
    IoTGpioSetDir(IOT_PWM_BEEP, IOT_GPIO_DIR_OUT);
    IoTPwmInit(IOT_PWM_PORT_PWM0);
    
    // 初始化方向引脚
    IoTGpioInit(DIR_PIN);
    IoSetFunc(DIR_PIN, 0);
    IoTGpioSetDir(DIR_PIN, IOT_GPIO_DIR_OUT);
    IoSetPull(DIR_PIN, IOT_IO_PULL_NONE);
    
    sg90_init();

    while (1) {
        int motor_state;
        int c_motor_state;
        int run_time = 0;
        int curtain_time = 0;
        
        /* 获取电机状态 */
        //osMutexAcquire(g_motor_mutex, osWaitForever);
        motor_state = g_shared_data.motor_state;
        c_motor_state = g_shared_data.c_motor_state;
        //osMutexRelease(g_motor_mutex);
        
        /* 处理步进电机控制 */
        if (c_motor_state == 0 &&  motor_state != MOTOR_STOP && i > 0) {
            printf("run!!! %d  t: %d\n",i,t);
            switch (motor_state) {
                case MOTOR_CW:
                    IoTGpioSetOutputVal(DIR_PIN, 1);
                    IoTPwmStart(IOT_PWM_PORT_PWM0, 50, 4000);
                    t--;
                    break;
                case MOTOR_CCW:
                    IoTGpioSetOutputVal(DIR_PIN, 0);
                    IoTPwmStart(IOT_PWM_PORT_PWM0, 50, 4000);
                    t++;
                    break;
                default:
                    IoTPwmStop(IOT_PWM_PORT_PWM0);
                    break;
            }
            i--;
        } else {
            g_shared_data.motor_state = MOTOR_STOP;
            IoTPwmStop(IOT_PWM_PORT_PWM0);
        }
        
        /* 处理舵机控制 */
        if (c_motor_state != 0 && j > 0) {
            printf("run!!! %d\n",j);
            switch (c_motor_state) {
                case 2: // 顺时针
                    ang += 10;
                    break;
                case 1: // 逆时针
                    ang -= 10;
                    break;
                default:
                    break;
            }
            set_sg90_angle(ang);
            j -= 10;
        } else {
            g_shared_data.c_motor_state = 0;
        }
        
        /* 更新位置信息 */
        Window_P = t * 100 / T_MAX;
        Curten_P = ang * 100 / ang_MAX;
        
        //osMutexAcquire(g_motor_mutex, osWaitForever);
        g_shared_data.window_position = Window_P;
        g_shared_data.curtain_position = Curten_P;
        //osMutexRelease(g_motor_mutex);
        
        //printf("Motor State: %d, Window_P: %d, Curten_P: %d\n", 
        //       motor_state, Window_P, Curten_P);
        
        osDelay(10);
    }
}

/* UDP接收线程 */
static void UdpTask(void *arg) {
    (void)arg;
    WifiStaReadyWait();
    cJsonInit();
    IoTMain();
    IoTSetMsgCallback(DemoMsgRcvCallBack);
    
    struct sockaddr_in serAddr = {0};
    struct sockaddr_in remoteAddr = {0};
    int sServer = UdpTransportInit(serAddr, remoteAddr);
    int addrLen = sizeof(remoteAddr);
    char recvData[UDP_RECV_LEN] = {0};
    static int recvDataFlag = -1;
    char *sendData = NULL;

    while (1) {
        int recvLen = recvfrom(sServer, recvData, UDP_RECV_LEN, 0, 
                             (struct sockaddr*)&remoteAddr, (socklen_t*)&addrLen);
        if (recvLen > 0) {
            if (strstr(inet_ntoa(remoteAddr.sin_addr), INVALID_VALUE) == NULL) {
                printf("Received data: %s\n", recvData);
                
                /* 处理电机控制命令 */
                if (strstr(recvData, "m1")) {
                    int run_time = extract_integer(recvData);
                    i = run_time;
                    //osMutexAcquire(g_motor_mutex, osWaitForever);
                    g_shared_data.motor_state = MOTOR_CW;
                    //osMutexRelease(g_motor_mutex);
                    printf("Motor CW, Time: %d\n", run_time);
                } 
                else if (strstr(recvData, "m2")) {
                    int run_time = extract_integer(recvData);
                    i = run_time;
                    //osMutexAcquire(g_motor_mutex, osWaitForever);
                    g_shared_data.motor_state = MOTOR_CCW;
                    //osMutexRelease(g_motor_mutex);
                    printf("Motor CCW, Time: %d\n", run_time);
                }
                
                /* 处理窗帘控制命令 */
                if (strstr(recvData, "c1")) {
                    int curtain_time = extract_integer(recvData);
                    j = curtain_time;
                    //osMutexAcquire(g_motor_mutex, osWaitForever);
                    g_shared_data.c_motor_state = 1;
                    //osMutexRelease(g_motor_mutex);
                    printf("Curtain CW, Time: %d\n", curtain_time);
                } 
                else if (strstr(recvData, "c2")) {
                    int curtain_time = extract_integer(recvData);
                    j = curtain_time;
                    //osMutexAcquire(g_motor_mutex, osWaitForever);
                    g_shared_data.c_motor_state = 2;
                    //osMutexRelease(g_motor_mutex);
                    printf("Curtain CCW, Time: %d\n", curtain_time);
                }
            }
            
            /* 发送响应 */
            if (recvDataFlag == HI_TRUE) {
                sendData = DEVICE_MSG_LIGHT_ON;
            } 
            else if (recvDataFlag == HI_FALSE) {
                sendData = DEVICE_MSG_LIGHT_OFF;
            } 
            else {
                sendData = "Received message";
            }
            sendto(sServer, sendData, strlen(sendData), 0, 
                  (struct sockaddr*)&remoteAddr, addrLen);
        }
        osDelay(20);
    }
}



/* 系统初始化函数 */
static void SensorSystemInit(void) {
    // 创建互斥锁
    osMutexAttr_t mutex_attr = {
        .name = "SensorMutex",
        .attr_bits = osMutexPrioInherit | osMutexRecursive
    };
    g_sensor_mutex = osMutexNew(&mutex_attr);

    mutex_attr.name = "MotorMutex";
    g_motor_mutex = osMutexNew(&mutex_attr);
    
    mutex_attr.name = "UdpMutex";
    g_udp_mutex = osMutexNew(&mutex_attr);

    // 创建线程
    osThreadAttr_t task_attr = {
        .name = "SensorTask",
        .stack_size = 4096,
        .priority = osPriorityNormal
    };
    sensor_thread_id = osThreadNew(SensorTask, NULL, &task_attr);

    task_attr.name = "MotorTask";
    motor_thread_id = osThreadNew(MotorControlTask, NULL, &task_attr);

    task_attr.name = "UdpTask";
    udp_thread_id = osThreadNew(UdpTask, NULL, &task_attr);
}

/* 主入口函数 */
APP_FEATURE_INIT(SensorSystemInit);