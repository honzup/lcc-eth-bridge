# lcc-eth-bridge

ESP-IDF firmware for the **Waveshare ESP32-P4-ETH** that bridges a physical
**LCC / OpenLCB CAN bus** to **JMRI** over Ethernet.

The ESP32 is a dumb wire: it moves CAN frames to and from a **GridConnect TCP
server on port 12021**. It is not an OpenLCB node – all the OpenLCB protocol
work happens in JMRI. Think of it as the Ethernet equivalent of a USB-CAN
adapter such as the MERG CANUSB4.

**Status:** in daily use on a layout with several OpenMRNLite nodes, JMRI 5.16.

```
  LCC bus (RJ45, 125 kbit/s CAN)
        │  CAN_H / CAN_L
   CAN interface board (TCAN332 transceiver, 3.3 V – no level shifter)
        │  GPIO20 = TX,  GPIO21 = RX
   ESP32-P4-ETH  ──Ethernet──▶  LAN  ──▶  JMRI (OpenLCB, GridConnect over TCP :12021)
```

## Quick start

**You need:** a Waveshare ESP32-P4-ETH, a 3.3 V CAN transceiver (board or
module), a USB-C cable, an Ethernet connection to the same LAN as JMRI, and a
Linux/macOS/Windows PC for building.

1. **Install ESP-IDF v5.5** (once). On Linux/macOS:
   ```bash
   git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
   ~/esp/esp-idf/install.sh esp32p4
   ```
   On Windows use Espressif's ESP-IDF installer and pick v5.5.
2. **Get the code:**
   ```bash
   git clone <this repository's URL> lcc-eth-bridge
   cd lcc-eth-bridge
   . ~/esp/esp-idf/export.sh        # in every new terminal
   idf.py set-target esp32p4
   ```
3. **Check your chip revision.** Plug the board in by USB and run
   `esptool.py --chip esp32p4 -p /dev/ttyACM0 chip_id` (the port may differ;
   on Windows it's a `COMx` port). If it reports **revision v3.x**, delete the
   two `CONFIG_ESP32P4_...REV...` lines from `sdkconfig.defaults` and then delete
   the generated `sdkconfig` file. Leave them for a **v1.x** board. See
   **Chip revision** below.
4. **Choose an OTA token** (any password-like string). It enables the status
   page and network updates. Keep it in a file:
   ```bash
   mkdir -p ~/.config && echo -n '<your-secret>' > ~/.config/lcc-eth-bridge-ota.token
   chmod 600 ~/.config/lcc-eth-bridge-ota.token
   ```
5. **Build and flash over USB:**
   ```bash
   OTA_TOKEN=$(cat ~/.config/lcc-eth-bridge-ota.token) idf.py build
   idf.py -p /dev/ttyACM0 -b 460800 flash monitor
   ```
   Exit the monitor with `Ctrl+]`.
6. **Wire the transceiver** with the power off – see **Hardware and wiring**.
   Termination goes only at the two ends of the bus.
7. **Connect Ethernet and power up.** The console shows `ETH got IP: <address>`.
   Give that address a DHCP reservation in your router.
8. **Check the bridge:** open `http://<address>/` in a browser. You should see
   `can_state: ACTIVE` and zero drops.
9. **Connect JMRI:** Preferences → Connections → add **OpenLCB**, connection
   **CAN via GridConnect Network Interface**, host `<address>`, port **12021**,
   then restart JMRI. Your nodes should appear under **OpenLCB → Open LccPro**
   (or **OpenLCB → Configure Nodes**).

Later firmware updates go over the network: `scripts/ota-push.sh <address>`
(see **OTA updates**).

## Hardware and wiring

Any 3.3 V CAN transceiver works (TCAN332, TCAN33x, SN65HVD230 …) – it is
transparent to the ESP32's TWAI (CAN) controller. This build uses my own CAN
interface board (TCAN332, two RJ45s, termination jumper, test points and
power/TX/RX LEDs), connected by a 4-pin header:

| ESP32-P4 | Interface board / transceiver | Note |
|----------|-------------------------------|------|
| GND      | GND (header pin 1)            | must be wired – the board is powered from the ESP32 |
| 3.3V     | 3V3 (header pin 2)            | 3.3 V, **not** 5 V |
| GPIO20   | TX → transceiver TXD (pin 3)  | |
| GPIO21   | RX ← transceiver RXD (pin 4)  | |

The pins are set in `main/board_config.h` (`BOARD_TWAI_TX_GPIO` / `BOARD_TWAI_RX_GPIO`).

**Termination:** 120 Ω across CAN_H/CAN_L at the two physical ends of the bus
only. With the power off you should measure about 60 Ω between CAN_H and CAN_L
(both ends terminated); about 120 Ω means only one end is. Many ready-made
SN65HVD230 modules have a 120 Ω resistor permanently fitted – remove it on any
module that isn't at an end.

Give the board a DHCP reservation so JMRI always finds it at the same address.

## Build – ESP-IDF v5.5, node-based TWAI driver

Uses the node-based `esp_twai` driver (IDF 5.5 and later).

```bash
. $IDF_PATH/export.sh            # wherever your ESP-IDF v5.5 is installed
idf.py set-target esp32p4
OTA_TOKEN=<your-secret> idf.py build
idf.py -p /dev/ttyACM0 -b 460800 flash monitor
```

`OTA_TOKEN` is optional, but it also switches on the status page – see
**Status page** and **OTA updates** below.

### Chip revision – the one setting that depends on your board

ESP32-P4 rev < 3.0 (early engineering samples) and rev ≥ 3.0 (production) need
**different builds**. `sdkconfig.defaults` here targets the **v1.x sample** this
project runs on:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y   # enable pre-v3.0 support
CONFIG_ESP32P4_REV_MIN_100=y           # min rev v1.0 (covers v1.3)
```

For a **production rev v3.0+ P4**, delete those two lines (the default is
rev v3.1). Do **not** `--force` a v3.x build onto a v1.x chip or the other way
round – the bootloader hangs with no output.

The serial console (UART on GPIO37/38 via the onboard CH343) shows:

```
main: ETH link UP (PHY negotiated)
bridge: GridConnect server on :12021
main: ETH got IP: <address>  -> point JMRI ... port 12021
```

## JMRI

Preferences → Connections → add **OpenLCB**, system connection
**CAN via GridConnect Network Interface**, host = the bridge's IP address,
port **12021**.

To let WiFi/TCP nodes and tools join the same network, start JMRI's own hub:
**OpenLCB menu → Start Hub**. JMRI then forwards their traffic to and from the
CAN bus through the bridge.

## How it's made robust

A naïve bridge works for simple traffic, then fails on long configuration
writes or at start of day (JMRI connecting and every node answering at once).
Each of these fixed a real failure:

1. **Dedicated CAN transmit task.** The IDF 5.5 TWAI driver keeps a *pointer* to
   the frame you hand it, not a copy. Passing a frame built on the stack caused
   memory corruption and a crash/reset every few seconds. Frames now live in a
   queue owned by a transmit task until the driver has sent them.
2. **Deep queue, no silent drops.** The network → CAN queue holds 1024 frames
   (`TX_Q_LEN`), so a burst from JMRI is never thrown away. Any drop that does
   happen is counted and shown on the status page.
3. **Pacing during bursts.** JMRI can send a multi-frame datagram much faster
   than a small node can empty its CAN receive buffer. A middle frame was lost
   and the node silently stored a shortened value (e.g. a node name). While more
   frames are queued the bridge now waits 2.5 ms between frames
   (`CAN_TX_PACE_US`); single frames still go straight out.
4. **Explicit bit timing.** Setting only "125 kbit/s" let IDF choose an 85 %
   sample point. OpenMRNLite nodes use the standard 75 % sample point, SJW 3
   (16 time quanta), so the bridge now sets exactly that. The mismatch showed
   up as bus errors whenever many nodes transmitted together.
5. **Bus-off recovery.** If the controller goes bus-off (e.g. transmitting on a
   broken bus), a recovery task restarts it automatically instead of needing a
   power cycle.
6. **Accept-all receive filter.** LCC uses 29-bit extended IDs; without an
   explicit accept-all filter no bus traffic reached JMRI.

## Status page

`GET http://<bridge>/` returns plain text: firmware version and running slot,
`tx_drops` / `rx_drops`, queue depths (now / max / size), CAN state
(ACTIVE / PASSIVE / BUS_OFF), TX/RX error counters, real bus errors broken down
by type (bit / form / stuff / ack) and `arb_lost`.

Lost arbitration is normal CAN behaviour (two nodes start at once and one backs
off), so `arb_lost` rising is not a fault. Rising `bus_err` or any drops are.

The web server only starts when the firmware was built with `OTA_TOKEN` set
(safe default: no token, no web server).

## OTA updates

Two OTA slots with automatic rollback (`partitions.csv`,
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Push a new image over the LAN:

```bash
scripts/ota-push.sh [bridge-ip]   # reads the token from ~/.config/lcc-eth-bridge-ota.token
```

which POSTs `build/lcc_eth_bridge.bin` to `/update?key=<token>` and waits for
the bridge to come back.

**Gotcha:** `OTA_TOKEN` is baked in when CMake configures the project, not on
every build. After changing the token run `idf.py fullclean` (or touch
`main/CMakeLists.txt`) before building, or the old token – or none – is
compiled in and you lose OTA until the next USB flash. Always build with the
token set.

## Tests and tools

The GridConnect codec is plain C with a desktop test – no IDF needed:

```bash
gcc -I main test/test_gridconnect.c main/gridconnect.c -o /tmp/gctest && /tmp/gctest
# gridconnect: all tests passed
```

`tools/` has small Python OpenLCB clients (using Bob Jacobsen's
[python-openlcb](https://github.com/bobjacobsen/python-openlcb)) to read node
configuration and to run a long-write regression test – see `tools/README.md`.
Run `write_test.py` against a node after any change to the transmit path.

## Notes

- One TCP client at a time (JMRI) by design; other clients join through the
  JMRI hub.
- A dumb bridge, not an OpenLCB node – use OpenMRN if the bridge itself ever
  needs to be a node.
- Harmless boot warning: the flash chip is 32 MB but only 4 MB is addressed.
- The Ethernet PHY can stall after many rapid resets (no `link UP` after boot).
  A full power cycle fixes it; a reset doesn't.
- A self-test build (`LCC_SELFTEST=1 idf.py build`, after `rm -rf build`)
  loops frames back inside the controller to check the driver and codec with
  nothing wired.

## Licence

MIT – see `LICENSE`.
