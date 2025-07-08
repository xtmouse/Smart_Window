#include "body_hw.h"
#include "ohos_init.h"
#include "cmsis_os2.h"

static void BodyHwExampleTask(void *arg)
{
    (void)arg;
    uint8_t pirState = 0;
    
    /* 初始化人体红外传感器 */
    BODY_HW_Init();
    
    while (1) {
        /* 获取传感器状态 */
        pirState = BODY_HW_GetData();
        
        if (pirState == 1) {
            printf("[ALERT] Human body detected!\n");
            // 此处添加触发动作（如点亮LED）
        } else {
            // 无人体活动
            printf("no body!!\n");
        }
        
        /* 延时500ms */
        usleep(500 * 1000);
    }
}

void BodyHwExample(void)
{
    osThreadAttr_t attr = {
        .name = "BodyHwTask",
        .stack_size = 2048,
        .priority = osPriorityNormal,
    };

    osThreadNew(BodyHwExampleTask, NULL, &attr);
}

APP_FEATURE_INIT(BodyHwExample);