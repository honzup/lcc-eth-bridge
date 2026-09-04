#pragma once
// ===================== Waveshare ESP32-P4-ETH board map =====================
// TWAI (CAN) -> TCAN332DR transceiver
#define BOARD_TWAI_TX_GPIO   20   // -> TCAN332 pin 1 (TXD)
#define BOARD_TWAI_RX_GPIO   21   // <- TCAN332 pin 4 (RXD)

// ============================ VERIFY THESE ==================================
// The Ethernet pin/PHY map is the ONE thing that can't be confirmed remotely —
// it MUST match the P4-ETH schematic. If the board never gets a DHCP lease,
// this is why. Cross-check against Espressif's maintained example:
//   idf.py create-project-from-example "espressif/esp-idf:examples/ethernet/basic"
// configured for esp32p4, and copy its exact PHY/pin/clock settings here.
#define BOARD_ETH_PHY_ADDR      1
#define BOARD_ETH_PHY_RST_GPIO  51   // hint from source chat — VERIFY (-1 = no reset line)
// MDC/MDIO/RMII pins come from the esp32p4 ETH_ESP32_EMAC_DEFAULT_CONFIG (MDC=31,
// MDIO=52) — the P4 devkit map, which matches the "31/52/51/50" schematic hint.
// If the board uses a PHY other than IP101, change esp_eth_phy_new_ip101() in main.c.
// If link fails, override emac_cfg.smi_gpio / .emac_dataif_gpio in main.c to match.
