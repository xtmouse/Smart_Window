#include <stdio.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "dc_motor.h"


//控制任务
osThreadId_t DC_MOTOR_Task_ID; //任务ID

void DC_MOTOR_Task(void)
{
    dc_motor_init();//直流电机初始化
    printf("init successfully!!!\n");

    while(1){
        DC_MOTOR(1);
        sleep(3);
        DC_MOTOR(0);
        printf("swag!!!\n");
    }


    // while (1) 
    // {
    //     usleep(10*1000);
    // }
}
//任务创建
void dc_motor_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "dc_motorTask";       // 任务的名字
    taskOptions.attr_bits = 0;               // 属性位
    taskOptions.cb_mem = NULL;               // 堆空间地址
    taskOptions.cb_size = 0;                 // 堆空间大小
    taskOptions.stack_mem = NULL;            // 栈空间地址
    taskOptions.stack_size = 1024;           // 栈空间大小 单位:字节
    taskOptions.priority = osPriorityNormal; // 任务的优先级

    DC_MOTOR_Task_ID = osThreadNew((osThreadFunc_t)DC_MOTOR_Task, NULL, &taskOptions); // 创建任务1
    if (DC_MOTOR_Task_ID != NULL)
    {
        printf("ID = %d, Task Create OK!\n", DC_MOTOR_Task_ID);
    }
}

/**
 * @description: 初始化并创建任务
 * @param {*}
 * @return {*}
 */
static void template_demo(void)
{
    printf("-Hi3861开发板--直流电机实验\r\n");

    dc_motor_task_create();//任务创建
}
SYS_RUN(template_demo);

