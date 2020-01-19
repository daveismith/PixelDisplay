/* Console example — WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "esp_event_loop.h"

#include "lwip/netif.h"
#include "lwip/ip.h"

extern EventGroupHandle_t wifi_event_group;

const extern int IP4_CONNECTED_BIT;
const extern int STARTED_BIT;

extern struct netif *netif_list;

static bool wifi_join(const char* ssid, const char* pass, int timeout_ms)
{
    wifi_config_t wifi_config = { 0 };
    strncpy((char*) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strncpy((char*) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    int bits = xEventGroupWaitBits(wifi_event_group, IP4_CONNECTED_BIT,
            1, 1, timeout_ms / portTICK_PERIOD_MS);
    return (bits & IP4_CONNECTED_BIT) != 0;
}

/** Arguments used by 'join' function */
static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;

static int connect(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(__func__, "Connecting to '%s'",
            join_args.ssid->sval[0]);

    bool connected = wifi_join(join_args.ssid->sval[0],
                           join_args.password->sval[0],
                           join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(__func__, "Connection timed out");
        return 1;
    }
    ESP_LOGI(__func__, "Connected");
    return 0;
}

const char *FLAG_NAMES[] = {
    "UP",
    "BROADCAST",
    "LINK_UP",
    "ETHARP",
    "ETHERNET",
    "IGMP",
    "MLD6",
    "GARP"
};

static int ifconfig_handler(int argc, char **argv)
{
    struct netif *netif = netif_list;
    bool needComma = false;

    /*
en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
	ether f0:18:98:27:6c:fe
	inet6 fe80::18a2:f0a4:e6d8:53ea%en0 prefixlen 64 secured scopeid 0xb
	inet 10.0.19.16 netmask 0xffffff00 broadcast 10.0.19.255
	inet6 2001:470:1d:20c:494:399e:39cc:f7f9 prefixlen 64 autoconf secured
	inet6 2001:470:1d:20c:3cd6:7432:3185:275a prefixlen 64 autoconf temporary
	nd6 options=201<PERFORMNUD,DAD>
	media: autoselect
	status: active
     */

    while (NULL != netif) {
        needComma = false;
        printf("%c%c%u: flags=%04u<", netif->name[0], netif->name[1], netif->num, netif->flags);
        for (size_t idx = 0; idx < sizeof(FLAG_NAMES) / sizeof(char *); idx++) {
            if ((netif->flags & (1 << idx)) != (1 << idx))
                continue;

            if (needComma) { printf(","); }
            printf("%s", FLAG_NAMES[idx]);
            needComma = true;
        }
        printf("> mtu %u\n", netif->mtu);
        if (netif->hwaddr_len > 0) {
            printf("        ether ");
            for (size_t idx = 0; idx < netif->hwaddr_len; idx++) { 
                if ( 0 != idx) { printf(":"); }
                printf("%02x", netif->hwaddr[idx]);
            }
            printf("\n");
        }

        // link local IPv6?
        // IPv4
        printf("        inet %s netmask 0x%08"PRIx32" broadcast %s\n", 
                        ip_ntoa(&netif->ip_addr),
                        ip4_addr_get_u32(&netif->netmask.u_addr.ip4),
                        ip_ntoa(&netif->gw));

#ifdef LWIP_IPV6
        // global IPv6?
        for (size_t idx = 0; idx < LWIP_IPV6_NUM_ADDRESSES; idx++) {
            if (IP6_ADDR_VALID != (netif->ip6_addr_state[idx] & IP6_ADDR_VALID))
                continue;


            printf("        inet6 %s\n", 
                        ip6addr_ntoa(&netif->ip6_addr[idx].u_addr.ip6));
        }
#endif //LWIP_IPV6

        // media
#ifdef MIB2_STATS
        //printf("stats\n");
#endif
        // status

        netif = netif->next;
    }

    return 0;
}

static int disconnect(int argc, char** argv)
{
   esp_wifi_disconnect();
   esp_wifi_restore();
   return 0;
}

void register_wifi()
{
    join_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    join_args.timeout->ival[0] = 5000; // set default value
    join_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "join",
        .help = "Join WiFi AP as a station",
        .hint = NULL,
        .func = &connect,
        .argtable = &join_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );

    const esp_console_cmd_t ifconfig_cmd = {
        .command = "ifconfig",
        .help = "configure network interface parameters",
        .hint = NULL,
        .func = &ifconfig_handler
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&ifconfig_cmd) );

    const esp_console_cmd_t disconnect_cmd = {
	.command = "disconnect",
	.help = "disconnect from WiFi AP",
	.hint = NULL,
	.func = &disconnect,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&disconnect_cmd) );
}
