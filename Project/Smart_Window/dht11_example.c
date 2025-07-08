#include <stdio.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"
#include "dht11.h"

static void DHT11_Task(void)
{
    u8 ledflag=0;
    u8 temperature=0;  	    
	u8 humidity=0;  
    
    while(DHT11_Init())	//DHT11初始化	
	{
		printf("DHT11 Init Error!!\r\n");
 		usleep(100000);
	}		
    printf("DHT11 Init Successful!!");
    while (1)
    {
       if( DHT11_Read_Data(&temperature,&humidity)==0)	//读取温湿度值
        {   
          if((temperature!= 0)||(humidity!=0))
          {
             ledflag++;
             printf("Temperature = %d\r\n",temperature);
             printf("Humidity = %d\r\n",humidity);
          }
        }
        //延时100ms
        usleep(500000);
    }
}

static void DHT11ExampleEntry(void)
{
    osThreadAttr_t attr;

    attr.name = "DHT11_Task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024;
    attr.priority = 25;

    if (osThreadNew((osThreadFunc_t)DHT11_Task, NULL, &attr) == NULL)
    {
        printf("Falied to create DHT11_Task!\n");
    }
}




APP_FEATURE_INIT(DHT11ExampleEntry);