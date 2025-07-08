#include "dc_motor.h"


//直流电机初始化
void dc_motor_init(void)
{
    hi_gpio_init();                                            // GPIO初始化
    hi_io_set_pull(DC_MOTOR_PIN, HI_IO_PULL_DOWN);             // 设置GPIO下拉
    hi_io_set_func(DC_MOTOR_PIN, DC_MOTOR_GPIO_FUN);           // 设置IO为GPIO功能
    hi_gpio_set_dir(DC_MOTOR_PIN, HI_GPIO_DIR_OUT);            // 设置GPIO为输出模式
}


