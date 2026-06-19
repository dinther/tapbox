#pragma once
#include <string.h>
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *ETH_TAG = "eth";

static bool ethConnected = false;
static char ethIPStr[16]  = "0.0.0.0";

// Forward-declared so the event handler can call esp_netif_set_hostname.
static esp_netif_t *s_eth_netif = nullptr;

static void on_eth_event(void *, esp_event_base_t, int32_t id, void *) {
    switch (id) {
        case ETHERNET_EVENT_START:
            esp_netif_set_hostname(s_eth_netif, "tapbox");
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
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ethConnected = true;
    esp_ip4addr_ntoa(&event->ip_info.ip, ethIPStr, sizeof(ethIPStr));
    ESP_LOGI(ETH_TAG, "IP: %s", ethIPStr);
}

// Call after esp_event_loop_create_default() and esp_netif_init().
static void initEthernet() {
    // WT32-ETH01: power up LAN8720 via GPIO16
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_16, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Create ETH netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);

    // Register event handlers
    esp_event_handler_register(ETH_EVENT,  ESP_EVENT_ANY_ID,    &on_eth_event, nullptr);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_ETH_GOT_IP, &on_got_ip,   nullptr);

    // Internal EMAC with external LAN8720A via RMII + external clock on GPIO0.
    // ETH_ESP32_EMAC_DEFAULT_CONFIG() already sets MDC=23, MDIO=18,
    // clock_mode=EMAC_CLK_EXT_IN, clock_gpio=0 — perfect for WT32-ETH01.
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num  = GPIO_NUM_23;
    emac_cfg.smi_gpio.mdio_num = GPIO_NUM_18;
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = 0;  // GPIO0

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t   *mac     = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr       = 1;
    phy_cfg.reset_gpio_num = -1;
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_cfg);

    esp_eth_handle_t    eth_handle;
    esp_eth_config_t    eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_driver_install(&eth_cfg, &eth_handle);

    esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(eth_handle));
    esp_eth_start(eth_handle);
}
