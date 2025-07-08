#include "body_hw.h"
#include "iot_gpio.h"
#include "iot_gpio_ex.h"

void BODY_HW_Init(void)
{
    /* 初始化GPIO */
    IoTGpioInit(BODY_HW_GPIO_ID);
    
    /* 设置GPIO功能 */
    IoSetFunc(BODY_HW_GPIO_ID, BODY_HW_GPIO_FUNC);
    
    /* 设置为输入模式 */
    IoTGpioSetDir(BODY_HW_GPIO_ID, IOT_GPIO_DIR_IN);
    
    /* 设置为下拉输入（防止悬空状态） */
    IoSetPull(BODY_HW_GPIO_ID, IOT_IO_PULL_DOWN);
    
    printf("[BODY_HW] Sensor init success. GPIO: %d\n", BODY_HW_GPIO_ID);
}

uint8_t BODY_HW_GetData(void)
{
    unsigned int value = 0;
    
    // 双参数调用（兼容两种SDK版本）
    if (IoTGpioGetInputVal(BODY_HW_GPIO_ID, &value) != 0) {
        printf("[ERROR] Read GPIO %d failed!\n", BODY_HW_GPIO_ID);
        return 0;
    }
    
    return (uint8_t)value;
}