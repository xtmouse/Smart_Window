/*
 * Copyright (c) 2022 HiSilicon (Shanghai) Technologies CO., LIMITED.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "iot_gpio_ex.h"
#include "iot_watchdog.h"
#include "iot_pwm.h"

#define LED_INTERVAL_TIME_US 300000
#define LED_TASK_STACK_SIZE 512
#define LED_TASK_PRIO 25

// 定义方向引脚
#define DIR_PIN         11  // 方向控制引脚

static int g_iState = 0;
#define IOT_PWM_PORT_PWM0   0
#define IOT_PWM_BEEP        7
#define IOT_GPIO_KEY        5

// 定义电机状态枚举
enum MotorState {
    MOTOR_STOP = 0,     // 停止状态
    MOTOR_CW,           // 顺时针旋转
    MOTOR_CCW           // 逆时针旋转
};

// 全局状态变量
static enum MotorState g_motorState = MOTOR_STOP;

// PWM控制任务
static void *PWMBeepTask(const char *arg)
{
    (void)arg;
    
    // 初始化方向引脚
    IoTGpioInit(DIR_PIN);
    IoSetFunc(DIR_PIN, 0);      // 设置为GPIO功能
    IoTGpioSetDir(DIR_PIN, IOT_GPIO_DIR_OUT);
    IoSetPull(DIR_PIN, IOT_IO_PULL_NONE);
    
    while (1) {
        switch(g_motorState) {
            case MOTOR_CW:   // 顺时针旋转
                IoTGpioSetOutputVal(DIR_PIN, 1); // 设置方向为顺时针
                IoTPwmStart(IOT_PWM_PORT_PWM0, 50, 4000); // 启动PWM
                break;
                
            case MOTOR_CCW:  // 逆时针旋转
                IoTGpioSetOutputVal(DIR_PIN, 0); // 设置方向为逆时针
                IoTPwmStart(IOT_PWM_PORT_PWM0, 50, 4000); // 启动PWM
                break;
                
            case MOTOR_STOP: // 停止
            default:
                IoTPwmStop(IOT_PWM_PORT_PWM0); // 停止PWM
                break;
        }
        
        // 短暂延迟，减少CPU占用
        usleep(100000); // 100ms
    }
}

// 按钮按下处理函数 - 切换电机状态
static void OnButtonPressed(char *arg)
{
    (void)arg;

    // 状态切换：停止 -> 顺时针 -> 逆时针 -> 停止...
    switch (g_motorState) {
        case MOTOR_STOP:
            g_motorState = MOTOR_CW;
            printf("Motor started: CLOCKWISE\n");
            break;
            
        case MOTOR_CW:
            g_motorState = MOTOR_CCW;
            printf("Motor changed: COUNTER-CLOCKWISE\n");
            break;
            
        case MOTOR_CCW:
            g_motorState = MOTOR_STOP;
            printf("Motor STOPPED\n");
            break;
            
        default:
            g_motorState = MOTOR_STOP;
            printf("Motor STOPPED (default)\n");
            break;
    }
}

// 主初始化函数
static void StartPWMBeepTask(void)
{
    osThreadAttr_t attr;

    // 初始化控制按钮
    IoTGpioInit(IOT_GPIO_KEY);
    IoSetFunc(IOT_GPIO_KEY, 0);
    IoTGpioSetDir(IOT_GPIO_KEY, IOT_GPIO_DIR_IN);
    IoSetPull(IOT_GPIO_KEY, IOT_IO_PULL_UP);
    IoTGpioRegisterIsrFunc(IOT_GPIO_KEY, IOT_INT_TYPE_EDGE, 
                          IOT_GPIO_EDGE_FALL_LEVEL_LOW, 
                          (GpioIsrCallbackFunc)OnButtonPressed, NULL);

    // 初始化PWM引脚
    IoTGpioInit(IOT_PWM_BEEP);
    IoSetFunc(IOT_PWM_BEEP, 5); /* 设置IO7的功能为PWM */
    IoTGpioSetDir(IOT_PWM_BEEP, IOT_GPIO_DIR_OUT);
    IoTPwmInit(IOT_PWM_PORT_PWM0);
    
    // 关闭看门狗（调试期间）
    IoTWatchDogDisable();

    // 创建PWM控制任务
    attr.name = "PWMBeepTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)PWMBeepTask, NULL, &attr) == NULL) {
        printf("[StartPWMBeepTask] Failed to create PWMBeepTask!\n");
    } else {
        printf("Stepper motor control started. Initial state: STOP\n");
        printf("Press button to cycle through states: CW -> CCW -> STOP\n");
    }
}

APP_FEATURE_INIT(StartPWMBeepTask);