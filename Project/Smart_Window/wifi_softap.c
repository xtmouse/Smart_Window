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

#include "wifi_softap.h"
#include "hi_wifi_api.h"
#include "lwip/ip_addr.h"
#include "lwip/netifapi.h"

#define APP_INIT_VAP_NUM    2
#define APP_INIT_USR_NUM    2

static struct netif *g_lwipNetif = NULL;

/* clear netif's ip, gateway and netmask */
void HiSoftapResetAddr(const struct netif *pstLwipNetif)
{
    ip4_addr_t st_gw;
    ip4_addr_t st_ipaddr;
    ip4_addr_t st_netmask;

    if (pstLwipNetif == NULL) {
        printf("hisi_reset_addr::Null param of netdev\r\n");
        return;
    }

    IP4_ADDR(&st_ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&st_gw, 0, 0, 0, 0);
    IP4_ADDR(&st_netmask, 0, 0, 0, 0);

    netifapi_netif_set_addr(pstLwipNetif, &st_ipaddr, &st_netmask, &st_gw);
}

int HiWifiStartSoftap(void)
{
    int ret;
    errno_t rc;
    char ifname[WIFI_IFNAME_MAX_SIZE + 1] = {0};
    int len = sizeof(ifname);
    hi_wifi_softap_config hapd_conf = {0};
    const unsigned char wifiVapResNum = APP_INIT_VAP_NUM;
    const unsigned char wifiUserResNum = APP_INIT_USR_NUM;
    ip4_addr_t st_gw;
    ip4_addr_t st_ipaddr;
    ip4_addr_t st_netmask;
    ip4_addr_t st_port;

    rc = memcpy_s(hapd_conf.ssid, HI_WIFI_MAX_SSID_LEN + 1, "XXXXXXXX", 9); /* 9:ssid length */
    if (rc != EOK) {
        return -1;
    }
    hapd_conf.authmode = HI_WIFI_SECURITY_OPEN;
    hapd_conf.channel_num = 1;

    ret = hi_wifi_softap_start(&hapd_conf, ifname, &len);
    if (ret != HISI_OK) {
        printf("hi_wifi_softap_start\n");
        return -1;
    }

    /* acquire netif for IP operation */
    g_lwipNetif = netifapi_netif_find(ifname);
    if (g_lwipNetif == NULL) {
        printf("%s: get netif failed\n", __FUNCTION__);
        return -1;
    }

    IP4_ADDR(&st_gw, 192, 168, 1, 1);          /* input your IP for example: 192.168.1.1 */
    IP4_ADDR(&st_ipaddr, 192, 168, 1, 4);      /* input your netmask for example: 192.168.4.1 */
    IP4_ADDR(&st_netmask, 255, 255, 255, 0);     /* input your gateway for example: 255.255.255.0 */
    netifapi_netif_set_addr(g_lwipNetif, &st_ipaddr, &st_netmask, &st_gw);

    netifapi_dhcps_start(g_lwipNetif, 0, 0);

    return 0;
}

void HiWifiStopSoftap(void)
{
    int ret;

    netifapi_dhcps_stop(g_lwipNetif);
    HiSoftapResetAddr(g_lwipNetif);

    ret = hi_wifi_softap_stop();
    if (ret != HISI_OK) {
        printf("failed to stop softap\n");
    }

    ret = hi_wifi_deinit();
    if (ret != HISI_OK) {
        printf("failed to deinit wifi\n");
    }

    g_lwipNetif = NULL;
}

