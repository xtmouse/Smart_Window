#ifndef BSP_SG90_H
#define BSP_SG90_H

#include "cmsis_os2.h"
#include "hi_io.h"
#include "hi_gpio.h"


//管脚定义
#define SG90_PIN         HI_IO_NAME_GPIO_14
#define SG90_GPIO_FUN    HI_IO_FUNC_GPIO_14_GPIO


//函数声明
void sg90_init(void);
void set_sg90_angle(uint16_t angle);

#endif

