#!/bin/bash
# load_pru0.sh — safely load a PRU0 firmware (.out) via remoteproc on the BBB.
# Usage:  sudo ./load_pru0.sh blink.out
#
# Why this exists: pasting the cp/tee/echo steps in one block loses newlines and
# the commands get concatenated (that's what broke your load). Run them as a
# script instead.
set -e

FW="$1"
[ -z "$FW" ] && { echo "usage: sudo $0 <firmware.out>"; exit 1; }
[ -f "$FW" ] || { echo "error: $FW not found"; exit 1; }

RPROC=/sys/class/remoteproc/remoteproc1
NAME=am335x-pru0-fw          # the default firmware name the PRU driver expects

# 1) Put the binary where the kernel looks for it.
sudo cp "$FW" "/lib/firmware/$NAME"

# 2) Tell remoteproc which firmware file to load (matches the filename above).
echo "$NAME" | sudo tee "$RPROC/firmware"

# 3) If it's already running, stop it before re-starting.
if [ "$(cat "$RPROC/state")" = "running" ]; then
    echo stop | sudo tee "$RPROC/state"
fi

# 4) Boot the PRU.
echo start | sudo tee "$RPROC/state"

echo "----------------------------------------"
echo "state : $(cat "$RPROC/state")"
echo "dmesg :"; sudo dmesg | tail -3
