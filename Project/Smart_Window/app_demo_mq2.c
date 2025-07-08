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

#include <hi_stdlib.h>
#include <math.h>
#include <hi_time.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_pwm.h"
#include "iot_gpio.h"
#include "ssd1306_oled.h"
#include "iot_adc.h"
#include "iot_gpio_ex.h"
#include "app_demo_multi_sample.h"
#include "app_demo_i2c_oled.h"
#include "app_demo_mq2.h"

#define MQ2_DEMO_TASK_STAK_SIZE    (1024 * 8)
#define MQ2_DEMO_TASK_PRIORITY    (25)
#define ADC_TEST_LENGTH    (20)
#define VLT_MIN    (100)
#define CAL_PPM    (25) // 校准环境中PPM值
#define RL    (1) // RL阻值
#define MQ2_RATIO    (1111)

#define X_CONSTANT  (613.9f)
#define Y_CONSTANT  (-2.074f)
#define X_CONSTANT_2 (11.5428 * 22)
#define Y_CONSTANT_2 (0.6549)
#define VOILTAGE_5_V (5)

#define PPM_THRESHOLD_300 (300)
#define PPM_THRESHOLD_3000 (3000)
#define SAMPLING_TIME (0xff)

#define ADC_RANGE_MAX ((float)4096.0)
#define ADC_VOLTAGE_1_8_V  ((float)1.8)
#define ADC_VOLTAGE_4_TIMES (4)

Mq2SensorDef combGas = {0};
float g_r0 = 22; /* R0 c初始值 */
void SetCombuSensorValue(void)
{
    combGas.g_combustibleGasValue = 0.0;
}

float GetCombuSensorValue(void)
{
    return combGas.g_combustibleGasValue;
}

/*
 *  ppm：为可燃气体的浓度
 *  VRL：电压输出值
 *  Rs：器件在不同气体，不同浓度下的电阻值
 *  R0：器件在洁净空气中的电阻值
 *  RL：负载电阻阻值
 */
void Mq2PpmCalibration(float rS)
{
    g_r0 = rS / pow(CAL_PPM / X_CONSTANT, 1 / Y_CONSTANT);
    printf("R0:%f\r\n", g_r0);
}

/* MQ2传感器数据处理 */
float Mq2GetPpm(float voltage)
{
    float vol = voltage;
    double ppm = 0;
    //static unsigned char flag = HI_TRUE;
    static unsigned char count = 0;

    float VolDif = (VOILTAGE_5_V - vol);
    float SeekModule = VolDif / vol;
    float rS = SeekModule * RL; /* 计算 RS值 */
    (void)memset_s(&ppm, sizeof(ppm), 0x0, sizeof(ppm));

    ppm = pow(X_CONSTANT_2 * vol / (VOILTAGE_5_V - vol), 1.0 / Y_CONSTANT_2); /* 计算ppm */
    // if (ppm < PPM_THRESHOLD_300) { /* 排除空气中其他气体的干扰 */
    //     ppm = 0;
    // }

    FlaotToString(ppm, combGas.g_ahu20GasBuff);
    return ppm;
}

/* mq2 sesor get data from adc change */
void Mq2GetData(void)
{
    unsigned short data = 0; /* 0 */
    float voltage;
    // ADC_Channal_2(gpio5)  自动识别模式  CNcomment:4次平均算法模式 CNend
    unsigned int ret = AdcRead(IOT_ADC_CHANNEL_2, &data,
                               IOT_ADC_EQU_MODEL_4, IOT_ADC_CUR_BAIS_DEFAULT, SAMPLING_TIME);
    if (ret != HI_ERR_SUCCESS) {
        printf("MQ2 Read Fail!!!Please reset\n");
        return HI_NULL;
    }
    voltage = (float)(data * ADC_VOLTAGE_1_8_V *
        ADC_VOLTAGE_4_TIMES / ADC_RANGE_MAX); /* vlt * 1.8* 4 / 4096.0为将码字转换为电压 */
    printf("voltage: %f!!!\n",voltage);
    combGas.g_combustibleGasValue = Mq2GetPpm(voltage);
    //printf("g_combustibleGasValue is %lf\r\n", combGas.g_combustibleGasValue);
}
