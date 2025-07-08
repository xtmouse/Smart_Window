#ifndef __RAIN_H
#define __RAIN_H

#include <stdio.h>
#include "iot_gpio.h"
#include "iot_gpio_ex.h"
#include "iot_adc.h"
#include "iot_pwm.h"
#include "stdint.h"

#define RAIN_READ_TIMES 5  // 雨滴传感器ADC循环读取次数
#define MODE 1  // 固定使用模拟模式(AO)

#define RAIN_AO_GPIO_PIN 5  // GPIO5
#define RAIN_AO_ADC_CHANNEL IOT_ADC_CHANNEL_5

void RAIN_Init(void);
uint16_t RAIN_GetData(void);

#endif /* __RAIN_H */