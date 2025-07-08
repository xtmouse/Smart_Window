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

#include <stdio.h>
#include <unistd.h>
#include <iot_pwm.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_i2c.h"
#include "app_demo_multi_sample.h"
#include "app_demo_mq2.h"
#include "app_demo_i2c_oled.h"
#include "app_demo_aht20.h"
#include "app_demo_config.h"
#include "app_demo_environment.h"

/* temperature menu display */
void ShowTemperatureValue(void)
{
    static unsigned short currentMode = 0;
    unsigned short temperatureStr[6] = {0};
    IoTI2cInit(0, HI_I2C_IDX_BAUDRATE); /* baudrate: 400000 */
    IoTI2cSetBaudrate(0, HI_I2C_IDX_BAUDRATE);

    currentMode = GetKeyStatus(CURRENT_MODE);

    while (1) {
        hi_udelay(DELAY_10_MS); // delay 10ms
        GetAht20SensorData();
        (void*)FlaotToString(GetAhtSensorValue(AHT_TEMPERATURE), temperatureStr);
        OledShowStr(OLED_X_POSITION_40, OLED_Y_POSITION_5,
                    temperatureStr, OLED_DISPLAY_STRING_TYPE_1); /* 40, 5, x.xx, 1 */
        if (currentMode != GetKeyStatus(CURRENT_MODE)) {
            currentMode = GetKeyStatus(CURRENT_MODE);
            break;
        }
        TaskMsleep(SLEEP_10_MS); // 10ms
    }
}

/* humidity value display */
void ShowHumidityValue(void)
{
    static unsigned short currentMode = 0;
    unsigned short humidityStr[6] = {0};
    IoTI2cInit(0, HI_I2C_IDX_BAUDRATE); /* baudrate: 400000 */
    IoTI2cSetBaudrate(0, HI_I2C_IDX_BAUDRATE);

    currentMode = GetKeyStatus(CURRENT_MODE);
    while (1) {
        hi_udelay(DELAY_10_MS); // delay 10ms
        GetAht20SensorData();
        (void*)FlaotToString(GetAhtSensorValue(AHT_HUMIDITY), humidityStr);
        OledShowStr(OLED_X_POSITION_56, OLED_Y_POSITION_5,
                    humidityStr,        OLED_DISPLAY_STRING_TYPE_1); /* 56, 5, x.xx, 1 */
        if (currentMode != GetKeyStatus(CURRENT_MODE)) {
            break;
        }
        TaskMsleep(SLEEP_10_MS); // 10ms
    }
}

/* combustible gas value display */
void ShowCombustibleGasValue(void)
{
    unsigned short combustibleGasValueStr[10] = {0};
    unsigned short currentMode = 0;
    IoTI2cInit(0, HI_I2C_IDX_BAUDRATE); /* baudrate: 400000 */
    IoTI2cSetBaudrate(0, HI_I2C_IDX_BAUDRATE);
    currentMode = GetKeyStatus(CURRENT_MODE);

    while (1) {
        SetCombuSensorValue();
        Mq2GetData();
        (void*)FlaotToString(GetCombuSensorValue(), combustibleGasValueStr);
        if (!GetCombuSensorValue()) {
            OledShowStr(OLED_X_POSITION_60, OLED_Y_POSITION_5,
                        "0.00    ", OLED_DISPLAY_STRING_TYPE_1); /* 60, 5, x.xx, 1 */
        } else {
            OledShowStr(OLED_X_POSITION_60, OLED_Y_POSITION_5,
                        combustibleGasValueStr, OLED_DISPLAY_STRING_TYPE_1); /* 60, 5, x.xx, 1 */
        }
        if (currentMode != GetKeyStatus(CURRENT_MODE)) {
            currentMode = GetKeyStatus(CURRENT_MODE);
            break;
        }
        TaskMsleep(SLEEP_10_MS); // 10ms
    }
}

/* environment function hamdle and display */
void EnvironmentFunc(void)
{
    /* 初始化时屏幕 i2c baudrate setting */
    IoTI2cInit(0, HI_I2C_IDX_BAUDRATE); /* baudrate: 400kbps */
    IoTI2cSetBaudrate(0, HI_I2C_IDX_BAUDRATE); /* 0, 400kbps */
    /* init oled i2c */
    IoTGpioInit(HI_GPIO_13); /* GPIO13 */
    IoSetFunc(HI_GPIO_13, HI_I2C_SDA_SCL); /* GPIO13,  SDA */
    IoTGpioInit(HI_GPIO_14); /* GPIO 14 */
    IoSetFunc(HI_GPIO_14, HI_I2C_SDA_SCL); /* GPIO14  SCL */
    EnvironmentDisplay();
}