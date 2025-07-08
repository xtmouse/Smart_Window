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

#ifndef UDP_CONFIG_H
#define UDP_CONFIG_H

#define UDP_DEMO

#ifdef UDP_DEMO
#define UDP_DEDMO_SUPPORT
#define CONFIG_WIFI_STA_MODULE
#elif defined(UDP_AP_DEMO)
#define UDP_DEDMO_SUPPORT
#define CONFIG_WIFI_AP_MODULE
#endif
/**
  @brief enable HW iot cloud
 * HW iot cloud send message to Hi3861 board and Hi861 board publish message to HW iot cloud
 */

// /<CONFIG THE LOG
/* if you need the iot log for the development ,
please enable it, else please comment it
*/
#define CONFIG_LINKLOG_ENABLE   1

// /<CONFIG THE WIFI
/* Please modify the ssid and pwd for the own */
#define AP_SSID  "HH" // WIFI SSID
#define AP_PWD   "12345678" // WIFI PWD

// /<Configure the iot platform
/* Please modify the device id and pwd for your own */
/* 设备ID名称，请参考华为物联网云文档获取该设备的ID。例如:60790e01ba4b2702c053ff03_helloMQTT */
#define CONFIG_DEVICE_ID  "xxxxxxx"
/* 设备密码，请参考华为物联网云文档设置该设备密码。例如：hispark2021 */
#define CONFIG_DEVICE_PWD "xxxxxx"
/* <if you use the tls mode and x509mode,
please modify the DEVICE CA AND PRIVATE KEY IN iot_main.c
*/
/* Tencent iot Cloud user ID , password */
#define CONFIG_USER_ID    "xxxxxxxx"
#define CONFIG_USER_PWD   "xxxxxxxx"

#endif