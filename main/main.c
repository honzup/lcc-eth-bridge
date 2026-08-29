#include "board_config.h"
#include "bridge.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp32.h"   // eth_esp32_emac_config_t (IDF 5.4+)
#include "nvs_flash.h"

#define TAG "main"

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *e = data;
    ESP_LOGI(TAG, "ETH got IP: " IPSTR "  -> point JMRI OpenLCB GridConnect here, port 12021",
             IP2STR(&e->ip_info.ip));
}

static void ethernet_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t nc = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *neti = esp_netif_new(&nc);

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num  = BOARD_ETH_MDC_GPIO;
    emac_cfg.smi_gpio.mdio_num = BOARD_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr       = BOARD_ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = BOARD_ETH_PHY_RST_GPIO;
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);  // swap if board uses another PHY

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &eth));
    ESP_ERROR_CHECK(esp_netif_attach(neti, esp_eth_new_netif_glue(eth)));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_eth_start(eth));
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ethernet_start();   // Stage 1-2: link + DHCP (watch serial for the IP)
    bridge_start();     // Stage 3-7: TWAI @125k <-> GridConnect TCP :12021
    ESP_LOGI(TAG, "LCC <-> Ethernet bridge up");
}
