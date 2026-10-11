#!/usr/bin/env bash
# Push build/lcc_eth_bridge.bin to the bridge over LAN OTA (POST /update?key=<token>).
# Token is read from ~/.config/lcc-eth-bridge-ota.token and never printed.
set -euo pipefail
HOST=${1:-192.168.1.87}
BIN="$(dirname "$0")/../build/lcc_eth_bridge.bin"
TOKEN_FILE=~/.config/lcc-eth-bridge-ota.token
[ -f "$BIN" ] || { echo "no $BIN – build first"; exit 1; }
echo "Before: $(curl -s -m4 "http://$HOST/" | grep -E '^version|bus_err' | tr '\n' ' ')"
curl -sS -m120 --fail-with-body -X POST --data-binary @"$BIN" \
  -H 'Content-Type: application/octet-stream' \
  "http://$HOST/update?key=$(cat "$TOKEN_FILE")" ; echo
echo "Waiting for reboot..."
for i in $(seq 1 30); do
  sleep 2
  if out=$(curl -s -m3 "http://$HOST/"); then echo "$out"; exit 0; fi
done
echo "bridge did not come back within 60 s – check it (rollback should restore the old image on the next reset)"
exit 1
