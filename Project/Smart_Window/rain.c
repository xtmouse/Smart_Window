#include "rain.h"
#include "iot_errno.h"
#include "iot_adc.h"

#define ADC_SAMPLING_TIME 0xff
#define ADC_RANGE_MAX 4095.0f
#define ADC_VREF 1.8f

// 1. 在RAIN_GetData之前声明RAIN_ADC_Read函数
static uint16_t RAIN_ADC_Read(void);

void RAIN_Init(void)
{
    // 初始化ADC通道
    //IoTAdcInit(RAIN_AO_ADC_CHANNEL, ADC_VREF * 1000); // 输入参考电压(mV)
    
    // 配置GPIO功能为ADC - 使用IoT GPIO函数
    // IoTGpioInit(RAIN_AO_GPIO_PIN);
    // IoSetFunc(RAIN_AO_GPIO_PIN, IOT_IO_FUNC_GPIO_5_GPIO); // 设置GPIO5功能复用
    // IoTGpioSetDir(RAIN_AO_GPIO_PIN, IOT_GPIO_DIR_IN); // 设置为输入模式
    // IoSetPull(RAIN_AO_GPIO_PIN, IOT_IO_PULL_DOWN); // 设置下拉
}

uint16_t RAIN_GetData(void)
{
    uint32_t tempData = 0;
    
    for (uint8_t i = 0; i < RAIN_READ_TIMES; i++) {
        tempData += RAIN_ADC_Read();
        TaskMsleep(50);
    }
    
    tempData /= RAIN_READ_TIMES;
    return (uint16_t)tempData;
}

uint16_t RAIN_ADC_Read(void)
{
    unsigned short data = 0;
    unsigned int ret = AdcRead(RAIN_AO_ADC_CHANNEL, &data,
                                 IOT_ADC_EQU_MODEL_4, 
                                 IOT_ADC_CUR_BAIS_DEFAULT, 
                                 ADC_SAMPLING_TIME);
    
    if (ret != IOT_SUCCESS) {
        printf("[Rain] ADC read error: %d\n", ret);
        return 0;
    }
    
    // 计算实际电压（考虑分压电路）
    float voltage = (float)data * ADC_VREF / ADC_RANGE_MAX;
    //printf("Raw ADC: %d, Voltage: %.2fV\n", data, voltage * 3.0f);
    // 转换为雨量百分比 (0-100%)
    // 注意：实际转换公式需要根据传感器校准
    uint16_t rain_value = 100 - (data * 100) / 4095;
    
    return rain_value;
}