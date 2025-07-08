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

#ifndef APP_AHT20_H
#define APP_AHT20_H

#define AHT_SLEEP_20MS  (20) // 20ms
#define AHT_SLEEP_50MS  (50) // 5ms
#define AHT_SLEEP_1S (1000) // 1s
#define AHT_DELAY_10MS  (10000) // 10ms
#define AHT_DELAY_40MS  (40000) // 40ms
#define AHT_DELAY_100MS  (100000) // 100ms
#define AHT_DEVICE_ADDR  (0x38) // device addr
#define AHT_DEVICE_READ_STATUS (0x71) // befor read tem&humi data need to send cmd to comfir the
#define AHT_DEVICE_INIT_CMD  (0xBE) // aht init cmd
#define AHT_DEVICE_TEST_CMD  (0xAC) // test cmd
#define AHT_DEVICE_PARAM_HIGH_BYTE  (0x33)
#define AHT_DEVICE_PARAM_LOW_BYTE   (0x00)
#define AHT_DEVICE_PARAM_INIT_HIGH  (0x08)
#define AHT_DEVICE_CALIBRATION  (0x80)
#define AHT_DEVICE_CALIBRATION_ERR  (0x1C)
#define AHT_DEVICE_CALIBRATION_ERR_R (0x18)
#define AHT_TASK_SLEEP_TIME  (200) // thread sleep 20ms
#define BAUDRATE_INIT   (400000)

typedef enum {
    AHT_TEMPERATURE = 1,
    AHT_HUMIDITY    = 2,
}AhtSersonType;

typedef enum {
    HI_I2C_SDA_SCL = 6,
} HiI2cConfig;

typedef struct {
    float  g_ahtTemper;
    float  g_ahtHumi;
} AhtSersonValue;

float GetAhtSensorValue(AhtSersonType type);
void GetAht20SensorData(void);

#endif