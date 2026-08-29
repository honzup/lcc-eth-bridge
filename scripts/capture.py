#!/usr/bin/env python3
# Reset the board via RTS and capture ~14s of console output at 115200.
import serial, time, sys
p = serial.Serial('/dev/ttyACM0', 115200, timeout=0.2)
p.setDTR(False); p.setRTS(True); time.sleep(0.12); p.setRTS(False)  # hard-reset pulse
t0 = time.time()
while time.time() - t0 < 10:
    d = p.read(4096)
    if d:
        sys.stdout.buffer.write(d); sys.stdout.flush()
p.close()
