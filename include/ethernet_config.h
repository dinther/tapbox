#pragma once
#include <string.h>
#include <stdio.h>
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *ETH_TAG = "eth";

static bool ethConnected = false;
static char ethIPStr[16]  = "0.0.0.0";

static esp_netif_t *s_eth_netif  = nullptr;
static bool         s_use_static = false;

static void on_eth_event(void *, esp_event_base_t, int32_t id, void *) {
    switch (id) {
        case ETHERNET_EVENT_START:
            esp_netif_set_hostname(s_eth_netif, "tapbox");
            break;
        case ETHERNET_EVENT_CONNECTED:
            if (s_use_static) {
                // With static IP there is no GOT_IP event; mark connected here
                // and populate ethIPStr from the netif's configured address.
                esp_netif_ip_info_t info;
                if (esp_netif_get_ip_info(s_eth_netif, &info) == ESP_OK)
                    esp_ip4addr_ntoa(&info.ip, ethIPStr, sizeof(ethIPStr));
                ethConnected = true;
            }
            break;
        case ETHERNET_EVENT_DISCONNECTED:
        case ETHERNET_EVENT_STOP:
            ethConnected = false;
            break;
        default:
            break;
    }
}

static void on_got_ip(void *, esp_event_base_t, int32_t, void *data) {
    // Only used in DHCP mode; static mode sets ethConnected in on_eth_event.
    if (s_use_static) return;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ethConnected = true;
    esp_ip4addr_ntoa(&event->ip_info.ip, ethIPStr, sizeof(ethIPStr));
    ESP_LOGI(ETH_TAG, "IP: %s", ethIPStr);
}

// Call after esp_event_loop_create_default() and esp_netif_init().
// netMode: 0=DHCP, 1=static. ip/sn/gt are 4-element octet arrays.
static void initEthernet(int netMode, const int ip[4], const int sn[4], const int gt[4]) {
    s_use_static = (netMode == 1);

    // Power up LAN8720 via GPIO16
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_16, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Create ETH netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);

    if (s_use_static) {
        esp_netif_dhcpc_stop(s_eth_netif);
        esp_netif_ip_info_t info = {};
        info.ip.addr      = htonl(((uint32_t)ip[0]<<24)|((uint32_t)ip[1]<<16)|((uint32_t)ip[2]<<8)|(uint32_t)ip[3]);
        info.netmask.addr = htonl(((uint32_t)sn[0]<<24)|((uint32_t)sn[1]<<16)|((uint32_t)sn[2]<<8)|(uint32_t)sn[3]);
        info.gw.addr      = htonl(((uint32_t)gt[0]<<24)|((uint32_t)gt[1]<<16)|((uint32_t)gt[2]<<8)|(uint32_t)gt[3]);
        esp_netif_set_ip_info(s_eth_netif, &info);
        ESP_LOGI(ETH_TAG, "Static IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    }

    // Register event handlers
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,    &on_eth_event, nullptr);
    esp_event_handler_register(IP_EVENT,  IP_EVENT_ETH_GOT_IP, &on_got_ip,   nullptr);

    // Internal EMAC with external LAN8720A via RMII + external clock on GPIO0.
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num  = GPIO_NUM_23;
    emac_cfg.smi_gpio.mdio_num = GPIO_NUM_18;
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = 0;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t   *mac     = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr       = 1;
    phy_cfg.reset_gpio_num = -1;
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_cfg);

    esp_eth_handle_t eth_handle;
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_driver_install(&eth_cfg, &eth_handle);

    esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(eth_handle));
    esp_eth_start(eth_handle);
}
