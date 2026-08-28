#!/bin/bash
# load_pru.sh — safely (re)load a PRU firmware (.out) via remoteproc on the BBB.
#
# Usage:  sudo ./load_pru.sh <pruN> <firmware.out>
#   <pruN> is pru0 or pru1  (the core you compiled for with -mmcu=am335x.pruN)
#
# Example:
#   sudo ./load_pru.sh pru0 pru1_servo.pru1.out   # (built with -mmcu=am335x.pru0)
#
# Why a script (not pasted commands): pasting the cp/tee/echo steps in one
# block loses newlines and concatenates commands (that broke your first load).
#
# The firmware drives P9_31 via the PRU's DIRECT GPO (__R30 bit 0), which needs
# no OCP / no SYSCFG / no STANDBY. P9_31 = mcasp0_aclkx = pad 0x44E10990, muxed
# to mode 5 (PRU0 R30_0) by U-Boot at boot (see /boot/firmware/uEnv.txt
# "mw.l 0x44E10990 0x05"). On kernel 6.x an ARM devmem2 write to the padconf is
# dropped, so the runtime mux below is a best-effort no-op - the U-Boot mux is
# what actually sticks. See pru_servo.c header for the rationale.
set -e

# 'stop' is a standalone action (no firmware needed).
if [ "$1" = "stop" ]; then
    PRU="${2:-pru0}"          # default to pru0 if no core given
    FW=""                     # not used in stop mode
else
    PRU="$1"
    FW="$2"
    [ -z "$PRU" ] && { echo "usage: sudo $0 <pru0|pru1> <firmware.out>"; exit 1; }
    [ -z "$FW"  ] && { echo "usage: sudo $0 <pru0|pru1> <firmware.out>"; exit 1; }
    [ -f "$FW"  ] || { echo "error: $FW not found"; exit 1; }
fi

# Map PRU core -> firmware name + DT-address pattern, then discover the
# remoteproc sysfs node. This MUST run before any block that touches $RPROC
# (including the 'stop' action below).
case "$PRU" in
    pru0) FWNAME=am335x-pru0-fw; PAT="4a334000" ;;
    pru1) FWNAME=am335x-pru1-fw; PAT="4a338000" ;;
    *)    echo "error: PRU must be pru0 or pru1"; exit 1 ;;
esac

# On 6.x the remoteproc node /name is the DT address, not "pruN":
#   pru0 -> 4a334000.pru   pru1 -> 4a338000.pru   (so match by address)
# Auto-discover the remoteproc node by reading its /name (remoteproc0 is
# usually wkup_m3, NOT a PRU, on this kernel — never hardcode the number).
RPROC=""
for d in /sys/class/remoteproc/*/; do
    if grep -q "$PAT" "$d/name" 2>/dev/null; then
        RPROC="${d%/}"
        break
    fi
done
if [ -z "$RPROC" ]; then
    echo "error: could not find a remoteproc node whose name contains '$PRU'."
    echo "Available nodes:"
    for d in /sys/class/remoteproc/*/; do echo -n "  $d -> "; cat "$d/name" 2>/dev/null; done
    exit 1
fi
echo "Using $RPROC (name: $(cat "$RPROC/name"))"

# Stop mode: just halt the PRU and exit.
if [ "$1" = "stop" ]; then
    if [ "$(cat "$RPROC/state" 2>/dev/null)" = "running" ]; then
        echo "stop  -> $RPROC/state"
        echo stop | sudo tee "$RPROC/state" >/dev/null
        for i in $(seq 1 20); do
            [ "$(cat "$RPROC/state" 2>/dev/null)" = "offline" ] && break
            sleep 0.1
        done
    else
        echo "already stopped (state: $(cat "$RPROC/state" 2>/dev/null))"
    fi
    echo "----------------------------------------"
    echo "state : $(cat "$RPROC/state")"
    exit 0
fi

# 1) If already running, stop it FIRST (so the firmware node isn't busy).
if [ "$(cat "$RPROC/state" 2>/dev/null)" = "running" ]; then
    echo "stop  -> $RPROC/state"
    echo stop | sudo tee "$RPROC/state" >/dev/null
    for i in $(seq 1 20); do
        [ "$(cat "$RPROC/state" 2>/dev/null)" = "offline" ] && break
        sleep 0.1
    done
fi

# 1b) Mux P9_31 to PRU0 R30 mode (runtime best-effort; real mux is U-Boot).
#     P9_31 = mcasp0_aclkx = pad 0x44E10990. Mode 5 -> PRU0 R30_0.
#     NOTE: on kernel 6.x ARM devmem2 writes to the padconf are dropped, so this
#     only helps if the U-Boot mux in /boot/firmware/uEnv.txt is already set.
if [ "$PRU" = "pru0" ]; then
    if command -v devmem2 >/dev/null 2>&1; then
        echo "mux   -> P9_31 (0x44E10990) = mode 5 (PRU0 R30_0)  [best-effort; U-Boot mux is authoritative]"
        sudo devmem2 0x44E10990 w 0x05 >/dev/null
    else
        echo "WARN  : devmem2 not found - P9_31 must already be muxed to mode 5 via U-Boot."
    fi
fi

# 2) Put the binary where the kernel looks for it.
sudo cp "$FW" "/lib/firmware/$FWNAME"

# 3) Tell remoteproc which firmware file to load.
echo "$FWNAME" | sudo tee "$RPROC/firmware" >/dev/null

# 4) Boot the PRU.
echo "start -> $RPROC/state"
echo start | sudo tee "$RPROC/state" >/dev/null

echo "----------------------------------------"
echo "state : $(cat "$RPROC/state")"
echo "dmesg :"; sudo dmesg | tail -4
