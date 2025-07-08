#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "oled_ssd1306.h"
#include "rain.h"
#include "iot_gpio_ex.h"
#include "iot_errno.h"
#include "iot_adc.h"

#define RAIN_TASK_STACK_SIZE 4096
#define RAIN_TASK_PRIORITY 25

#define ADC_SAMPLING_TIME 0xff
#define ADC_RANGE_MAX 4095.0f
#define ADC_VREF 1.8f


static void RainTask(void *arg)
{
    (void)arg;
    uint16_t value;
    
    printf("Rain sensor demo start!\n");
    
    // 初始化雨滴传感器
    RAIN_Init();
    
    while (1) {  
        // 获取雨量值 (0-100)
        value = RAIN_GetData();  
        
        printf("Rain: %d%%\r\n", value);

        TaskMsleep(1000);
    }
}

void RainDemo(void)
{
    osThreadAttr_t attr = {
        .name = "RainTask",
        .attr_bits = 0U,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .stack_size = RAIN_TASK_STACK_SIZE,
        .priority = RAIN_TASK_PRIORITY,
    };

    if (osThreadNew((osThreadFunc_t)RainTask, NULL, &attr) == NULL) {
        printf("Failed to create rain task!\n");
    }
}

SYS_RUN(RainDemo);