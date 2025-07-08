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
#include "app_demo_environment.h"
#include "app_demo_multi_sample.h"
#include "app_demo_aht20.h"
#include "app_demo_mq2.h"
#include "app_demo_config.h"

static unsigned short g_hi3861BoardLedTest = 0;

#define FLAG_TRUE   (1)
#define FLAG_FAILSE (0)

void ReturnAllModeEnumSample(void)
{
    static unsigned char currentMode = 0;
    currentMode = GetKeyStatus(CURRENT_MODE);
    while (1) {
        if (currentMode != GetKeyStatus(CURRENT_MODE)) {
            currentMode = GetKeyStatus(CURRENT_MODE);
            break;
        }
        TaskMsleep(SLEEP_1_MS);
    }
}

/* environment menu display */
void ShowAllEnvironmentValue(void)
{
    unsigned short combustibleMainMenuGasValueStr[10] = {0};
    unsigned short temperatureStr[6] = {0};
    unsigned short humidityStr[6] = {0};
    static unsigned char currentMode = 0;

    currentMode = GetKeyStatus(CURRENT_MODE);
    IoTI2cInit(0, HI_I2C_IDX_BAUDRATE); /* baudrate: 400000 */
    IoTI2cSetBaudrate(0, HI_I2C_IDX_BAUDRATE);
    while (1) {
        (void)GetAht20SensorData();
        Mq2GetData();
        (void*)FlaotToString(GetAhtSensorValue(AHT_TEMPERATURE), temperatureStr);
        (void*)FlaotToString(GetAhtSensorValue(AHT_HUMIDITY), humidityStr);
        (void*)FlaotToString(GetCombuSensorValue(), combustibleMainMenuGasValueStr);

        OledShowStr(OLED_X_POSITION_18, OLED_Y_POSITION_5,
                    temperatureStr, OLED_DISPLAY_STRING_TYPE_1); /* 18, 5, xx, 1 */

        OledShowStr(OLED_X_POSITION_81, OLED_Y_POSITION_5,
                    humidityStr, OLED_DISPLAY_STRING_TYPE_1); /* 18, 5, xx, 1 */
        
        if (!GetCombuSensorValue()) {
            OledShowStr(OLED_X_POSITION_48, OLED_Y_POSITION_6,
                        "0.00    ", OLED_DISPLAY_STRING_TYPE_1); /* 48, 6, x.xx, 1 */
        } else {
            OledShowStr(OLED_X_POSITION_48, OLED_Y_POSITION_6,
                        combustibleMainMenuGasValueStr, OLED_DISPLAY_STRING_TYPE_1); /* 48, 6, x.xx, 1 */
        }
        if (currentMode != GetKeyStatus(CURRENT_MODE)) {
            currentMode = GetKeyStatus(CURRENT_MODE);
            break;
        }
        TaskMsleep(SLEEP_100_MS); // 10ms
    }
}

void EnvironmentAllMode(void)
{
    OledFillScreen(OLED_CLEAN_SCREEN); // clean screen
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_0,
                "WiFi-AP  ON  U:1", OLED_DISPLAY_STRING_TYPE_1); /* 0, 0, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_1,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 1, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_2,
                "  Environment   ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 2, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_3,
                "  Monitoring    ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 3, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_4,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "T:    C H:    % ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 5, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_6,
                "C_Gas:00        ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 6, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_7,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 7, x.xx, 1 */
    ShowAllEnvironmentValue();
}

void EnvironmentTemperatureMode(void)
{
    OledFillScreen(OLED_CLEAN_SCREEN); // clean screen
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_0,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 0, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_1,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 1, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_2,
                "  Temperature   ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 2, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_3,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 3, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_4,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "   T:    C      ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 5, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_6,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 6, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_7,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 7, x.xx, 1 */
    ShowTemperatureValue();
}

void EnvironmentHumidityMode(void)
{
    OledFillScreen(OLED_CLEAN_SCREEN); // clean screen
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_0,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 0, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_1,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 1, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_2,
                "    Humidity    ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 2, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_3,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 3, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_4,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "     H:    %    ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 5, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_6,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 6, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_7,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 7, x.xx, 1 */
    ShowHumidityValue();
}

void EnvironmentCombustibleGasMode(void)
{
    OledFillScreen(OLED_CLEAN_SCREEN); // clean screen
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_0,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 0, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_1,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 1, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_2,
                "     C_Gas      ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 2, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_3,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 3, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_4,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "  C_Gas:        ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 5, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_6,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 6, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_7,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 7, x.xx, 1 */
    ShowCombustibleGasValue();
}

void EnvironmentReturnMode(void)
{
    OledFillScreen(OLED_CLEAN_SCREEN); // clean screen
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_0,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 0, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_1,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 1, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_2,
                "Return Menu     ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 2, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_3,
                "  Environment   ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 3, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_4,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 4, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_5,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 5, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_6,
                "                ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 6, x.xx, 1 */
    OledShowStr(OLED_X_POSITION_0, OLED_Y_POSITION_7,
                "Continue        ", OLED_DISPLAY_STRING_TYPE_1); /* 0, 7, x.xx, 1 */
    ReturnAllModeEnumSample();
}

void EnvironmentDisplay(void)
{
    while (HI_ERR_SUCCESS != OledInit()) {
        if (g_hi3861BoardLedTest == FLAG_FAILSE) {
            g_hi3861BoardLedTest = FLAG_TRUE;
             /* test HiSpark board */
            FACTORY_HISPARK_BOARD_TEST("-----------HiSpark board check----------");
        }
        TaskMsleep(SLEEP_1S);
    }
    /* 按键中断初始化 */
    TestGpioInit();
    while (1) {
        switch (GetKeyStatus(CURRENT_MODE)) {
            case ENV_ALL_MODE:
                EnvironmentAllMode();
                break;
            case ENV_TEMPERRATURE_MODE:
                EnvironmentTemperatureMode();
                break;
            case ENV_HUMIDITY_MODE:
                EnvironmentHumidityMode();
                break;
            case COMBUSTIBLE_GAS_MODE:
                EnvironmentCombustibleGasMode();
                break;
            case ENV_RETURN_MODE:
                EnvironmentReturnMode();
                break;
            default:
                break;
        }
    }
}
