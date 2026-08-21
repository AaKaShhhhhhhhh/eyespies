# PRU Servo — Self-Study Tutor Guide (eyespies)

> Goal: replace the jittery `gpiod` servo pulse with a **jitter-free PRU0
> firmware**. You (the user) run and load everything on the BeagleBone. The
> files are fully written and commented; read them, then execute.
>
> You said two things that shape this guide:
>   1. You have **no LED** to test with → every check below works WITHOUT an
>      LED (your tilt servo, plugged into P9_16, is your "LED" — it clicks).
>   2. You want to know **where these magic addresses come from** so you could
>      find them yourself with no one's help → see section 0b.

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

## 0b. Where do these magic addresses come from? (read this — it's your question)

If you had **no me, no internet helper, just the chip and a PDF**, here is the
exact paper trail. Everything in the firmware is copied from two documents:

### Source A — AM335x Technical Reference Manual (TRM)
This is Texas Instruments' 5000-page datasheet for the AM335x chip (the CPU on
the BeagleBone Black). Search "AM335x TRM SPRUH73" and download the PDF.

From it you get:
- **GPIO0 base `0x44E07000`** → TRM chapter **"Memory Map"** (the table of
  peripheral base addresses). It lists `GPIO0` at `0x44E0_7000`, `GPIO1` at
  `0x4804_C000`, etc.
- **The register offsets `0x134 / 0x194 / 0x190`** → TRM chapter **25
  "General-Purpose Input/Output (GPIO)"**, the register summary table. There
  you see:
  - `GPIO_OE`            offset `0x134`
  - `GPIO_DATAIN`        offset `0x138` (read pin)
  - `GPIO_DATAOUT`       offset `0x13C` (write whole value)
  - `GPIO_CLEARDATAOUT`  offset `0x190` (write 1 → pin LOW)
  - `GPIO_SETDATAOUT`    offset `0x194` (write 1 → pin HIGH)
- **PRU0 DRAM `0x4A300000`** → TRM chapter **"PRU-ICSS"** memory map. The PRU
  subsystem's Data RAM 0 sits at `0x4A30_0000` (the PRU itself sees it at
  local address `0x0000_0000`; ARM sees it at `0x4A300000`).

### Source B — BeagleBone Black System Reference Manual (SRM)
This is the board's own manual (beagleboard.org). Its **P8/P9 header tables**
tell you which physical pin is which GPIO:
- `P9_16` → `GPIO0_19`  (our tilt servo)
- `P9_14` → `GPIO0_18`  (our pan servo, not wired)

So `GPIO0.19` = bit `19` inside the GPIO0 registers, and `GPIO0.18` = bit `18`.
That's where `BIT_TILT = (1<<19)` and `BIT_PAN = (1<<18)` come from.

### Source C (no PDF needed) — the device tree on the board itself
If you're *on* the BBB, you can read the addresses the kernel already uses:
```bash
# Where the PRU subsystem lives (look at the "reg" line):
cat /proc/device-tree/ocp/pruss@4a300000/reg 2>/dev/null | hexdump -C
# Which gpiochip is GPIO0 and its base address:
ls /sys/class/gpio/            # or: gpioinfo  (libgpiod)
cat /sys/class/gpio/gpiochip0/label   # usually "gpio-0-31" = GPIO0 bank
```
The device tree is just TI's TRM numbers written into a file the kernel reads.

**The point:** none of these numbers are "my" invention. They are in the TRM
(A for chip internals) and the SRM (B for pin labels). With those two PDFs you
re-derive every `#define` in `pru0_servo.pru0.c` yourself.

---

## 1. Phase 1 — Toolchain & "Hello PRU"

```bash
sudo apt update
sudo apt install gcc-pru pru-software-support-package
```

Now see what coprocessors Linux can load:
```bash
ls /sys/class/remoteproc/
# On your kernel this prints something like:
#   remoteproc0  remoteproc1  remoteproc2
```

**IMPORTANT (you already found this):** `remoteproc0` is **NOT** the PRU.
Its name is `wkup_m3` — that's the AM335x power-management M3 core, a totally
different coprocessor. The PRU appears as the *other* entries. To find which
`remoteprocN` is pru0, run:
```bash
for d in /sys/class/remoteproc/*/; do echo -n "$d -> "; cat "$d/name"; done
```
Look for the line whose name contains **`pru0`** (e.g. `4a334000.pru0` or
`pru0`). That `N` is your PRU. Example output:
```
/sys/class/remoteproc/remoteproc0/ -> wkup_m3
/sys/class/remoteproc/remoteproc1/ -> 4a334000.pru0      <-- THIS is pru0
/sys/class/remoteproc/remoteproc2/ -> 4a338000.pru1
```
So on your board **pru0 = remoteproc1**. Use that number everywhere below.

---

## 2. Phase 2 — GPIO from PRU (the blink = the whole foundation)

This IS the blink program in `pru/pru0_servo.pru0.c` built with `-DBLINK_TEST`.
It does exactly one new thing versus normal C: instead of `digitalWrite`, you
**write a number to a memory address** to move the pin. That's it. No Linux
function. That understanding is the foundation for the servo.

Build & load (details in "Load the firmware" below). Then **check without extra
hardware** (Phase 2 verification below).

---

## 3. Phase 3 — Cycle-accurate timing

`__delay_cycles(N)` is most accurate with a constant; for a variable delay we
loop fixed chunks (see `delay_us()` in the firmware: 190 cycles × N ≈ N µs;
calibrate once with a stopwatch if you want exactness). 200 MHz → 200 cycles = 1 µs.

---

## 4. Phase 4 — The servo pulse

`pulse_pin()` / the servo loop in `pru0_servo.pru0.c` holds a pin HIGH for the
angle width, LOW for the rest of 20 ms. Build **without** `-DBLINK_TEST`.

---

## 5. Phase 5 — ARM ↔ PRU shared memory

The PRU reads `SHM->tilt_us` from its RAM at offset `0x1000`. ARM (`arm_write.c`)
`mmap`s that RAM at physical `0x4A300000`, adds `0x1000`, and writes the angle.
No syscalls in the PRU hot loop. A single 32-bit write is atomic → no lock.

---

## 6. Phase 6 — Wire into `turret` (later)

Delete the gpiod pulse thread. Keep `v4l2.c`'s angle math. Where it called
`servo_set_angle(...)`, instead write `cmd->tilt_us = angle_to_us(tilt)`.
At startup: `open("/dev/mem")` + `mmap` once; load PRU via remoteproc.

---

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

# 2) Find the REAL pru0 (NOT remoteproc0 if that is wkup_m3!)
for d in /sys/class/remoteproc/*/; do echo -n "$d -> "; cat "$d/name"; done
# Suppose the line with pru0 is remoteproc1:
RPROC=/sys/class/remoteproc/remoteproc1

# 3) Stop if already running, then load + start
sudo bash -c "echo stop  > $RPROC/state" 2>/dev/null
sudo cp blink.out /lib/firmware/am335x-pru0-fw
sudo bash -c "echo start > $RPROC/state"
cat $RPROC/state                                # should print: running
dmesg | tail -n 5                               # should show remoteproc loading am335x-pru0-fw
```

To switch to the real servo later: `sudo cp pru0_servo.out /lib/firmware/am335x-pru0-fw`
then `echo stop; echo start`.

---

## NO-LED verification (you don't have an LED — use these)

1. **Firmware loaded? (pure software)**
   - `cat $RPROC/state` → must print `running`.
   - `dmesg | tail` → should show the kernel loading `am335x-pru0-fw` and
     "remote processor pru0 is now up". If it errors, the build/load failed.
2. **Pin actually toggling? (no LED, no multimeter — use the servo)**
   Your **tilt servo is already plugged into P9_16**. Load `blink.out` and the
   servo **clicks between two positions once per second** (1 Hz). That click
   IS your blink indicator — the PRU is driving the pin. No LED required.
   (If you ever get a multimeter: DC volts on P9_16 jumps 0 V ↔ 3.3 V each
   second.)
3. **Servo test (Phase 4):** load `pru0_servo.out`, then
   `sudo ./arm_write 1500` → servo snaps to center and is **dead silent**
   (compare: the old gpiod version buzzes). `sudo ./arm_write 1000` → moves
   to ~45°; `sudo ./arm_write 2000` → ~135°. Silent the whole time = win.

---

## Cheat sheet (keep open)

```
PRU clock ........ 200 MHz  -> 1 cycle = 5 ns, 200 cyc = 1 us
GPIO0 base ....... 0x44E07000        (from TRM memory map)
  OE  offset ..... 0x134   (0 = output)          (TRM ch.25)
  SET offset ..... 0x194   (write 1 -> high)     (TRM ch.25)
  CLR offset ..... 0x190   (write 1 -> low)      (TRM ch.25)
P9_16 = GPIO0.19  (tilt, WIRED)      (from BBB SRM header table)
P9_14 = GPIO0.18  (pan, NOT wired)   (from BBB SRM header table)
Servo: 50 Hz, 20 ms period, 500-2500 us = 0-180 deg
PRU0 DRAM: PRU@0x0 / ARM phys 0x4A300000, SHM at +0x1000  (TRM PRU-ICSS ch)
remoteproc0 on this kernel = wkup_m3 (NOT pru0!) -> find pru0 via /name
Load: cp .out ->/lib/firmware/am335x-pru0-fw ; echo start ->remoteprocN/state
```

## Homework (do ONLY this, then report back)
1. `sudo apt install gcc-pru pru-software-support-package`
2. Run the `for d in /sys/class/remoteproc/*/` loop and tell me **which
   `remoteprocN` has `pru0` in its name** (and confirm `remoteproc0` = `wkup_m3`,
   as you found).
3. `make blink.out` (after installing the toolchain), load it with the correct
   `RPROC`, and confirm: (a) `cat $RPROC/state` = `running`, (b) `dmesg | tail`
   shows the firmware loaded, (c) your tilt servo clicks once per second.
   Report what you see.
