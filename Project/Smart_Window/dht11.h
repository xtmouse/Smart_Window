#ifndef _DHT11_H__
#define _DHT11_H__
#include "iot_gpio.h"
#include "iot_gpio_ex.h"

#define u8 unsigned char
#define u16 unsigned short
#define u32 unsigned int
 //IO方向设置
#define DHT11_GPIO  IOT_IO_NAME_GPIO_13
// #define DHT11_IO_IN()  	GpioSetDir(DHT11_GPIO, WIFI_IOT_GPIO_DIR_IN)
// #define DHT11_IO_OUT()  GpioSetDir(DHT11_GPIO, WIFI_IOT_GPIO_DIR_OUT)
////IO操作函数											   
#define	DHT11_DQ_OUT_High IoTGpioSetOutputVal(DHT11_GPIO, 1); //设置GPIO输出高电平
#define	DHT11_DQ_OUT_Low IoTGpioSetOutputVal(DHT11_GPIO, 0); //设置GPIO输出低电平   

u8 DHT11_Read_Byte(void);//读出一个字节
u8 DHT11_Read_Bit(void);//读出一个位
u8 DHT11_Check(void);//检测是否存在DHT11
void DHT11_Rst(void);//复位DHT11 

extern u8 DHT11_Init(void);//初始化DHT11
extern u8 DHT11_Read_Data(u8 *temp,u8 *humi);//读取温湿度
#endif