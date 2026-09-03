"""Minimal OpenLCB client helper for talking to fleet nodes THROUGH the JMRI hub
(or directly to the bridge's GridConnect TCP port). Read/write node memory + read
SNIP, for diagnosing/verifying the CAN datagram path.

Dependency: bobjacobsen's python-openlcb (NOT on PyPI). Get it with:
    git clone https://github.com/bobjacobsen/python-openlcb
and either put it next to this repo, at ~/python-openlcb, or set OPENLCB_PATH.

Hub address: env LCC_HUB (default 192.168.1.46:12021 = the JMRI OpenLCB hub).
Our local node id defaults into the 05.01.01 example range (won't collide with
the 03.04.* fleet); override with env LCC_LOCAL_ID.
"""
import os, sys, time, threading

for _p in (os.environ.get("OPENLCB_PATH"),
           os.path.expanduser("~/python-openlcb"),
           os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-openlcb")):
    if _p and os.path.isdir(os.path.join(_p, "openlcb")):
        sys.path.insert(0, _p)
        break
try:
    from openlcb import precise_sleep
    from openlcb.tcplink.tcpsocket import TcpSocket
    from openlcb.canbus.canphysicallayergridconnect import CanPhysicalLayerGridConnect
    from openlcb.canbus.canlink import CanLink
    from openlcb.nodeid import NodeID
    from openlcb.datagramservice import DatagramService
    from openlcb.memoryservice import MemoryReadMemo, MemoryWriteMemo, MemoryService
    from openlcb.message import Message
    from openlcb.mti import MTI
    from openlcb.snip import SNIP
except ImportError:
    sys.exit("python-openlcb not found. Clone https://github.com/bobjacobsen/python-openlcb "
             "and set OPENLCB_PATH (see lcc_hub.py header).")

CONFIG_SPACE = 253   # 0xFD
CDI_SPACE = 255      # 0xFF


class Hub:
    def __init__(self):
        host, _, port = (os.environ.get("LCC_HUB", "192.168.1.46:12021")).partition(":")
        local = os.environ.get("LCC_LOCAL_ID", "05.01.01.01.7A." + format(os.getpid() % 250 + 1, "02X"))
        self._sock = TcpSocket(); self._sock.connect(host, int(port or 12021))
        self._phys = CanPhysicalLayerGridConnect()
        self._link = CanLink(self._phys, NodeID(local))
        self._dg = DatagramService(self._link)
        self._link.registerMessageReceivedListener(self._dg.process)
        self._link.registerMessageReceivedListener(self._on_msg)
        self._mem = MemoryService(self._dg)
        self._snip = {}
        self._phys.physicalLayerUp()
        t = time.time()
        while self._link.pollState() != CanLink.State.Permitted and time.time() - t < 8:
            self._pump1()
        if self._link.pollState() != CanLink.State.Permitted:
            sys.exit("could not bring the link up (hub reachable? %s)" % os.environ.get("LCC_HUB", "192.168.1.46:12021"))

    def _pump1(self):
        self._phys.receiveAll(self._sock); self._phys.sendAll(self._sock); precise_sleep(.01)

    def pump(self, secs):
        t = time.time()
        while time.time() - t < secs:
            self._pump1()

    def _on_msg(self, m):
        if m.mti == MTI.Simple_Node_Ident_Info_Reply:
            s = SNIP(); s.addData(bytearray(m.data)); s.updateStringsFromSnipData()
            self._snip[str(m.source)] = s

    def discover(self, nodeid, timeout=16):
        """Prompt the node to announce its alias so the lib can address it."""
        far = NodeID(nodeid); t = time.time(); last = 0
        while time.time() - t < timeout and far not in self._link.nodeIdToAlias:
            if time.time() - last > 2:
                self._link.sendMessage(Message(MTI.Verify_NodeID_Number_Global, self._link.localNodeID, None))
                last = time.time()
            self._pump1()
        return far in self._link.nodeIdToAlias

    def read(self, nodeid, addr, count, space=CONFIG_SPACE, timeout=4):
        r = {}; ev = threading.Event()
        self._mem.requestMemoryRead(MemoryReadMemo(
            NodeID(nodeid), count, space, addr,
            lambda m: (r.__setitem__('e', 1), ev.set()),
            lambda m: (r.__setitem__('d', bytes(m.data)), ev.set())))
        t = time.time()
        while not ev.is_set() and time.time() - t < timeout:
            self._pump1()
        return r.get('d')

    def write(self, nodeid, addr, data, space=CONFIG_SPACE, timeout=4):
        r = {}; ev = threading.Event()
        self._mem.requestMemoryWrite(MemoryWriteMemo(
            NodeID(nodeid),
            lambda m: (r.__setitem__('ok', 1), ev.set()),
            lambda m: (r.__setitem__('rej', 1), ev.set()),
            len(data), space, addr, bytearray(data)))
        t = time.time()
        while not ev.is_set() and time.time() - t < timeout:
            self._pump1()
        return 'ok' in r

    def snip(self, nodeid, timeout=3):
        self._snip.pop(nodeid, None)
        self._link.sendMessage(Message(MTI.Simple_Node_Ident_Info_Request,
                                       self._link.localNodeID, NodeID(nodeid)))
        t = time.time()
        while nodeid not in self._snip and time.time() - t < timeout:
            self._pump1()
        return self._snip.get(nodeid)

    def close(self):
        self._phys.physicalLayerDown()


def cstr(b):
    """Config string field -> python str (stops at the first NUL)."""
    return None if b is None else b.split(b'\x00')[0].decode('ascii', 'replace')
