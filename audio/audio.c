/*
 * Copyright (c) 2022 HiSilicon (Shanghai) Technologies CO., LIMITED.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include "sample_comm.h"
#include "acodec.h"
#include "audio_aac_adp.h"
#include "audio_dl_adp.h"
#include "hi_resampler_api.h"
#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
#include "hi_vqe_register_api.h"
#endif
#include "sdk.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


static PAYLOAD_TYPE_E gs_enPayloadType = PT_AAC;
static HI_BOOL gs_bAioReSample = HI_FALSE;
static HI_BOOL gs_bUserGetMode = HI_FALSE;
static HI_BOOL gs_bAoVolumeCtrl = HI_FALSE;
static AUDIO_SAMPLE_RATE_E g_in_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
static AUDIO_SAMPLE_RATE_E g_out_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
/* 0: close, 1: record, 2:reserved, 3:talkv2 */
static HI_U32 g_ai_vqe_type = 1;
static HI_BOOL g_sample_audio_exit = HI_FALSE;

static AI_RECORDVQE_CONFIG_S g_ai_record_vqe_attr;
static AI_TALKVQEV2_CONFIG_S g_ai_talkv2_vqe_attr;

/* 新增UDP相关定义 */
#define UDP_DEST_IP "192.168.137.1" // 目标主机IP
#define UDP_DEST_PORT 8888          // 目标端口

#define SAMPLE_DBG(s32Ret)                                                          \
    do {                                                                            \
        printf("s32Ret = %#x, fuc:%s, line:%d\n", s32Ret, __FUNCTION__, __LINE__);  \
    } while (0)

/* function : PT Number to String */
static char *SAMPLE_AUDIO_Pt2Str(PAYLOAD_TYPE_E enType)
{
    if (enType == PT_G711A) {
        return "g711a";
    } else if (enType == PT_G711U) {
        return "g711u";
    } else if (enType == PT_ADPCMA) {
        return "adpcm";
    } else if (enType == PT_G726) {
        return "g726";
    } else if (enType == PT_LPCM) {
        return "pcm";
    } else if (enType == PT_AAC) {
        return "aac";
    } else if (enType == PT_MP3) {
        return "mp3";
    } else {
        return "data";
    }
}

static HI_S32 SAMPLE_AUDIO_Getchar(HI_VOID)
{
    int c;

    if (g_sample_audio_exit == HI_TRUE) {
        SAMPLE_COMM_AUDIO_DestroyAllTrd();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
        return -1;
    }

    c = getchar();

    if (g_sample_audio_exit == HI_TRUE) {
        SAMPLE_COMM_AUDIO_DestroyAllTrd();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
        return -1;
    }

    return c;
}

typedef struct {
    AENC_CHN AeChn;
    int sockfd;
    struct sockaddr_in dest_addr;
    HI_BOOL *pbExitFlag;
} UdpSendThreadArg;

static void *SAMPLE_AUDIO_UdpSendThread(void *arg) {
    UdpSendThreadArg *thread_arg = (UdpSendThreadArg *)arg;
    AUDIO_STREAM_S stStream;
    HI_S32 s32Ret;

    while (*thread_arg->pbExitFlag == HI_FALSE) {

        s32Ret = HI_MPI_AENC_GetStream(thread_arg->AeChn, &stStream, 100);
        if (s32Ret == HI_SUCCESS) {
            if (stStream.pStream && stStream.u32Len > 0) {

                ssize_t bytes_sent = sendto(
                    thread_arg->sockfd, 
                    stStream.pStream, 
                    stStream.u32Len, 
                    0,
                    (const struct sockaddr *)&thread_arg->dest_addr, 
                    sizeof(thread_arg->dest_addr)
                );
                
                if (bytes_sent < 0) {
                    perror("UDP send failed");
                }
            }
            
          
            HI_MPI_AENC_ReleaseStream(thread_arg->AeChn, &stStream);
        } else if (s32Ret != HI_ERR_AENC_NOBUF) {
            usleep(10 * 1000);
        }
    }
    
    close(thread_arg->sockfd);
    printf("UDP send thread exit\n");
    return NULL;
}


/* function : Add dynamic load path */
#ifndef HI_VQE_USE_STATIC_MODULE_REGISTER
static HI_VOID SAMPLE_AUDIO_AddLibPath(HI_VOID)
{
    HI_S32 s32Ret;
    HI_CHAR aszLibPath[FILE_NAME_LEN] = {0};
#if defined(__HuaweiLite__) && (!defined(__OHOS__))
    s32Ret = snprintf_s(aszLibPath, FILE_NAME_LEN, FILE_NAME_LEN - 1, "/sharefs/");
    if (s32Ret <= EOK) {
        printf("\n snprintf_s fail! ret = 0x%x", s32Ret);
        return;
    }
#else
#endif
    s32Ret = Audio_Dlpath(aszLibPath);
    if (s32Ret != HI_SUCCESS) {
        printf("%s: add lib path %s failed\n", __FUNCTION__, aszLibPath);
    }
    return;
}
#endif

static FILE *SAMPLE_AUDIO_OpenAencFile(AENC_CHN AeChn, PAYLOAD_TYPE_E enType)
{
    FILE *pfd = NULL;
    HI_CHAR aszFileName[FILE_NAME_LEN] = {0};
    HI_S32 s32Ret;
    /* create file for save stream */
#if defined(__HuaweiLite__) && (!defined(__OHOS__))
    s32Ret = snprintf_s(aszFileName, FILE_NAME_LEN, FILE_NAME_LEN - 1, "/sharefs/audio_chn%d.%s", AeChn,
        SAMPLE_AUDIO_Pt2Str(enType));
    if (s32Ret <= EOK) {
        printf("\n snprintf_s fail! ret = 0x%x", s32Ret);
        return NULL;
    }
#else
    s32Ret = snprintf_s(aszFileName, FILE_NAME_LEN, FILE_NAME_LEN - 1,
        "audio_chn%d.%s", AeChn, SAMPLE_AUDIO_Pt2Str(enType));
    if (s32Ret <= EOK) {
        printf("\n snprintf_s fail! ret = 0x%x", s32Ret);
        return NULL;
    }
#endif
    pfd = fopen(aszFileName, "w+");
    if (pfd == NULL) {
        printf("%s: open file %s failed\n", __FUNCTION__, aszFileName);
        return NULL;
    }
    printf("open stream file:\"%s\" for aenc ok\n", aszFileName);
    return pfd;
}

static FILE *SAMPLE_AUDIO_OpenAdecFile(ADEC_CHN AdChn, PAYLOAD_TYPE_E enType)
{
    FILE *pfd = NULL;
    HI_CHAR aszFileName[FILE_NAME_LEN] = {0};
    HI_S32 s32Ret;
#if (!defined(__HuaweiLite__)) || defined(__OHOS__)
    HI_CHAR path[PATH_MAX] = {0};
#endif

    /* create file for save stream */
#if defined(__HuaweiLite__) && (!defined(__OHOS__))
    s32Ret = snprintf_s(aszFileName, FILE_NAME_LEN, FILE_NAME_LEN - 1, "/sharefs/audio_chn%d.%s", AdChn,
        SAMPLE_AUDIO_Pt2Str(enType));
    if (s32Ret <= EOK) {
        printf("\n snprintf_s fail! ret = 0x%x", s32Ret);
        return NULL;
    }
#else
    s32Ret = snprintf_s(aszFileName, FILE_NAME_LEN, FILE_NAME_LEN - 1,
        "audio_chn%d.%s", AdChn, SAMPLE_AUDIO_Pt2Str(enType));
    if (s32Ret <= EOK) {
        printf("\n snprintf_s fail! ret = 0x%x", s32Ret);
        return NULL;
    }
#endif

#if (!defined(__HuaweiLite__)) || defined(__OHOS__)
    if (realpath(aszFileName, path) == NULL) {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "adec file name realpath fail");
        return NULL;
    }

    pfd = fopen(path, "rb");
    if (pfd == NULL) {
        printf("%s: open file %s failed\n", __FUNCTION__, aszFileName);
        return NULL;
    }
#else
    pfd = fopen(aszFileName, "rb");
    if (pfd == NULL) {
        printf("%s: open file %s failed\n", __FUNCTION__, aszFileName);
        return NULL;
    }
#endif

    printf("open stream file:\"%s\" for adec ok\n", aszFileName);
    return pfd;
}

static hi_void sample_audio_adec_ao_init_param(AIO_ATTR_S *attr)
{
    attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    attr->u32FrmNum = FPS_30;
    attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    attr->enWorkmode = AIO_MODE_I2S_MASTER;
    attr->enSoundmode = AUDIO_SOUND_MODE_MONO; // AUDIO_SOUND_MODE_STEREO;
    attr->u32ChnCnt = 1; /* 2: chn num */
    attr->u32ClkSel = 1;
    attr->enI2sType = AIO_I2STYPE_INNERCODEC;
    attr->u32PtNumPerFrm = AACLC_SAMPLES_PER_FRAME;
    attr->u32EXFlag = 0;

    gs_bAioReSample = HI_FALSE;
    g_in_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
    g_out_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
}

static HI_VOID SAMPLE_AUDIO_AdecAoInner(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
    HI_S32 s32Ret;
    FILE *pfd = NULL;

    s32Ret = SAMPLE_COMM_AUDIO_AoBindAdec(AoDev, AoChn, AdChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return;
    }

    pfd = SAMPLE_AUDIO_OpenAdecFile(AdChn, gs_enPayloadType);
    if (pfd == NULL) {
        SAMPLE_DBG(HI_FAILURE);
        goto ADECAO_ERR0;
    }

    s32Ret = SAMPLE_COMM_AUDIO_CreateTrdFileAdec(AdChn, pfd);
    if (s32Ret != HI_SUCCESS) {
        (HI_VOID)fclose(pfd);
        pfd = HI_NULL;
        SAMPLE_DBG(s32Ret);
        goto ADECAO_ERR0;
    }

    HI_MPI_AO_SetVolume(AoDev, 0);
    printf("bind adec:%d to ao(%d,%d) ok \n", AdChn, AoDev, AoChn);
    printf("\nplease press twice ENTER to exit this sample\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdFileAdec(AdChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return;
    }

ADECAO_ERR0:
    s32Ret = SAMPLE_COMM_AUDIO_AoUnbindAdec(AoDev, AoChn, AdChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

    return;
}

/* function : file -> Adec -> Ao */
static HI_S32 SAMPLE_AUDIO_AdecAo(HI_VOID)
{
    HI_S32 s32Ret, s32AoChnCnt;
    const AO_CHN AoChn = 0;
    const ADEC_CHN AdChn = 0;
    AIO_ATTR_S stAioAttr;
    AUDIO_DEV AoDev = SAMPLE_AUDIO_INNER_AO_DEV;

    sample_audio_adec_ao_init_param(&stAioAttr);

    s32Ret = SAMPLE_COMM_AUDIO_StartAdec(AdChn, gs_enPayloadType);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto ADECAO_ERR3;
    }

    s32AoChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, s32AoChnCnt, &stAioAttr, g_in_sample_rate, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto ADECAO_ERR2;
    }

    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto ADECAO_ERR1;
    }

    SAMPLE_AUDIO_AdecAoInner(AoDev, AoChn, AdChn);

ADECAO_ERR1:
    s32Ret = SAMPLE_COMM_AUDIO_StopAo(AoDev, s32AoChnCnt, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }
ADECAO_ERR2:
    s32Ret = SAMPLE_COMM_AUDIO_StopAdec(AdChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

ADECAO_ERR3:
    return s32Ret;
}

static hi_void sample_audio_ai_aenc_init_param(AIO_ATTR_S *attr)
{
    attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    attr->enWorkmode = AIO_MODE_I2S_MASTER;
    attr->enSoundmode = AUDIO_SOUND_MODE_MONO; // AUDIO_SOUND_MODE_STEREO;
    attr->u32EXFlag = 0;
    attr->u32FrmNum = FPS_30;
    attr->u32ChnCnt = 1; // 2; /* 2: chn num */
    attr->u32ClkSel = 1;
    attr->enI2sType = AIO_I2STYPE_INNERCODEC;
    attr->u32PtNumPerFrm = AACLC_SAMPLES_PER_FRAME;

    gs_bAioReSample = HI_FALSE;
    g_in_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
    g_out_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
}

static HI_VOID SAMPLE_AUDIO_StartAdecAo(HI_BOOL bSendAdec, AUDIO_DEV AoDev, AENC_CHN AeChn, AIO_ATTR_S *pstAioAttr)
{
    HI_S32 s32Ret, s32AoChnCnt;
    const AO_CHN AoChn = 0;
    const ADEC_CHN AdChn = 0;
    FILE *pfd = NULL;

    if (bSendAdec == HI_TRUE) {
        s32Ret = SAMPLE_COMM_AUDIO_StartAdec(AdChn, gs_enPayloadType);
        if (s32Ret != HI_SUCCESS) {
            return;
        }

        s32AoChnCnt = pstAioAttr->u32ChnCnt;
        s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, s32AoChnCnt, pstAioAttr, g_in_sample_rate, gs_bAioReSample);
        if (s32Ret != HI_SUCCESS) {
            goto STOP_ADEC;
        }

        pfd = SAMPLE_AUDIO_OpenAencFile(AdChn, gs_enPayloadType);
        if (pfd == NULL) {
            SAMPLE_DBG(HI_FAILURE);
            goto STOP_AO;
        }
        s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAencAdec(AeChn, AdChn, pfd);
        if (s32Ret != HI_SUCCESS) {
            (HI_VOID)fclose(pfd);
            pfd = HI_NULL;
            SAMPLE_DBG(s32Ret);
            goto STOP_AO;
        }

        s32Ret = SAMPLE_COMM_AUDIO_AoBindAdec(AoDev, AoChn, AdChn);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            goto DESTORY_AENC_THREAD;
        }

        printf("bind adec:%d to ao(%d,%d) ok \n", AdChn, AoDev, AoChn);
    }

    printf("\nplease press twice ENTER to exit this sample\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    if (bSendAdec == HI_TRUE) {
        s32Ret = SAMPLE_COMM_AUDIO_AoUnbindAdec(AoDev, AoChn, AdChn);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
        }

    DESTORY_AENC_THREAD:
        (HI_VOID)SAMPLE_COMM_AUDIO_DestroyTrdAencAdec(AdChn);

    STOP_AO:
        (HI_VOID)SAMPLE_COMM_AUDIO_StopAo(AoDev, s32AoChnCnt, gs_bAioReSample);

    STOP_ADEC:
        (HI_VOID)SAMPLE_COMM_AUDIO_StopAdec(AdChn);
    }
}

static HI_VOID SAMPLE_AUDIO_AiAencInner(AUDIO_DEV AiDev, AUDIO_DEV AoDev, HI_S32 s32AencChnCnt, AIO_ATTR_S *pstAioAttr)
{
    HI_S32 i, j, s32Ret;
    AI_CHN AiChn;
    AENC_CHN AeChn = 0;
    pthread_t udp_thread;
    UdpSendThreadArg thread_arg;
    HI_BOOL bSendUdp = HI_TRUE; // 新增UDP发送标志

    for (i = 0; i < s32AencChnCnt; i++) {
        AeChn = i;
        AiChn = i;

        if (gs_bUserGetMode == HI_TRUE) {
            s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAiAenc(AiDev, AiChn, AeChn);
        } else {
            s32Ret = SAMPLE_COMM_AUDIO_AencBindAi(AiDev, AiChn, AeChn);
        }
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            goto EXIT;
        }
        printf("Ai(%d,%d) bind to AencChn:%d ok!\n", AiDev, AiChn, AeChn);
    }

    /* 设置UDP发送参数 */
    if (bSendUdp) {
        // 创建UDP socket
        thread_arg.sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (thread_arg.sockfd < 0) {
            perror("UDP socket creation failed");
            goto EXIT;
        }
        
        // 设置目标地址
        memset(&thread_arg.dest_addr, 0, sizeof(thread_arg.dest_addr));
        thread_arg.dest_addr.sin_family = AF_INET;
        thread_arg.dest_addr.sin_port = htons(UDP_DEST_PORT);
        if (inet_pton(AF_INET, UDP_DEST_IP, &thread_arg.dest_addr.sin_addr) <= 0) {
            perror("Invalid UDP address");
            close(thread_arg.sockfd);
            goto EXIT;
        }
        
        thread_arg.AeChn = AeChn;
        thread_arg.pbExitFlag = &g_sample_audio_exit;
        
        // 创建UDP发送线程
        if (pthread_create(&udp_thread, NULL, SAMPLE_AUDIO_UdpSendThread, &thread_arg) != 0) {
            perror("Thread creation failed");
            close(thread_arg.sockfd);
            goto EXIT;
        }
    }

    printf("\nAAC encoded audio streaming via UDP. Press ENTER twice to stop.\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    /* 清理资源 */
    g_sample_audio_exit = HI_TRUE;
    if (bSendUdp) {
        pthread_join(udp_thread, NULL);
    }

    for (i = 0; i < s32AencChnCnt; i++) {
        AeChn = i;
        AiChn = i;

        if (gs_bUserGetMode == HI_TRUE) {
            s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, AiChn);
        } else {
            s32Ret = SAMPLE_COMM_AUDIO_AencUnbindAi(AiDev, AiChn, AeChn);
        }
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
        }
    }
    return;

EXIT:
    for (j = 0; j < i; j++) {
        if (gs_bUserGetMode == HI_TRUE) {
            SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, j);
        } else {
            SAMPLE_COMM_AUDIO_AencUnbindAi(AiDev, j, j);
        }
    }
    return;
}

/* function : Ai -> Aenc -> file -> Adec -> Ao */
static HI_S32 SAMPLE_AUDIO_AiAenc(HI_VOID)
{
    HI_S32 s32Ret;
    HI_S32 s32AiChnCnt;
    HI_S32 s32AencChnCnt;
    AIO_ATTR_S stAioAttr;
    AUDIO_DEV AiDev = SAMPLE_AUDIO_INNER_AI_DEV;
    AUDIO_DEV AoDev = SAMPLE_AUDIO_INNER_AO_DEV;

    sample_audio_ai_aenc_init_param(&stAioAttr);

    /* step 1: start Ai */
    s32AiChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, &stAioAttr, g_out_sample_rate, gs_bAioReSample, NULL, 0);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto EXIT;
    }

    /* step 2: config audio codec */
    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto STOP_AI;
    }

    /* step 3: start Aenc */
    s32AencChnCnt = stAioAttr.u32ChnCnt >> (HI_U32)stAioAttr.enSoundmode;
    s32Ret = SAMPLE_COMM_AUDIO_StartAenc(s32AencChnCnt, &stAioAttr, gs_enPayloadType);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto STOP_AI;
    }

    SAMPLE_AUDIO_AiAencInner(AiDev, AoDev, s32AencChnCnt, &stAioAttr);

    s32Ret = SAMPLE_COMM_AUDIO_StopAenc(s32AencChnCnt);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

STOP_AI:
    s32Ret = SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, gs_bAioReSample, HI_FALSE);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

EXIT:
    return s32Ret;
}

static hi_void sample_audio_ai_ao_init_param(AIO_ATTR_S *attr)
{
    attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    attr->enWorkmode = AIO_MODE_I2S_MASTER;
    attr->enSoundmode = AUDIO_SOUND_MODE_MONO; // AUDIO_SOUND_MODE_STEREO;
    attr->u32EXFlag = 0;
    attr->u32FrmNum = FPS_30;
    attr->u32PtNumPerFrm = AACLC_SAMPLES_PER_FRAME;
    attr->u32ChnCnt = 1; // 2; /* 2: chn num */
    attr->u32ClkSel = 1;
    attr->enI2sType = AIO_I2STYPE_INNERCODEC;

    gs_bAioReSample = HI_FALSE;
    /* config ao resample attr if needed */
    if (gs_bAioReSample == HI_TRUE) {
        /* ai 48k -> 32k */
        g_out_sample_rate = AUDIO_SAMPLE_RATE_32000;

        /* ao 32k -> 48k */
        g_in_sample_rate = AUDIO_SAMPLE_RATE_32000;
    } else {
        g_in_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
        g_out_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
    }

    /* resample and anr should be user get mode */
    gs_bUserGetMode = (gs_bAioReSample == HI_TRUE) ? HI_TRUE : HI_FALSE;
}

static HI_VOID SAMPLE_AUDIO_AiAoInner(AUDIO_DEV AiDev, AI_CHN AiChn, AUDIO_DEV AoDev, AO_CHN AoChn)
{
    HI_S32 s32Ret;

    /* bind AI to AO channel */
    if (gs_bUserGetMode == HI_TRUE) {
        s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAiAo(AiDev, AiChn, AoDev, AoChn);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            return;
        }
    } else {
        s32Ret = SAMPLE_COMM_AUDIO_AoBindAi(AiDev, AiChn, AoDev, AoChn);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            return;
        }
    }
    printf("ai(%d,%d) bind to ao(%d,%d) ok\n", AiDev, AiChn, AoDev, AoChn);

    if (gs_bAoVolumeCtrl == HI_TRUE) {
        s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAoVolCtrl(AoDev);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            goto AIAO_ERR0;
        }
    }

    printf("\nplease press twice ENTER to exit this sample\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    if (gs_bAoVolumeCtrl == HI_TRUE) {
        s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdAoVolCtrl(AoDev);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            return;
        }
    }

AIAO_ERR0:
    if (gs_bUserGetMode == HI_TRUE) {
        s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, AiChn);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
        }
    } else {
        s32Ret = SAMPLE_COMM_AUDIO_AoUnbindAi(AiDev, AiChn, AoDev, AoChn);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
        }
    }

    return;
}

/* function : Ai -> Ao(with fade in/out and volume adjust) */
static HI_S32 SAMPLE_AUDIO_AiAo(HI_VOID)
{
    HI_S32 s32Ret;
    HI_S32 s32AiChnCnt;
    HI_S32 s32AoChnCnt;
    const AI_CHN AiChn = 0;
    const AO_CHN AoChn = 0;
    AIO_ATTR_S stAioAttr;
    AUDIO_DEV AiDev = SAMPLE_AUDIO_INNER_AI_DEV;
    AUDIO_DEV AoDev = SAMPLE_AUDIO_INNER_AO_DEV;

    sample_audio_ai_ao_init_param(&stAioAttr);

    /* enable AI channel */
    s32AiChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, &stAioAttr, g_out_sample_rate, gs_bAioReSample, NULL, 0);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto AIAO_ERR3;
    }

    /* enable AO channel */
    s32AoChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, s32AoChnCnt, &stAioAttr, g_in_sample_rate, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto AIAO_ERR2;
    }

    /* config internal audio codec */
    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto AIAO_ERR1;
    }

    SAMPLE_AUDIO_AiAoInner(AiDev, AiChn, AoDev, AoChn);

AIAO_ERR1:
    s32Ret = SAMPLE_COMM_AUDIO_StopAo(AoDev, s32AoChnCnt, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

AIAO_ERR2:
    s32Ret = SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, gs_bAioReSample, HI_FALSE);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

AIAO_ERR3:
    return s32Ret;
}

static hi_void sample_audio_ai_to_sys_chn_init_param(AIO_ATTR_S *attr)
{
    attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    attr->u32PtNumPerFrm = AACLC_SAMPLES_PER_FRAME;
    attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    attr->enWorkmode = AIO_MODE_I2S_MASTER;
    attr->enSoundmode = AUDIO_SOUND_MODE_STEREO;
    attr->u32EXFlag = 0;
    attr->u32FrmNum = FPS_30;
    attr->u32ChnCnt = 2; /* 2: chn num */
    attr->enI2sType = AIO_I2STYPE_INNERCODEC;
    attr->u32ClkSel = 1;

    gs_bAioReSample = HI_FALSE;
    /* config ao resample attr if needed */
    if (gs_bAioReSample == HI_TRUE) {
        /* ai 48k -> 16k */
        g_out_sample_rate = AUDIO_SAMPLE_RATE_16000;

        /* ao 16k -> 48k */
        g_in_sample_rate = AUDIO_SAMPLE_RATE_16000;
    } else {
        g_in_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
        g_out_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
    }

    /* resample and anr should be user get mode */
    gs_bUserGetMode = (gs_bAioReSample == HI_TRUE) ? HI_TRUE : HI_FALSE;
}

/* function : Ai -> Ao(with fade in/out and volume adjust) */
static HI_S32 SAMPLE_AUDIO_AiToAoSysChn(HI_VOID)
{
    HI_S32 s32Ret, s32AiChnCnt, s32AoChnCnt;
    const AI_CHN AiChn = 0;
    AIO_ATTR_S stAioAttr;
    AUDIO_DEV AiDev = SAMPLE_AUDIO_INNER_AI_DEV;
    AUDIO_DEV AoDev = SAMPLE_AUDIO_INNER_AO_DEV;

    sample_audio_ai_to_sys_chn_init_param(&stAioAttr);

    /* enable AI channel */
    s32AiChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, &stAioAttr, g_out_sample_rate, gs_bAioReSample, NULL, 0);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto SYS_CHN_ERR3;
    }

    /* enable AO channel */
    s32AoChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, s32AoChnCnt, &stAioAttr, g_in_sample_rate, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto SYS_CHN_ERR2;
    }

    /* config internal audio codec */
    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto SYS_CHN_ERR1;
    }

    /* bind AI to AO channel */
    s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAiAo(AiDev, AiChn, AoDev, AO_SYSCHN_CHNID);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto SYS_CHN_ERR1;
    }
    printf("ai(%d,%d) bind to ao(%d,%d) ok\n", AiDev, AiChn, AoDev, AO_SYSCHN_CHNID);
    printf("\nplease press twice ENTER to exit this sample\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, AiChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

SYS_CHN_ERR1:
    s32Ret = SAMPLE_COMM_AUDIO_StopAo(AoDev, s32AoChnCnt, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

SYS_CHN_ERR2:
    s32Ret = SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, gs_bAioReSample, HI_FALSE);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
    }

SYS_CHN_ERR3:
    return s32Ret;
}

static hi_void sample_audio_ai_vqe_process_ao_init_param(AIO_ATTR_S *attr, hi_void **ai_vqe_attr)
{
    attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    attr->enWorkmode = AIO_MODE_I2S_MASTER;
    attr->enSoundmode = AUDIO_SOUND_MODE_STEREO;
    attr->u32EXFlag = 0;
    attr->u32FrmNum = FPS_30;
    attr->u32PtNumPerFrm = SAMPLE_AUDIO_PTNUMPERFRM;
    attr->u32ChnCnt = 2; /* 2: chn num */
    attr->u32ClkSel = 1;
    attr->enI2sType = AIO_I2STYPE_INNERCODEC;

    gs_bAioReSample = HI_FALSE;
    g_in_sample_rate = AUDIO_SAMPLE_RATE_BUTT;
    g_out_sample_rate = AUDIO_SAMPLE_RATE_BUTT;

    if (g_ai_vqe_type == 1) { /* 1: Record VQE */
        (hi_void)memset_s(&g_ai_record_vqe_attr, sizeof(AI_RECORDVQE_CONFIG_S), 0, sizeof(AI_RECORDVQE_CONFIG_S));
        g_ai_record_vqe_attr.s32WorkSampleRate = AUDIO_SAMPLE_RATE_48000;
        g_ai_record_vqe_attr.s32FrameSample = SAMPLE_AUDIO_PTNUMPERFRM;
        g_ai_record_vqe_attr.enWorkstate = VQE_WORKSTATE_COMMON;
        g_ai_record_vqe_attr.s32InChNum = 2; /* 2: chn num */
        g_ai_record_vqe_attr.s32OutChNum = 2; /* 2: chn num */
        g_ai_record_vqe_attr.enRecordType = VQE_RECORD_NORMAL;
        g_ai_record_vqe_attr.stDrcCfg.bUsrMode = HI_FALSE;
        g_ai_record_vqe_attr.stRnrCfg.bUsrMode = HI_FALSE;
        g_ai_record_vqe_attr.stHdrCfg.bUsrMode = HI_FALSE;
        g_ai_record_vqe_attr.stHpfCfg.bUsrMode = HI_TRUE;
        g_ai_record_vqe_attr.stHpfCfg.enHpfFreq = AUDIO_HPF_FREQ_80;
        g_ai_record_vqe_attr.u32OpenMask =
            AI_RECORDVQE_MASK_DRC | AI_RECORDVQE_MASK_HDR | AI_RECORDVQE_MASK_HPF | AI_RECORDVQE_MASK_RNR;
        *ai_vqe_attr = (hi_void *)&g_ai_record_vqe_attr;
    } else if (g_ai_vqe_type == 3) { /* 3: TalkV2 VQE */
        (hi_void)memset_s(&g_ai_talkv2_vqe_attr, sizeof(AI_TALKVQEV2_CONFIG_S), 0, sizeof(AI_TALKVQEV2_CONFIG_S));
        g_ai_talkv2_vqe_attr.s32WorkSampleRate = AUDIO_SAMPLE_RATE_16000;
        g_ai_talkv2_vqe_attr.s32FrameSample = SAMPLE_AUDIO_PTNUMPERFRM;
        g_ai_talkv2_vqe_attr.enWorkstate = VQE_WORKSTATE_COMMON;
        g_ai_talkv2_vqe_attr.s32InChNum = 2; /* 2: chn num */
        g_ai_talkv2_vqe_attr.s32OutChNum = 2; /* 2: chn num */
        g_ai_talkv2_vqe_attr.stPnrCfg.bUsrMode = HI_FALSE;
        g_ai_talkv2_vqe_attr.stAgcCfg.bUsrMode = HI_FALSE;
        g_ai_talkv2_vqe_attr.u32OpenMask = AI_TALKVQEV2_MASK_PNR | AI_TALKVQEV2_MASK_AGC | AI_TALKVQEV2_MASK_LIMITER;
        *ai_vqe_attr = (hi_void *)&g_ai_talkv2_vqe_attr;
    } else {
        *ai_vqe_attr = HI_NULL;
    }
}

/* function : Ai -> Ao */
static HI_S32 SAMPLE_AUDIO_AiVqeProcessAo(HI_VOID)
{
    HI_S32 i, j, s32Ret, s32AiChnCnt, s32AoChnCnt;
    AUDIO_DEV AiDev = SAMPLE_AUDIO_INNER_AI_DEV;
    AUDIO_DEV AoDev = SAMPLE_AUDIO_INNER_AO_DEV;
    AIO_ATTR_S stAioAttr;
    HI_VOID *pAiVqeAttr = NULL;

    sample_audio_ai_vqe_process_ao_init_param(&stAioAttr, &pAiVqeAttr);

    /* step 1: start Ai */
    s32AiChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, &stAioAttr, g_out_sample_rate, gs_bAioReSample, pAiVqeAttr,
        g_ai_vqe_type);
    if (s32Ret != HI_SUCCESS) {
        goto VQE_ERR2;
    }

    /* step 2: start Ao */
    s32AoChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, s32AoChnCnt, &stAioAttr, g_in_sample_rate, gs_bAioReSample);
    if (s32Ret != HI_SUCCESS) {
        goto VQE_ERR1;
    }

    /* step 3: config audio codec */
    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        goto VQE_ERR0;
    }

    /* step 4: Ao bind Ai Chn */
    for (i = 0; i < (hi_s32)((HI_U32)s32AiChnCnt >> (HI_U32)stAioAttr.enSoundmode); i++) {
        s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAiAo(AiDev, i, AoDev, i);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
            for (j = 0; j < i; j++) {
                SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, j);
            }
            goto VQE_ERR0;
        }

        printf("bind ai(%d,%d) to ao(%d,%d) ok \n", AiDev, i, AoDev, i);
    }

    printf("\nplease press twice ENTER to exit this sample\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    /* step 5: exit the process */
    for (i = 0; i < s32AiChnCnt; i++) {
        s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, i);
        if (s32Ret != HI_SUCCESS) {
            SAMPLE_DBG(s32Ret);
        }
    }

VQE_ERR0:
    (HI_VOID)SAMPLE_COMM_AUDIO_StopAo(AoDev, s32AoChnCnt, gs_bAioReSample);

VQE_ERR1:
    (HI_VOID)SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, gs_bAioReSample, HI_TRUE);

VQE_ERR2:
    return s32Ret;
}

static hi_void sample_audio_ai_hdmi_ao_init_param(AIO_ATTR_S *ai_attr, AIO_ATTR_S *ao_attr)
{
    ai_attr->enI2sType = AIO_I2STYPE_INNERCODEC;
    ai_attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    ai_attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    ai_attr->enWorkmode = AIO_MODE_I2S_MASTER;
    ai_attr->enSoundmode = AUDIO_SOUND_MODE_STEREO;
    ai_attr->u32EXFlag = 1;
    ai_attr->u32FrmNum = FPS_30;
    ai_attr->u32PtNumPerFrm = SAMPLE_AUDIO_PTNUMPERFRM;
    ai_attr->u32ChnCnt = 2; /* 2: chn num */
    ai_attr->u32ClkSel = 1;

    ao_attr->enSamplerate = AUDIO_SAMPLE_RATE_48000;
    ao_attr->enBitwidth = AUDIO_BIT_WIDTH_16;
    ao_attr->enWorkmode = AIO_MODE_I2S_MASTER;
    ao_attr->enSoundmode = AUDIO_SOUND_MODE_STEREO;
    ao_attr->u32EXFlag = 1;
    ao_attr->u32FrmNum = FPS_30;
    ao_attr->u32PtNumPerFrm = SAMPLE_AUDIO_PTNUMPERFRM;
    ao_attr->u32ChnCnt = 2; /* 2: chn num */
    ao_attr->u32ClkSel = 0;
    ao_attr->enI2sType = AIO_I2STYPE_INNERHDMI;

    gs_bAioReSample = HI_FALSE;
    /* resample should be user get mode */
    gs_bUserGetMode = (gs_bAioReSample == HI_TRUE) ? HI_TRUE : HI_FALSE;
}

/* function : Ai -> Ao(Hdmi) */
static HI_S32 SAMPLE_AUDIO_AiHdmiAo(HI_VOID)
{
    HI_S32 s32Ret, s32AiChnCnt;
    AUDIO_DEV AiDev = SAMPLE_AUDIO_INNER_AI_DEV;
    const AI_CHN AiChn = 0;
    AUDIO_DEV AoDev = SAMPLE_AUDIO_INNER_HDMI_AO_DEV;
    const AO_CHN AoChn = 0;
    AIO_ATTR_S stAioAttr;
    AIO_ATTR_S stHdmiAoAttr;

    sample_audio_ai_hdmi_ao_init_param(&stAioAttr, &stHdmiAoAttr);

    /* enable AI channel */
    s32AiChnCnt = stAioAttr.u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, &stAioAttr, AUDIO_SAMPLE_RATE_BUTT, HI_FALSE, NULL, 0);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    /* enable AO channel */
    s32Ret =
        SAMPLE_COMM_AUDIO_StartAo(AoDev, stHdmiAoAttr.u32ChnCnt, &stHdmiAoAttr, stHdmiAoAttr.enSamplerate, HI_FALSE);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    /* config audio codec */
    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    /* AI to AO channel */
    s32Ret = SAMPLE_COMM_AUDIO_CreateTrdAiAo(AiDev, AiChn, AoDev, AoChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    printf("\nplease press twice ENTER to exit this sample\n");
    SAMPLE_AUDIO_Getchar();
    SAMPLE_AUDIO_Getchar();

    s32Ret = SAMPLE_COMM_AUDIO_DestroyTrdAi(AiDev, AiChn);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    s32Ret = SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, HI_FALSE, HI_FALSE);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    s32Ret = SAMPLE_COMM_AUDIO_StopAo(AoDev, stHdmiAoAttr.u32ChnCnt, HI_FALSE);
    if (s32Ret != HI_SUCCESS) {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static HI_VOID SAMPLE_AUDIO_Usage(HI_VOID)
{
    printf("\n\n/Usage:./sample_audio <index>/\n");
    printf("\tindex and its function list below\n");
    printf("\t0:  start AI to AO loop\n");
    printf("\t1:  send audio frame to AENC channel from AI, save them\n");
    printf("\t2:  read audio stream from file, decode and send AO\n");
    printf("\t3:  start AI(VQE process), then send to AO\n");
    printf("\t4:  start AI to AO(Hdmi) loop\n");
    printf("\t5:  start AI to AO(Syschn) loop\n");
}

/* function : to process abnormal case */
static void SAMPLE_AUDIO_HandleSig(HI_S32 signo)
{
    if (g_sample_audio_exit == HI_TRUE) {
        return;
    }

    if ((signo == SIGINT) || (signo == SIGTERM)) {
        g_sample_audio_exit = HI_TRUE;
    }
}

#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
/* function : to register vqe module */
static HI_S32 SAMPLE_AUDIO_RegisterVQEModule(HI_VOID)
{
    HI_S32 s32Ret;
    AUDIO_VQE_REGISTER_S stVqeRegCfg = { 0 };

    /* Resample */
    stVqeRegCfg.stResModCfg.pHandle = HI_VQE_RESAMPLE_GetHandle();

    /* RecordVQE */
    stVqeRegCfg.stRecordModCfg.pHandle = HI_VQE_RECORD_GetHandle();

    /* TalkVQE */
    stVqeRegCfg.stHpfModCfg.pHandle = HI_VQE_HPF_GetHandle();
    stVqeRegCfg.stAecModCfg.pHandle = HI_VQE_AEC_GetHandle();
    stVqeRegCfg.stAgcModCfg.pHandle = HI_VQE_AGC_GetHandle();
    stVqeRegCfg.stAnrModCfg.pHandle = HI_VQE_ANR_GetHandle();
    stVqeRegCfg.stEqModCfg.pHandle = HI_VQE_EQ_GetHandle();

    s32Ret = HI_MPI_AUDIO_RegisterVQEModule(&stVqeRegCfg);
    if (s32Ret != HI_SUCCESS) {
        printf("%s: register vqe module fail with s32Ret = 0x%x!\n", __FUNCTION__, s32Ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}
#endif

static hi_void main_inner(hi_u32 index)
{
    switch (index) {
        case 0: {
            SAMPLE_AUDIO_AiAo();
            break;
        }
        case 1: {
            SAMPLE_AUDIO_AiAenc();
            break;
        }
        case 2: { /* 2: sample index */
            SAMPLE_AUDIO_AdecAo();
            break;
        }
        case 3: { /* 3: sample index */
            SAMPLE_AUDIO_AiVqeProcessAo();
            break;
        }
        case 4: { /* 4: sample index */
            SAMPLE_AUDIO_AiHdmiAo();
            break;
        }
        case 5: { /* 5: sample index */
            SAMPLE_AUDIO_AiToAoSysChn();
            break;
        }
        default: {
            break;
        }
    }
}

#if defined(__HuaweiLite__) && (!defined(__OHOS__))
HI_S32 app_main(int argc, char *argv[])
#else
HI_S32 main(int argc, char *argv[])
#endif
{
    HI_S32 s32Ret;
    VB_CONFIG_S stVbConf;
    HI_U32 u32Index;
    sdk_init();

    if (argc != 2) { /* 2: arg count */
        SAMPLE_AUDIO_Usage();
        return HI_FAILURE;
    }

    s32Ret = strcmp(argv[1], "-h");
    if (s32Ret == HI_SUCCESS) {
        SAMPLE_AUDIO_Usage();
        return HI_FAILURE;
    }

    u32Index = atoi(argv[1]);
    if (u32Index > 5) { /* 5: sample index */
        SAMPLE_AUDIO_Usage();
        return HI_FAILURE;
    }

#if (!defined(__HuaweiLite__)) || defined(__OHOS__)
    SAMPLE_SYS_SIGNAL(&SAMPLE_AUDIO_HandleSig);
#endif

#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
    SAMPLE_AUDIO_RegisterVQEModule();
#endif

    s32Ret = memset_s(&stVbConf, sizeof(VB_CONFIG_S), 0, sizeof(VB_CONFIG_S));
    if (s32Ret != EOK) {
        printf("%s: memset_s failed, ret:0x%x.\n", __FUNCTION__, s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (s32Ret != HI_SUCCESS) {
        printf("%s: system init failed with %d!\n", __FUNCTION__, s32Ret);
        return HI_FAILURE;
    }

#ifndef HI_VQE_USE_STATIC_MODULE_REGISTER
    SAMPLE_AUDIO_AddLibPath();
#endif

    HI_MPI_AENC_AacInit();
    HI_MPI_ADEC_AacInit();

    main_inner(u32Index);

    HI_MPI_AENC_AacDeInit();
    HI_MPI_ADEC_AacDeInit();

    SAMPLE_COMM_SYS_Exit();

    sdk_exit();
    return s32Ret;
}
