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
# The firmware drives P9_16 via the PRU's DIRECT GPO (__R30), which needs no
# OCP / no SYSCFG / no STANDBY. The only pin setup required is muxing P9_16 to
# PRU mode (mode 5 = pr1_pru0_pru_r30_5) - done at RUNTIME below via devmem2
# (no reboot, no DT overlay). See pru_servo.c header for the rationale.
set -e

PRU="$1"
FW="$2"
[ -z "$PRU" ] && { echo "usage: sudo $0 <pru0|pru1> <firmware.out>"; exit 1; }
[ -z "$FW"  ] && { echo "usage: sudo $0 <pru0|pru1> <firmware.out>"; exit 1; }
[ -f "$FW"  ] || { echo "error: $FW not found"; exit 1; }

# On 6.x the remoteproc node /name is the DT address, not "pruN":
#   pru0 -> 4a334000.pru   pru1 -> 4a338000.pru   (so match by address)
case "$PRU" in
    pru0) FWNAME=am335x-pru0-fw; PAT="4a334000" ;;
    pru1) FWNAME=am335x-pru1-fw; PAT="4a338000" ;;
    *)    echo "error: PRU must be pru0 or pru1"; exit 1 ;;
esac

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

# 1) If already running, stop it FIRST (so the firmware node isn't busy).
if [ "$(cat "$RPROC/state" 2>/dev/null)" = "running" ]; then
    echo "stop  -> $RPROC/state"
    echo stop | sudo tee "$RPROC/state" >/dev/null
    for i in $(seq 1 20); do
        [ "$(cat "$RPROC/state" 2>/dev/null)" = "offline" ] && break
        sleep 0.1
    done
fi

# 1b) Mux P9_16 to PRU0 R30 mode (runtime, no reboot).
#     P9_16 = conf_gpmc_be1n = pad 0x44E10984. Mode 5 -> pr1_pru0_pru_r30_5.
#     (The PRU's OCP master cannot reach GPIO0 on this image, so __R30 is used.)
if [ "$PRU" = "pru0" ]; then
    if command -v devmem2 >/dev/null 2>&1; then
        echo "mux   -> P9_16 (0x44E10984) = mode 5 (PRU0 R30_5)"
        sudo devmem2 0x44E10984 w 0x05 >/dev/null
    else
        echo "WARN  : devmem2 not found - P9_16 must already be muxed to mode 5."
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
