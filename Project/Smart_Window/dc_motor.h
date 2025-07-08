#ifndef BSP_DC_MOTOR_H
#define BSP_DC_MOTOR_H

#include "cmsis_os2.h"
#include "hi_io.h"
#include "hi_gpio.h"

//管脚定义
#define DC_MOTOR_PIN         HI_IO_NAME_GPIO_14
#define DC_MOTOR_GPIO_FUN    HI_IO_FUNC_GPIO_14_GPIO

#define DC_MOTOR(a)          hi_gpio_set_ouput_val(DC_MOTOR_PIN,a)

//函数声明
void dc_motor_init(void);


#endif

