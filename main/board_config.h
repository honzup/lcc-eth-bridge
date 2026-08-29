#pragma once
// ===================== Waveshare ESP32-P4-ETH board map =====================
// TWAI (CAN) -> TCAN332DR transceiver
#define BOARD_TWAI_TX_GPIO   17   // -> TCAN332 pin 1 (TXD)
#define BOARD_TWAI_RX_GPIO   18   // <- TCAN332 pin 4 (RXD)

// ============================ VERIFY THESE ==================================
// The Ethernet pin/PHY map is the ONE thing that can't be confirmed remotely —
// it MUST match the P4-ETH schematic. If the board never gets a DHCP lease,
// this is why. Cross-check against Espressif's maintained example:
//   idf.py create-project-from-example "espressif/esp-idf:examples/ethernet/basic"
// configured for esp32p4, and copy its exact PHY/pin/clock settings here.
#define BOARD_ETH_PHY_ADDR      1
#define BOARD_ETH_PHY_RST_GPIO  51   // hint from source chat — VERIFY
#define BOARD_ETH_MDC_GPIO      52   // VERIFY
#define BOARD_ETH_MDIO_GPIO     50   // VERIFY
// If the board uses a PHY other than IP101, change esp_eth_phy_new_ip101() in main.c.
// If link fails, also override the RMII data/clock pins (ETH_ESP32_EMAC_DEFAULT_CONFIG
// fills chip defaults, which may not match this board's wiring).
