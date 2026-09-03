#!/usr/bin/env python3
"""Regression check for the multi-frame CDI-write truncation bug (fixed by the
bridge's can_tx_task pacing). Writes a distinctive 24-char string to a node's
user-name field (a multi-frame datagram), reads it back, and restores the
original name.

    PASS  -> read back matches  -> bridge pacing is working
    FAIL  -> a middle CAN frame was dropped -> truncation regression

Usage:  python3 write_test.py <node-id>
        python3 write_test.py 03.04.00.1E.48.03
Env:    LCC_HUB (default 192.168.1.46:12021), OPENLCB_PATH (see lcc_hub.py)
"""
import sys, time
from lcc_hub import Hub, cstr

NAME_ADDR = 1               # user name lives at config offset 1 (after the 1-byte version)
PROBE = "ABCDEFGHIJKLMNOPQRSTUVWX"   # 24 distinct chars -> a lost frame is obvious

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    node = sys.argv[1]
    hub = Hub()
    try:
        if not hub.discover(node):
            sys.exit("node %s not on the bus (powered? on CAN?)" % node)
        orig = hub.read(node, NAME_ADDR, 40)
        print("node %s  original name: %r" % (node, cstr(orig)))
        hub.write(node, NAME_ADDR, PROBE.encode() + b"\x00")
        time.sleep(0.4)
        got = cstr(hub.read(node, NAME_ADDR, 40))
        print("  wrote: %r" % PROBE)
        print("  read : %r" % got)
        ok = (got == PROBE)
        print("  RESULT:", "PASS (clean)" if ok else "FAIL (truncated -> middle frame lost)")
        if orig is not None:                     # restore the original name
            hub.write(node, NAME_ADDR, bytes(orig[:62]))
        sys.exit(0 if ok else 1)
    finally:
        hub.close()

if __name__ == "__main__":
    main()
