#!/bin/bash
# load_pru0.sh — safely (re)load a PRU0 firmware (.out) via remoteproc on the BBB.
# Usage:  sudo ./load_pru0.sh blink.out
#
# Why this exists: pasting the cp/tee/echo steps in one block loses newlines and
# the commands get concatenated (that's what broke your first load). Run them as
# a script instead. This script also tolerates an already-offline core so the
# chain never breaks with "Invalid argument".
set -e

FW="$1"
[ -z "$FW" ] && { echo "usage: sudo $0 <firmware.out>"; exit 1; }
[ -f "$FW" ] || { echo "error: $FW not found"; exit 1; }

RPROC=/sys/class/remoteproc/remoteproc1
NAME=am335x-pru0-fw          # the default firmware name the PRU driver expects

# 1) If already running, stop it FIRST (so the firmware node isn't busy).
if [ "$(cat "$RPROC/state" 2>/dev/null)" = "running" ]; then
    echo "stop  -> $RPROC/state"
    echo stop | sudo tee "$RPROC/state" >/dev/null
    # wait until the core reports offline (max ~2s)
    for i in $(seq 1 20); do
        [ "$(cat "$RPROC/state" 2>/dev/null)" = "offline" ] && break
        sleep 0.1
    done
fi

# 2) Put the binary where the kernel looks for it.
sudo cp "$FW" "/lib/firmware/$NAME"

# 3) Tell remoteproc which firmware file to load (matches the filename above).
echo "$NAME" | sudo tee "$RPROC/firmware" >/dev/null

# 4) Boot the PRU.
echo "start -> $RPROC/state"
echo start | sudo tee "$RPROC/state" >/dev/null

echo "----------------------------------------"
echo "state : $(cat "$RPROC/state")"
echo "dmesg :"; sudo dmesg | tail -4
