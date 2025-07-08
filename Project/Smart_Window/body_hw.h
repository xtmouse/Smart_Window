#ifndef __BODY_HW_H
#define __BODY_HW_H

#include <stdio.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "iot_gpio_ex.h"

/***************根据自己需求更改****************/
// BODY_HW GPIO宏定义
#define BODY_HW_GPIO_ID         1
#define BODY_HW_GPIO_FUNC       IOT_IO_FUNC_GPIO_1_GPIO  // GPIO1功能

/*********************END**********************/

void BODY_HW_Init(void);
uint8_t BODY_HW_GetData(void);

#endif /* __BODY_HW_H */