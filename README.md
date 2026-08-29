# lcc-eth-bridge

ESP-IDF firmware for the **Waveshare ESP32-P4-ETH** that bridges the physical
**LCC / OpenLCB CAN bus** to **JMRI** over Ethernet. The ESP is a dumb wire: it
shuttles CAN frames to/from a **GridConnect TCP server on port 12021**. All
OpenLCB node logic lives in JMRI — this box is the equivalent of an LCC
buffer/gateway, not a node.

**Status: live.** Boots, links at 100 Mbps full duplex, DHCP-reserved at
**192.168.1.87**, GridConnect server reachable on :12021.

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

## Build — ESP-IDF v5.5, node-based TWAI

Uses the modern node-based `esp_twai` driver (v5.5+). Installed at `~/esp/esp-idf`.

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 -b 460800 flash monitor
```

### Chip revision — the one setting that depends on your silicon

ESP32-P4 rev <3.0 (engineering samples) and rev ≥3.0 (production) are
**mutually exclusive** builds. `sdkconfig.defaults` here targets the **early
v1.x sample** this project runs on:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y   # enable pre-v3.0 support
CONFIG_ESP32P4_REV_MIN_100=y           # min rev v1.0 (covers v1.3)
```

For a **production rev v3.0+ P4**, delete those two lines (the default is
rev v3.1). Do NOT `--force` a v3.x build onto a v1.x chip or vice-versa — the
bootloader hangs silently.

Serial console (UART on GPIO37/38 via the onboard CH343) prints:

```
main: ETH link UP (PHY negotiated)
bridge: GridConnect server on :12021
main: ETH got IP: 192.168.1.87  -> point JMRI ... port 12021
```

Board MAC `e8:f6:0a:e7:20:ef`, DHCP-reserved to **.87** in OPNsense (kept below
the .180+ pool so the reservation isn't squatted).

## JMRI

Preferences → Connections → add **OpenLCB**, system connection
**CAN via GridConnect Network Interface**, host = **192.168.1.87**, port **12021**.

## Host test for the codec

The GridConnect codec is pure C with a desktop test — no IDF needed:

```bash
gcc -I main test/test_gridconnect.c main/gridconnect.c -o /tmp/gctest && /tmp/gctest
# gridconnect: all tests passed
```

## Branches

- **`main`** — IDF 5.5 + node-based TWAI. Runs on this v1.3 board via the pre-v3
  flag above; drop the flag for production v3.x silicon.
- **`idf5.4-legacy-twai`** — IDF 5.4 + legacy `driver/twai.h`, a fallback if
  you're pinned to 5.4 for other reasons. Same behaviour.

## Notes

- Single TCP client (JMRI). See the `ponytail:` note in `bridge.c` to fan out.
- Dumb bridge, not an OpenLCB node — that's OpenMRN territory if ever needed.
- Cosmetic boot warning: flash chip is 32 MB but only 4 MB is addressed (app uses
  <1 MB); harmless.
