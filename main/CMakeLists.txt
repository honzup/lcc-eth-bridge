idf_component_register(
    SRCS "main.c" "bridge.c" "gridconnect.c" "ota.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_twai esp_eth esp_netif esp_event nvs_flash lwip
             esp_http_server app_update)

# Solo bench bring-up:  LCC_SELFTEST=1 idf.py build flash monitor
if(DEFINED ENV{LCC_SELFTEST})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LCC_SELFTEST=1)
endif()

# Push-OTA token (secret stays out of source/git). No token -> no OTA endpoint.
# Build with:  OTA_TOKEN=yoursecret idf.py build flash
if(DEFINED ENV{OTA_TOKEN})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE OTA_TOKEN="$ENV{OTA_TOKEN}")
endif()
