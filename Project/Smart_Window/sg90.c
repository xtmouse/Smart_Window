#include "sg90.h"
#include <unistd.h>
#include "hi_time.h"


//SG90初始化
void sg90_init(void)
{
    hi_gpio_init();                                 // GPIO初始化
    hi_io_set_func(SG90_PIN, SG90_GPIO_FUN);       // 设置IO为GPIO功能
    hi_gpio_set_dir(SG90_PIN, HI_GPIO_DIR_OUT);    // 设置GPIO为输出模式
}

//设置SG90角度
void set_sg90_angle(uint16_t angle)
{
    uint8_t i=20;
    uint16_t time=0;

    time=(angle-0)*(2500-500)/(180-0)+500;

    while(i--)
    {
        IoTGpioSetOutputVal(SG90_PIN,HI_GPIO_VALUE1);
        hi_udelay(time);
        IoTGpioSetOutputVal(SG90_PIN,HI_GPIO_VALUE0);
        hi_udelay(20000-time);
    }
}


