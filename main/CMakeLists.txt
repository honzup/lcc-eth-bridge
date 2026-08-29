idf_component_register(
    SRCS "main.c" "bridge.c" "gridconnect.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_twai esp_eth esp_netif esp_event nvs_flash lwip)
