# PRU Servo — Self-Study Tutor Guide (eyespies)

> Goal: replace the jittery `gpiod` servo pulse with a **jitter-free PRU0
> firmware**. You (the user) run and load everything on the BeagleBone. The
> files are fully written and commented; read them, then execute.

---

## 0. The mental model (read this first)

- **Servo contract:** a servo wants a **50 Hz pulse** (one every 20 ms). The
  *width* of the HIGH part sets the angle: 0.5 ms = 0°, 1.5 ms = 90°,
  2.5 ms = 180°. Wobble in that width = audible buzz.
- **Why gpiod buzzes:** Linux makes the pulse with a syscall + `nanosleep`.
  The scheduler slips it by ~20 µs → the servo thinks the angle is wobbling.
- **Why the PRU doesn't:** the PRU is a 200 MHz co-processor with **no OS**.
  It flips the pin with `__delay_cycles()` accurate to **5 ns** and never
  calls Linux in the hot loop. (Bonus: it drives the pin in GPIO mode, so we
  need **zero** device-tree / pinmux changes — it sidesteps the wall that
  blocked hardware PWM earlier.)

### Memory = registers (key idea)
On the AM335x, hardware is controlled by writing numbers to addresses.
GPIO bank 0 is at `0x44E07000`. Inside it:

| Offset | Register        | Writing does…                     |
|--------|-----------------|-----------------------------------|
| 0x134  | GPIO_OE         | `0` to a bit → that pin = OUTPUT  |
| 0x194  | GPIO_SETDATAOUT | `1` to a bit → pin HIGH           |
| 0x190  | GPIO_CLEARDATAOUT | `1` to a bit → pin LOW          |

Pins: **P9_16 = GPIO0.19 (tilt, wired)**, **P9_14 = GPIO0.18 (pan, not wired)**.

**Memorize:** PRU = tiny no-OS 200 MHz chip; 1 cycle = 5 ns; servo = 50 Hz,
0.5–2.5 ms; GPIO0 base `0x44E07000`.

---

## 1. Phase 1 — Toolchain & "Hello PRU"

```bash
sudo apt update
sudo apt install gcc-pru pru-software-support-package
ls /sys/class/remoteproc/        # find remoteproc0 / remoteproc1
```

`remoteproc` is the Linux driver that loads PRU programs. **Confirm which
directory is pru0** before loading (see Phase 3 load steps).

## 2. Phase 2 — GPIO from PRU (the blink = the whole foundation)

This IS the blink program in `pru/pru0_servo.pru0.c` built with `-DBLINK_TEST`.
It does exactly one new thing versus normal C: instead of `digitalWrite`, you
**write a number to a memory address** to move the pin. That's it. No Linux
function. That understanding is the foundation for the servo.

Build & load (details in Phase 3). Then **check without extra hardware** (Phase 2
verification below).

## 3. Phase 3 — Cycle-accurate timing

`__delay_cycles(N)` is most accurate with a constant; for a variable delay we
loop fixed chunks (see `delay_us()` in the firmware: 190 cycles × N ≈ N µs;
calibrate once with a stopwatch if you want exactness). 200 MHz → 200 cycles = 1 µs.

## 4. Phase 4 — The servo pulse

`pulse_pin()` / the servo loop in `pru0_servo.pru0.c` holds a pin HIGH for the
angle width, LOW for the rest of 20 ms. Build **without** `-DBLINK_TEST`.

## 5. Phase 5 — ARM ↔ PRU shared memory

The PRU reads `SHM->tilt_us` from its RAM at offset `0x1000`. ARM (`arm_write.c`)
`mmap`s that RAM at physical `0x4A300000`, adds `0x1000`, and writes the angle.
No syscalls in the PRU hot loop. A single 32-bit write is atomic → no lock.

## 6. Phase 6 — Wire into `turret` (later)

Delete the gpiod pulse thread. Keep `v4l2.c`'s angle math. Where it called
`servo_set_angle(...)`, instead write `cmd->tilt_us = angle_to_us(tilt)`.
At startup: `open("/dev/mem")` + `mmap` once; load PRU via remoteproc.

## 7. Phase 7 — Verify & tune

1. Standalone: `arm_write 1500` → servo centers & is **silent** (proves PRU
   killed the buzz).
2. Full `./turret` → tilt tracks you, no buzz, holds when centered.
3. Yocto later: systemd loads PRU before turret auto-starts.

---

## Load the firmware (do this every time, on the BBB, in `pru/`)

```bash
# 1) Build
make blink.out            # for Phase 1/2
# or: make pru0_servo.out # for Phase 4+

# 2) Find which remoteproc is pru0 (pick the one whose name mentions pru0)
ls /sys/class/remoteproc/
cat /sys/class/remoteproc/remoteproc0/name     # expect ...pru0...
# If remoteproc0 is pru0, use RPROC=remoteproc0; else remoteproc1, etc.
RPROC=/sys/class/remoteproc/remoteproc0

# 3) Stop if already running, then load + start
sudo bash -c "echo stop  > $RPROC/state" 2>/dev/null
sudo cp blink.out /lib/firmware/am335x-pru0-fw
sudo bash -c "echo start > $RPROC/state"
cat $RPROC/state                                # should print: running
```

To switch to the real servo later: `sudo cp pru0_servo.out /lib/firmware/am335x-pru0-fw`
then `echo stop; echo start`.

---

## NO-EXTRA-HARDWARE verification

You said you don't have energy to wire an LED. Use these instead:

1. **Firmware loaded?** `cat $RPROC/state` → `running`. If it says `offline`
   or errors, the build/load failed (check `dmesg | tail`).
2. **Blink test (Phase 2):** load `blink.out` with the **tilt servo** plugged
   into P9_16. The servo will **click between two positions once per second**
   (1 Hz). That proves the PRU controls the pin. No LED needed.
   (Or: multimeter on P9_16 in DC volts jumps 0 V ↔ 3.3 V each second.)
3. **Servo test (Phase 4):** load `pru0_servo.out`, then
   `sudo ./arm_write 1500` → servo snaps to center and is **dead silent**
   (compare: the old gpiod version buzzes). `sudo ./arm_write 1000` → moves
   to ~45°; `sudo ./arm_write 2000` → ~135°. Silent the whole time = win.

---

## Cheat sheet (keep open)

```
PRU clock ........ 200 MHz  -> 1 cycle = 5 ns, 200 cyc = 1 us
GPIO0 base ....... 0x44E07000
  OE  offset ..... 0x134   (0 = output)
  SET offset ..... 0x194   (write 1 -> high)
  CLR offset ..... 0x190   (write 1 -> low)
P9_16 = GPIO0.19  (tilt, WIRED)
P9_14 = GPIO0.18  (pan, NOT wired)
Servo: 50 Hz, 20 ms period, 500-2500 us = 0-180 deg
PRU0 DRAM: PRU@0x0 / ARM phys 0x4A300000, SHM at +0x1000
Load: cp .out ->/lib/firmware/am335x-pru0-fw ; echo start ->remoteprocN/state
```

## Homework (do ONLY this, then report back)
1. `sudo apt install gcc-pru pru-software-support-package`
2. `ls /sys/class/remoteproc/` and tell me which `remoteprocN` is **pru0**
   (run `cat .../name` to confirm).
3. `make blink.out`, load it, confirm the tilt servo clicks once per second
   (Phase 2 check). Report what you see.
