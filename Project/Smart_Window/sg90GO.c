#include <stdio.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "sg90.h"

//控制任务
osThreadId_t SG90_Task_ID; //任务ID

void SG90_Task(void)
{
    int16_t ang=0;
    uint8_t dir=0;

    sg90_init();

    while (1) 
    {
        if(dir==0)
        {
            ang+=10;
            if(ang>180)dir=1;
        }
        else
        {
            ang-=10;
            if(ang<0)dir=0;            
        }
        set_sg90_angle(ang);
        usleep(200*1000); //20ms
    }
}
//任务创建
void sg90_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "sg90Task";       // 任务的名字
    taskOptions.attr_bits = 0;               // 属性位
    taskOptions.cb_mem = NULL;               // 堆空间地址
    taskOptions.cb_size = 0;                 // 堆空间大小
    taskOptions.stack_mem = NULL;            // 栈空间地址
    taskOptions.stack_size = 1024;           // 栈空间大小 单位:字节
    taskOptions.priority = osPriorityNormal1; // 任务的优先级

    SG90_Task_ID = osThreadNew((osThreadFunc_t)SG90_Task, NULL, &taskOptions); // 创建任务
    if (SG90_Task_ID != NULL)
    {
        printf("ID = %d, SG90_Task_ID Create OK!\n", SG90_Task_ID);
    }
}

/**
 * @description: 初始化并创建任务
 * @param {*}
 * @return {*}
 */
static void template_demo(void)
{
    printf("-Hi3861开发板--SG90舵机实验\r\n");
    //led_task_create();
    sg90_task_create();//任务创建
}
SYS_RUN(template_demo);

