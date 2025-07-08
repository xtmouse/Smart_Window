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

// /<this demo make the wifi to connect to the specified AP

#include <unistd.h>
#include <hi_wifi_api.h>
#include <lwip/ip_addr.h>
#include <lwip/netifapi.h>
#include <hi_types_base.h>
#include <hi_task.h>
#include <hi_mem.h>
#include "wifi_device.h"
#include "cmsis_os2.h"
#include "wifi_device_config.h"
#include "lwip/api_shell.h"
#include "udp_config.h"

#define APP_INIT_VAP_NUM    2
#define APP_INIT_USR_NUM    2

static struct netif *g_lwipNetif = NULL;
static hi_bool  g_scanDone = HI_FALSE;
static struct netif* g_iface = NULL;
void WifiStaStop(int netId);
static int WifiStaStart(void);
int g_netId = -1;
int g_staConnect = 0;
#define MAC_ADDR_BUF_LEN (32)
#define WIFI_CONNECT_STATUS ((unsigned char)0x02)
/**
 @brief  print wifi sta module link information
 @param WifiLinkedInfo: wifi sta struct,include device's SSID,BSSID,IP address, and so on...
 @param info : wifi sta link information
*/
static void PrintLinkedInfo(const WifiLinkedInfo* info)
{
    int ret = 0;

    if (!info) {
        return;
    }

    static char macAddress[MAC_ADDR_BUF_LEN] = {0};
    unsigned char* mac = info->bssid;
    if (snprintf_s(macAddress, sizeof(macAddress) + 1, sizeof(macAddress), "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]) < 0) { /* mac地址从0,1,2,3,4,5位 */
            return;
    }
}
/**
 @brief  The function will show the state changes during WiFi connection
 @param WifiLinkedInfo: wifi sta struct,include device's SSID,BSSID,IP address, and so on...
 @param info : wifi sta link information
 @param state: wifi state
*/
static void OnWifiConnectionChanged(int state, const WifiLinkedInfo* info)
{
    if (!info) {
        return;
    }

    printf("%s %d, state = %d, info = \r\n", __FUNCTION__, __LINE__, state);
    PrintLinkedInfo(info);

    if (state == WIFI_STATE_AVALIABLE) {
        g_staConnect = 1;
    } else {
        g_staConnect = 0;
    }
}
/**
    @brief The function will display the scan results during WiFi connection
    @param state: wifi scan result state
    @param size: wifi scan result buffer size
*/
static void OnWifiScanStateChanged(int state, int size)
{
    printf("%s %d, state = %X, size = %d\r\n", __FUNCTION__, __LINE__, state, size);
}

static WifiEvent g_defaultWifiEventListener = {
    .OnWifiConnectionChanged = OnWifiConnectionChanged,
    .OnWifiScanStateChanged = OnWifiScanStateChanged
};
/**
    @brief This function will start wifi station module, and WiFi will connect to the hotspot
*/

static int WifiStaStart(void)
{
    WifiDeviceConfig apConfig = {0};
    (void)strcpy_s(apConfig.ssid, strlen(AP_SSID) + 1, AP_SSID);
    (void)strcpy_s(apConfig.preSharedKey, strlen(AP_PWD) + 1, AP_PWD);
    apConfig.securityType = WIFI_SEC_TYPE_PSK;

    WifiErrorCode errCode;
    int netId = -1;

    errCode = RegisterWifiEvent(&g_defaultWifiEventListener);
    printf("RegisterWifiEvent: %d\r\n", errCode);

    errCode = EnableWifi();
    printf("EnableWifi: %d\r\n", errCode);

    errCode = AddDeviceConfig(&apConfig, &netId);
    printf("AddDeviceConfig: %d\r\n", errCode);

    g_staConnect = 0;
    errCode = ConnectTo(netId);
    printf("ConnectTo(%d): %d\r\n", netId, errCode);

    while (!g_staConnect) { // wait until connect to AP
        osDelay(10);    /* 连接10ms */
    }
    printf("g_staConnect: %d\r\n", g_staConnect);

    g_iface = netifapi_netif_find("wlan0");
    if (g_iface) {
        err_t ret = netifapi_dhcp_start(g_iface);
        printf("netifapi_dhcp_start: %d\r\n", ret);

        osDelay(100); // wait DHCP server give me IP 100ms
        ret = netifapi_netif_common(g_iface, dhcp_clients_info_show, NULL);
        printf("netifapi_netif_common: %d\r\n", ret);
    }
    return netId;
}
/**
    @brief This function will start wifi station module, and WiFi will connect to the hotspot
           The function gets DHCP, and so on...
*/
void WifiStaModule(void)
{
    ip4_addr_t ipAddr;
    ip4_addr_t ipAny;
    IP4_ADDR(&ipAny, 0, 0, 0, 0);
    IP4_ADDR(&ipAddr, 0, 0, 0, 0);
    g_netId = WifiStaStart();
    printf("wifi sta dhcp done\r\n");
    return;
}
