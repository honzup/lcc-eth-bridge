#pragma once
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
// ===================== WT32-ETH01 board map (classic ESP32) =====================
// UNTESTED on hardware. Classic ESP32 + LAN8720 PHY. The esp32 ETH_ESP32_EMAC_DEFAULT_CONFIG
// already matches this board's RMII wiring: MDC=23, MDIO=18, 50 MHz clock IN on GPIO0.
// TWAI (CAN) -> 3.3 V transceiver. GPIO32/33 are free (Ethernet uses 0,18,19,21,22,23,25,26,27).
#define BOARD_TWAI_TX_GPIO      33   // header pin "485_EN" on many boards
#define BOARD_TWAI_RX_GPIO      32   // header pin "CFG" on many boards
#define BOARD_ETH_PHY_ADDR      1
#define BOARD_ETH_PHY_RST_GPIO  -1   // no PHY reset line
#define BOARD_ETH_OSC_EN_GPIO   16   // enables the board's 50 MHz oscillator – drive high before EMAC init
#define BOARD_ETH_PHY_LAN87XX   1    // main.c: LAN8720 instead of IP101
#define BOARD_TWAI_BRP          40   // TWAI clock = 80 MHz APB / 40 = 2 MHz quanta (P4: 40 MHz / 20)

#else
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
#define BOARD_ETH_PHY_RST_GPIO  51   // PHY reset pin (-1 = no reset line) – check your board's schematic
// MDC/MDIO/RMII pins come from the esp32p4 ETH_ESP32_EMAC_DEFAULT_CONFIG (MDC=31,
// MDIO=52) — the P4 devkit map, which matches the "31/52/51/50" schematic hint.
// If the board uses a PHY other than IP101, change esp_eth_phy_new_ip101() in main.c.
// If link fails, override emac_cfg.smi_gpio / .emac_dataif_gpio in main.c to match.
#define BOARD_TWAI_BRP          20   // 40 MHz XTAL / 20 = 2 MHz quanta
#endif
