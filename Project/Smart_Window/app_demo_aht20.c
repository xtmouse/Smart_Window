/*
 * Copyright (c) 2022 HiSilicon (Shanghai) Technologies CO., LIMITED.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hi_mux.h>
#include <hi_time.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "app_demo_multi_sample.h"
#include "iot_errno.h"
#include "iot_i2c.h"
#include "app_demo_aht20.h"

#define  AHT_REG_ARRAY_LEN          (6)
#define  AHT_OC_ARRAY_LEN           (6)
#define  AHT_SNED_CMD_LEN           (3)
#define  AHT20_DEMO_TASK_STAK_SIZE  (1024*4)
#define  AHT20_DEMO_TASK_PRIORITY   (25)
#define  AHT_REG_ARRAY_INIT_LEN     (1)
#define  AHT_CALCULATION            (1048576)

#define  AHT_WRITE_COMMAND ((unsigned char)0x00)
#define  AHT_READ_COMMAND ((unsigned char)0x01)

#define IOT_BIT_1 (1)
#define IOT_BIT_4 (4)
#define IOT_BIT_8 (8)
#define IOT_BIT_12 (12)
#define IOT_BIT_16 (16)

#define BUFF_BIT_1 (1)
#define BUFF_BIT_2 (2)
#define BUFF_BIT_3 (3)
#define BUFF_BIT_4 (4)
#define BUFF_BIT_5 (5)

#define CONSTER_50 (50)
#define CONSTER_100 (100)
#define CONSTER_200 (200)

#define LOW_4_BIT ((unsigned char)0x0f)
#define HIGH_4_BIT ((unsigned char)0xf0)

AhtSersonValue sensorV = {0};

float GetAhtSensorValue(AhtSersonType type)
{
    float value = 0;

    switch (type) {
        case AHT_TEMPERATURE:
            value = sensorV.g_ahtTemper;
            break;
        case AHT_HUMIDITY:
            value = sensorV.g_ahtHumi;
            break;
        default:
            break;
    }
    return value;
}

/*
* Check whether the bit3 of the temperature and humidity sensor is initialized successfully,
* otherwise send the setting of 0xbe to set the sensor initialization
*/
static unsigned int Ath20CheckAndInit(unsigned char initCmd,
    unsigned char initHighByte, unsigned char initLowByte)
{
    unsigned int status = 0;

    IotI2cData aht20I2cWriteCmdAddrInit = {0};
    IotI2cData aht20I2cData = { 0 };
    unsigned char recvDataInit[AHT_REG_ARRAY_INIT_LEN] = { 0 };
    unsigned char initSendUserCmd[AHT_SNED_CMD_LEN] = {initCmd, initHighByte, initLowByte};

    (void)memset_s(&recvDataInit, sizeof(recvDataInit), 0x0, sizeof(recvDataInit));
    (void)memset_s(&aht20I2cData, sizeof(IotI2cData), 0x0, sizeof(IotI2cData));

    aht20I2cData.receiveBuf = recvDataInit;
    aht20I2cData.receiveLen = AHT_REG_ARRAY_INIT_LEN;
    aht20I2cWriteCmdAddrInit.sendBuf = initSendUserCmd;
    aht20I2cWriteCmdAddrInit.sendLen = AHT_SNED_CMD_LEN;

    status = IoTI2cRead(IOT_I2C_IDX_0, (AHT_DEVICE_ADDR << IOT_BIT_1) | AHT_READ_COMMAND,
        aht20I2cData.receiveBuf, aht20I2cData.receiveLen);
    if (((recvDataInit[0] != AHT_DEVICE_CALIBRATION_ERR) && (recvDataInit[0] != AHT_DEVICE_CALIBRATION_ERR_R)) ||
        (recvDataInit[0] == AHT_DEVICE_CALIBRATION)) {
        status = IoTI2cWrite(IOT_I2C_IDX_0, (AHT_DEVICE_ADDR << IOT_BIT_1) | AHT_WRITE_COMMAND,
            aht20I2cWriteCmdAddrInit.sendBuf, aht20I2cWriteCmdAddrInit.sendLen);
        TaskMsleep(AHT_SLEEP_1S);
        return IOT_FAILURE;
    }

    return IOT_SUCCESS;
}

/* 发送触犯测量命令 */
unsigned int Aht20Write(unsigned char triggerCmd, unsigned char highByteCmd, unsigned char lowByteCmd)
{
    unsigned int status = 0;

    IotI2cData aht20I2cWriteCmdAddr = {0};
    unsigned char sendUserCmd[AHT_SNED_CMD_LEN] = {triggerCmd, highByteCmd, lowByteCmd};

    aht20I2cWriteCmdAddr.sendBuf = sendUserCmd;
    aht20I2cWriteCmdAddr.sendLen = AHT_SNED_CMD_LEN;

    status = IoTI2cWrite(IOT_I2C_IDX_0, (AHT_DEVICE_ADDR << IOT_BIT_1) | AHT_WRITE_COMMAND,
        aht20I2cWriteCmdAddr.sendBuf, aht20I2cWriteCmdAddr.sendLen);

    return IOT_SUCCESS;
}

/* 读取 aht20 serson 数据 */
unsigned int Aht20Read(unsigned int recvLen, unsigned char type)
{
    unsigned int status = 0;

    unsigned char recvData[AHT_REG_ARRAY_LEN] = { 0 };
    IotI2cData aht20I2cData = { 0 };
    float temper = 0;
    float temperT = 0;
    float humi = 0;
    float humiH = 0;
    /* Request memory space */
    (void)memset_s(&recvData, sizeof(recvData), 0x0, sizeof(recvData));
    (void)memset_s(&aht20I2cData, sizeof(IotI2cData), 0x0, sizeof(IotI2cData));
    aht20I2cData.receiveBuf = recvData;
    aht20I2cData.receiveLen = recvLen;
    
    status = IoTI2cRead(IOT_I2C_IDX_0, (AHT_DEVICE_ADDR << IOT_BIT_1) | AHT_READ_COMMAND,
                        aht20I2cData.receiveBuf, aht20I2cData.receiveLen);
    if (type == AHT_TEMPERATURE) {
        temper = (float)(((recvData[BUFF_BIT_3] & LOW_4_BIT) << IOT_BIT_16) |
            (recvData[BUFF_BIT_4] << IOT_BIT_8) |
            recvData[BUFF_BIT_5]); // 温度拼接
        temperT = (temper / AHT_CALCULATION) * CONSTER_200 - CONSTER_50;  // T = (S_t/2^20)*200-50
        sensorV.g_ahtTemper = temperT;
        return IOT_SUCCESS;
    }
    if (type == AHT_HUMIDITY) {
        humi = (float)((recvData[BUFF_BIT_1] << IOT_BIT_12 | recvData[BUFF_BIT_2] << IOT_BIT_4) |
            ((recvData[BUFF_BIT_3] & HIGH_4_BIT) >> IOT_BIT_4)); // 湿度拼接
        humiH = humi / AHT_CALCULATION * CONSTER_100;
        sensorV.g_ahtHumi = humiH;
        return IOT_SUCCESS;
    }
}

void *AppDemoAht20(char *param)
{
    unsigned int status = 0;
    unsigned int ret = 0;

    /* init oled i2c */
    IoTGpioInit(HI_GPIO_13); /* GPIO13 */
    IoSetFunc(HI_GPIO_13, HI_I2C_SDA_SCL); /* GPIO13,  SDA */
    IoTGpioInit(HI_GPIO_14); /* GPIO 14 */
    IoSetFunc(HI_GPIO_14, HI_I2C_SDA_SCL); /* GPIO14  SCL */

    IoTI2cInit(IOT_I2C_IDX_0, BAUDRATE_INIT); /* baudrate: 400000 */
    /* 上电等待40ms */
    hi_udelay(AHT_DELAY_40MS); // 40ms
    while (1) {
        if (ret == IOT_SUCCESS) {
        /* check whethe the sensor  calibration */
            while (IOT_SUCCESS != Ath20CheckAndInit(AHT_DEVICE_INIT_CMD,
                AHT_DEVICE_PARAM_INIT_HIGH, AHT_DEVICE_PARAM_LOW_BYTE)) {
                //printf("aht20 sensor check init failed!\r\n");
                TaskMsleep(AHT_SLEEP_50MS);
            }
            /* on hold master mode */
            status = Aht20Write(AHT_DEVICE_TEST_CMD, AHT_DEVICE_PARAM_HIGH_BYTE,
                                AHT_DEVICE_PARAM_LOW_BYTE); // tempwerature
            if (status != IOT_SUCCESS) {
                printf("get tempwerature data error!\r\n");
            }
            hi_udelay(AHT_DELAY_100MS); // 100ms等待测量完成
            status = Aht20Read(AHT_REG_ARRAY_LEN, AHT_TEMPERATURE);
            if (status != IOT_SUCCESS) {
                printf("get tempwerature data error!\r\n");
            }
            status = Aht20Read(AHT_REG_ARRAY_LEN, AHT_HUMIDITY);
            if (status != IOT_SUCCESS) {
                printf("get humidity data error!\r\n");
            }
        }
        TaskMsleep(AHT_TASK_SLEEP_TIME); // 20ms
    }
}

/* get aht20 sensor data */
void GetAht20SensorData(void)
{
    unsigned int status = 0;

    /* on hold master mode */
    status = Aht20Write(AHT_DEVICE_TEST_CMD, AHT_DEVICE_PARAM_HIGH_BYTE,
                        AHT_DEVICE_PARAM_LOW_BYTE); // tempwerature
    hi_udelay(AHT_DELAY_100MS); // 100ms等待测量完成
    status = Aht20Read(AHT_REG_ARRAY_LEN, AHT_TEMPERATURE);
    status = Aht20Read(AHT_REG_ARRAY_LEN, AHT_HUMIDITY);
}

static void StartAht20Task(void)
{
    osThreadAttr_t attr = {0};

    attr.stack_size = AHT20_DEMO_TASK_STAK_SIZE;
    attr.priority = AHT20_DEMO_TASK_PRIORITY;
    attr.name = (hi_char*)"app_demo_aht20_task";

    if (osThreadNew((osThreadFunc_t)AppDemoAht20, NULL, &attr) == NULL) {
        printf("[LedExample] Falied to create app_demo_aht20!\n");
    }
}

SYS_RUN(StartAht20Task);