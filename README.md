# lcc-eth-bridge

ESP-IDF firmware for the **Waveshare ESP32-P4-ETH** that bridges the physical
**LCC / OpenLCB CAN bus** to **JMRI** over Ethernet. The ESP is a dumb wire: it
shuttles CAN frames to/from a **GridConnect TCP server on port 12021**. All
OpenLCB node logic lives in JMRI — this box is the equivalent of an LCC
buffer/gateway, not a node.

```
  LCC bus (RJ45, 125 kbit/s CAN)
        │  CAN_H / CAN_L
     TCAN332DR transceiver  (3.3 V, native — no level shifter)
        │  GPIO17 = TX,  GPIO18 = RX
     ESP32-P4-ETH  ──Ethernet──▶  LAN  ──▶  JMRI (OpenLCB, GridConnect over TCP :12021)
```

## Wiring (TCAN332DR, SOIC-8)

| P4 pin | TCAN332 | Note |
|--------|---------|------|
| GPIO17 | 1 TXD   | |
| GND    | 2 GND   | 100 nF ceramic across pins 3↔2, close to the IC |
| 3.3V   | 3 VCC   | |
| GPIO18 | 4 RXD   | |
| CAN_L  | 6 CANL  | to LCC bus |
| CAN_H  | 7 CANH  | to LCC bus |

Prove the transceiver locally first; connect to the live LCC RJ45 bus last.

## Build / flash

Requires **ESP-IDF v5.5+** (P4 TWAI + the node-based `esp_driver_twai` API).

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

On boot the serial console prints the DHCP address:

```
ETH got IP: 192.168.1.xxx  -> point JMRI OpenLCB GridConnect here, port 12021
```

**Reserve a DHCP lease** for the board's MAC in OPNsense (reservations live at
.180+, outside the pool) so JMRI always finds it at the same IP.

## JMRI

Preferences → Connections → add **OpenLCB**, system connection
**CAN via GridConnect Network Interface**, host = the board's IP, port **12021**.

## Host test (the one bit that's actually verified here)

The GridConnect codec is a pure C module with a desktop test — no IDF needed:

```bash
gcc -I main test/test_gridconnect.c main/gridconnect.c -o /tmp/gctest && /tmp/gctest
# gridconnect: all tests passed
```

## What's NOT verified — read before trusting it

- **The firmware has not been compiled or flashed** (no P4 / IDF here). Only the
  GridConnect codec is proven. Expect to fix a header name or struct-field name
  on first `idf.py build` (e.g. `rx.header.rtr`, `emac_cfg.smi_gpio`,
  `esp_eth_mac_esp32.h`) — these moved across IDF 5.x.
- **`main/board_config.h` Ethernet pins/PHY are guesses** and MUST be checked
  against the P4-ETH schematic. If no DHCP lease appears, that's the cause.
  Fastest ground truth: generate Espressif's own example, get *its* Ethernet
  linking on this board, then copy its exact PHY/pin/clock config here:
  `idf.py create-project-from-example "espressif/esp-idf:examples/ethernet/basic"`.

## Scope / deliberate simplifications

- **Single client** (JMRI). One TCP connection at a time — see the `ponytail:`
  note in `bridge.c` to fan out to multiple.
- **Dumb bridge, not an OpenLCB node.** If you later want the ESP itself to be
  a node (produce/consume events without JMRI), that's OpenMRN territory —
  more code, only worth it if the node logic must live on the ESP.
