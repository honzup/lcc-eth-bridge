idf_component_register(
    SRCS "main.c" "bridge.c" "gridconnect.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_twai esp_eth esp_netif esp_event nvs_flash lwip)

# Solo bench bring-up:  LCC_SELFTEST=1 idf.py build flash monitor
if(DEFINED ENV{LCC_SELFTEST})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LCC_SELFTEST=1)
endif()
