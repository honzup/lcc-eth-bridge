#!/usr/bin/env python3
"""Dump a fleet node's identity as seen two ways, to spot node-vs-JMRI-vs-bridge
issues: its live SNIP reply (what JMRI's node list shows) AND its stored config
user name/description read straight from config space. If these disagree, the
problem is on the node; if they agree but JMRI shows something else, it's JMRI's
display cache.

Usage:  python3 read_config.py <node-id> [<node-id> ...]
        python3 read_config.py 03.04.00.1E.48.01 03.04.00.1E.48.02 03.04.00.1E.48.03
Env:    LCC_HUB (default 192.168.1.46:12021), OPENLCB_PATH (see lcc_hub.py)
"""
import sys
from lcc_hub import Hub, cstr

NAME_ADDR, DESC_ADDR = 1, 64   # config-space offsets of the UserInfoSegment strings

def main():
    nodes = sys.argv[1:]
    if not nodes:
        sys.exit(__doc__)
    hub = Hub()
    try:
        for node in nodes:
            print("=== %s ===" % node)
            if not hub.discover(node):
                print("  not on the bus"); continue
            s = hub.snip(node)
            if s:
                print("  SNIP   : model=%r  name=%r  desc=%r"
                      % (s.modelName, s.userProvidedNodeName, s.userProvidedDescription))
            else:
                print("  SNIP   : no reply")
            print("  config : name=%r  desc=%r"
                  % (cstr(hub.read(node, NAME_ADDR, 62)), cstr(hub.read(node, DESC_ADDR, 63))))
    finally:
        hub.close()

if __name__ == "__main__":
    main()
