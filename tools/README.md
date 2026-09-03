# tools — OpenLCB diagnostics for the LCC fleet

Small Python OpenLCB clients that talk to the fleet nodes **through the JMRI hub**
(or directly to this bridge's GridConnect TCP port) to read/write node memory and
read SNIP. Written while root-causing the multi-frame CDI-write truncation bug
(see the bridge commit that added `can_tx_task` pacing); kept as a permanent
regression check and diagnostic.

## The bug these caught

Long config-string writes (e.g. node names) were silently truncated: the bridge
delivered a datagram's CAN frames back-to-back faster than an OpenMRNLite node
serviced its TWAI RX FIFO, so a **middle frame was dropped** and the
sequence-number-less reassembler stitched the survivors into a short write
(`Node#3 16 Servo/Frog` -> `16rog`). It was systemic (all nodes), not WiFi, not
JMRI. Fix = a 2.5 ms inter-frame gap in the bridge's TX, only during multi-frame
bursts.

## Dependency

Bob Jacobsen's **python-openlcb** (not on PyPI). Get it once:

    git clone https://github.com/bobjacobsen/python-openlcb ~/python-openlcb

`lcc_hub.py` finds it at `~/python-openlcb`, next to this repo, or via
`OPENLCB_PATH`. No other deps (pure Python).

## Config

- `LCC_HUB` — hub address, default `192.168.1.46:12021` (the JMRI OpenLCB hub).
  JMRI's OpenLCB connection must have **Start Hub** running.
- `LCC_LOCAL_ID` — our client's node id, default a random `05.01.01.*` (won't
  collide with the `03.04.*` fleet).

## Tools

    python3 write_test.py <node-id>              # regression: does a long write still truncate?
    python3 read_config.py <node-id> [<id> ...]  # dump SNIP + stored name/description

`write_test.py` writes a distinctive 24-char string to the node's user name,
reads it back (**PASS** = clean, **FAIL** = a frame was lost), and restores the
original name. Run it against any node after touching the bridge TX path to
confirm the pacing still holds:

    python3 write_test.py 03.04.00.1E.48.03
    #   RESULT: PASS (clean)

`read_config.py` shows the node's live SNIP **and** its config-space name/desc —
if those two disagree it's a node problem; if they agree but JMRI's list shows
something else, it's JMRI's display cache.

## Fleet node ids

| node | id | repo |
|------|----|----|
| #1 8-out/8-in | `03.04.00.1E.48.01` | `john/lcc-8out-40action-node` |
| #2 outputs/effects | `03.04.00.1E.48.02` | `john/lcc-out-node` |
| #3 servo/frog | `03.04.00.1E.48.03` | `john/lcc-servo-node` |
| #4 railpoint (WiFi) | `03.04.00.1E.48.04` | `john/lcc-railpoint` |
