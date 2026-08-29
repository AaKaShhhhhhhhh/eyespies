# PRU Servo on the BeagleBone Black — A Field Guide to Embedded Linux & PRU I/O

> **Restructured 2026-08-28** from a 1,500-line raw debug journal into a learning
> document. The original chronological notes are preserved as a *case study* in
> Part B; the **concepts** we were forced to learn are in Part A; the **recipe that
> actually works** is in Part C.
>
> **Final status: SOLVED.** PRU0 drives an MG90S servo on **P9_31** (PRU0 `__R30`
> bit 0), muxed to mode 5 by U-Boot. Verified moving on the board 2026-08-28.
>
> **Companion files:**
> - `PRU_SERVO_RAW_JOURNAL.md` — the full 1,562-line raw iteration log (every command
>   and readback, kept verbatim).
> - `docs/TURRET_PLAN.md` — the camera-turret project plan (next phase).

---

## 0. TL;DR — the working recipe

```bash
# 1) One-time, in /boot/firmware/uEnv.txt (U-Boot runs this every boot, full HW access):
uenvcmd=mw.l 0x44E10990 0x05        # P9_31 pad -> mode 5 (PRU0 R30_0)

# 2) Reboot once so the mux takes effect.
sudo reboot

# 3) Build + load the PRU firmware (the BeagleBone is the build host; Mac can't run pru-gcc):
cd ~/eyespies/pru && git pull
make pru_servo.out
sudo bash load_pru.sh pru0 pru_servo.out     # auto-detects remoteproc node, loads, starts

# 4) Confirm: mux held + servo sweeping
sudo devmem2 0x44E10990        # -> 0x5  (mode 5, U-Boot mux stuck)
# Servo observed sweeping continuously.

# 5) Stop (freezes at current angle, stays powered):
sudo bash load_pru.sh stop
```

**Wiring:** servo signal (yellow) → **P9_31**; 5 V (red) + GND (black) on any 5 V/GND
header pair (e.g. P9_5/P9_6 for 5 V, P9_1/P9_2 for GND). MG90S is 3.3 V-tolerant on
signal.

---

# PART A — EMBEDDED LINUX CORE CONCEPTS (what this struggle taught)

This is the part to read if you want to *understand* the board, not just copy commands.

## A1. The AM335x memory map — and who is allowed to touch what

The BeagleBone Black is a Sitara **AM3358**: an ARM Cortex-A8 **plus** a PRU-ICSS
(Industrial Communications SubSystem) that contains **two PRU cores** (PRU0, PRU1).
The whole chip is mapped into one physical address space, but *different masters can
reach different regions*, and that single fact caused 90% of our pain.

| Region | Physical base | Reachable from | Notes |
|--------|---------------|---------------|-------|
| **Control Module / padconf** | `0x44E10000` | ARM *only at boot* (U-Boot) | Pin multiplexing lives here. **Linux 6.x blocks ARM `/dev/mem` writes to it.** |
| **GPIO0** (bank 0) | `0x44E07000` | ARM; PRU OCP = **NO** | GPIO0 sits on the **L4_WKUP** interconnect; the PRU's OCP master cannot reach it. |
| **GPIO1/2/3** (banks 1–3) | `0x4804xxxx` | ARM + PRU OCP | On **L4_PER**; the PRU *can* reach these over OCP. |
| **PRU-ICSS CFG** | PRU-local `0x00026000` / ARM-global `0x4A326000` | PRU (local) / ARM (global) | SYSCFG register at `+0x4`. STANDBY gate. |
| **PRU shared RAM** | PRU-local `0x00010000` / ARM-global `0x4A310000` | PRU (local) / ARM (global) | The natural ARM↔PRU command channel. |
| **PRU0 core** | `4a334000` | remoteproc | Shows up as `remoteproc1` in sysfs. |
| **PRU1 core** | `4a338000` | remoteproc | Shows up as `remoteproc2` in sysfs. |

**Lesson 1:** "An address" is not absolute — the *same physical RAM* has a different
address depending on whether the PRU or the ARM is reading it. Always use the
**PRU-local** view inside firmware and the **ARM-global** view in userspace. Mixing
them (e.g. dereferencing `0x4A310000` from inside the PRU) faults the core.

**Lesson 2:** The PRU OCP master cannot reach **GPIO0** (L4_WKUP). Every TI PRU GPIO
example uses GPIO1/2/3 for this reason. We confirmed this the hard way: PRU writes to
`0x44E07xxx` were silently dropped even with STANDBY cleared.

## A2. Pin multiplexing — the heart of the struggle

Every ball on the AM335x has up to **8 functions** (modes 0–7). Mode 7 is almost
always **GPIO**. A *padconf register* (in the Control Module) selects which internal
signal a ball routes to. Example for P9_31:

```
P9_31 = ball (mcasp0_aclkx) = pad 0x44E10990
   mode 0 = McASP0_ACLKX      mode 5 = pr1_pru0_pru_r30_0   <-- PRU direct output
   mode 6 = GPIO3_14          mode 7 = GPIO3_14 (default boot)
```

Writing `0x05` to `0x44E10990` routes the ball to **PRU0 `__R30` bit 0**. That is the
entire "pinmux" for a PRU output — no driver, no peripheral, just a routing decision
made before Linux boots.

**Definitive evidence** (board pinctrl dump) that we used to prove which ball is which:
```
pin 97  (PIN97)  44e10984 00000005   # P9_16 gpmc_be1n  -> mode5, but NO PRU route (cape)
pin 100 (PIN100) 44e10990 00000027   # P9_31 mcasp0_aclkx -> mode7 (pre-fix); cape mode5 = R30_0
pin 111 (PIN111) 44e109bc 00000024   # our OLD P9_29 attempt -> WRONG pad + mode4
```
Cross-checked against the **official BeagleBoard PRU cape DTS**
(`AM335X-PRU-RPROC-4-19-TI-PRUCAPE-00A0.dts`): P9_31 = `0x190 0x05` ⇒ PRU CAPE Blue LED
⇒ **PRU0 R30_0**. P9_16 (`gpmc_be1n`) is **absent** from the cape's PRU outputs, so it
has no PRU route at all.

## A3. Three ways to set a pinmux — and why only one worked here

| Method | Works on this 6.x image? | Why |
|--------|--------------------------|-----|
| **U-Boot `uenvcmd` `mw.l`** | ✅ **YES (permanent)** | Runs *before* the kernel with full hardware access; the kernel's pinctrl driver never re-touches a pad that no DT group references, so the value sticks. |
| **Device-tree overlay (`.dtbo`)** | ❌ fragile/no-op | `bone-pinmux-helper` loads but doesn't apply; `pinctrl-hog` doesn't stick; `&ocp`/`&pruss` consumers hit dependency cycles. One overlay shape *bricked boot* (see B4). |
| **`config-pin`** | ❌ dead | Needs the cape-universal overlay; removed/non-functional on 6.x. |
| **ARM `devmem2` to padconf** | ❌ **silently dropped** | Kernel 6.x blocks `/dev/mem` writes to the Control Module range. `devmem2` prints "Written" but readback is unchanged. |

**Lesson 3 (the big one):** On this kernel, **pinmux is a boot-time concern.** Set it in
U-Boot (`uenvcmd`), not from Linux. Runtime `devmem2` muxing is a best-effort no-op.

**Lesson 4:** `devmem2` is *not* useless — ARM `/dev/mem` writes to the **PRU RAM/CFG
global mirror** (`0x4A31xxxx`, `0x4A32xxxx`) **do** work. It is specifically the
**Control Module** (`0x44E10000`) that is blocked. Knowing which region is blocked is
what let us build the ARM→PRU command channel later.

## A4. Device Tree, overlays, and pinctrl — why `config-pin` died

The Device Tree describes hardware to the kernel. Pinmux is expressed as **pinctrl
groups**; the `pinctrl-single` driver applies them. Two failure modes we hit:

- **Attaching `pinctrl-0` to the pinmux *controller* itself** (`&am33xx_pinmux`)
  **bricked boot** — the kernel hung at "Starting kernel." Never do this; only attach
  pinmux groups to a *consumer* node (a peripheral), and only via an overlay the loader
  accepts.
- **`dtb_overlay=` format bug:** this U-Boot build does **not** tokenize
  space-separated paths. A multi-path string concatenates into one bad filename →
  `FDT_ERR_BADMAGIC`. Fix = **ONE bare filename**.

**Lesson 5:** Overlays are powerful but unforgiving on a moving target kernel. When the
kernel's pinctrl owns the pad, the only reliable external knob is U-Boot.

## A5. remoteproc — loading firmware into the PRU

`remoteproc` is the kernel framework that boots a co-processor from an ELF firmware.
Flow:

1. Firmware `am335x-pru0-fw` is placed in `/lib/firmware/`.
2. `echo am335x-pru0-fw > /sys/class/remoteproc/remoteprocN/firmware`
3. `echo start > /sys/class/remoteproc/remoteprocN/state`
4. Kernel copies the ELF (using its **resource table** to find carveouts) into PRU
   instruction/data RAM and releases the core. `state` reads `running`.

**Critical gotcha:** `remoteproc0` is usually **`wkup_m3`** (the CM3 power coprocessor),
**NOT a PRU.** The PRU cores appear as `remoteproc1`/`remoteproc2` but the *number can
shift*. **Discover by `/name`**, never hardcode the number:
```bash
for d in /sys/class/remoteproc/*/; do
  grep -q 4a334000 "$d/name" && echo "PRU0 is $d"   # 4a338000 = PRU1
done
```
`load_pru.sh` does exactly this.

## A6. The PRU itself — `__R30` vs OCP, STANDBY_INIT, address views

Two ways for firmware to move a pin:

- **Direct GPO (`__R30`):** a register whose bits appear *directly* on the ball when the
  ball is muxed to a PRU mode. No peripherals, no OCP, **no STANDBY gating**. This is the
  simplest deterministic output and is what finally worked.
  ```c
  volatile register uint32_t __R30 asm("r30");
  __R30 |= (1u << 0);   // P9_31 high
  __R30 &= ~(1u << 0);  // P9_31 low
  ```
- **OCP master:** the PRU can read/write the whole memory map (act like a DMA master).
  Requires **STANDBY_INIT** (CFG SYSCFG bit 4) cleared 1→0, and the OCP port enabled.
  We proved the OCP path to GPIO0 is dead (A1), so we abandoned it.

**STANDBY_INIT confusion (retired):** SYSCFG **bit 0** is the *read-only* IDLE_MODE
status bit — writing it is a no-op. The real gate is **bit 4** (STANDBY_INIT). On this
image the PRU's *own local* write to `0x00026004` did **not** clear it; only ARM's
global `0x4A326004` write did. But since we pivoted to `__R30` (which needs no OCP/STANDBY
at all), the whole question became moot. **`__R30` needs none of it** — that is why it
won.

**Lesson 6:** When you just need a deterministic output, prefer `__R30` direct GPO over
the OCP master. It sidesteps STANDBY, OE registers, and the L4_WKUP wall entirely.

## A7. Userspace GPIO on 6.x — libgpiod, not sysfs

`/sys/class/gpio` (sysfs GPIO) was **removed** in kernel 6.x. The API is now
**libgpiod** (`gpiodetect`, `gpioset`, `gpioinfo`, and the `gpiod.h` C API). Our earlier
"servo moved on P9_16" proof was a `gpioset gpiochip0 19=1` — pure userspace GPIO, no PRU.

```c
struct gpiod_chip *c = gpiod_chip_open_by_name("gpiochip0");
struct gpiod_line *l = gpiod_chip_get_line(c, 19);   // P9_16 = GPIO0_19
gpiod_line_request_output(l, "svc", 0);
gpiod_line_set_value(l, 1);
```
**Lesson 7:** libgpiod goes through the kernel GPIO driver (enables the clock, owns the
pad). Raw `/dev/mem` mmaps to a gated GPIO bank and **bus-fault** (`external abort on
non-linefetch`) — we hit that exact fault on GPIO3.

## A8. Real-time reality — why servos belong on the PRU

A servo needs a **stable 50 Hz** pulse with 1–2 ms width *forever*, or it loses position
and buzzes. A Linux userspace thread bit-banging that (even with `SCHED_FIFO`) gets
preempted by the camera/capture loop, producing jitter. The PRU, running bare-metal at
200 MHz with no OS, emits cycle-perfect PWM. **That is the architectural reason the servo
moved to the PRU** and the turret's detection code can stay on the ARM uninterrupted.

## A9. Safety — PMIC brown-outs & live wires

Several board reboots were **self-inflicted**:

- **Never insert/remove a header wire while powered.** A momentary bridge to a neighbour
  pin dumps current into the PMIC → brown-out reset. Power the board **off** (remove
  barrel) before changing any P8/P9 wiring.
- An **externally powered servo** (separate 5 V supply) on a BBB pin without common
  ground/level-shift can drive current into the 3.3 V domain → reboot or damage. For
  loopback tests, the servo must be **fully unplugged** (all three wires off).
- Repeated sudden power loss corrupted the eMMC (ext4 went read-only). Reimage + `git
  pull` restores everything; only `/boot/firmware/uEnv.txt` is board-local state.

---

# PART B — THE INVESTIGATION AS A CASE STUDY

Condensed from ~40 board iterations. Each milestone carries the lesson; the dead ends are
kept so we never re-chase them.

### B1. Wrong pad + wrong mode on P9_29 (the 8-turn mux fight)
We muxed P9_29 with `mw.l 0x44E109BC 0x24`. The register `0x44E109BC` is real, but P9_29's
*actual* pad is `0x44E10994`, and PRU mode is **5** (`0x05`), not 4. So R30 never reached
the ball. **Lesson:** cross-check the ball→pad offset against the TRM/cape DTS before
writing a mux; guessing offsets wastes days.

### B2. "Self-mux from the PRU" — dead
Firmware tried to write its own padconf over OCP. Pin stayed unchanged. **Lesson:** the PRU
cannot mux its own pins (Control Module blocked); mux comes from U-Boot.

### B3. DT overlays of every shape — all no-ops / brick
`bone-pinmux-helper`, `pinctrl-hog`, `&ocp` consumer, `&pruss` consumer — none applied the
pin on 6.12; one shape bricked boot (B4). **Lesson:** overlays are not the mux path on
this kernel.

### B4. Attaching `pinctrl-0` to `&am33xx_pinmux` bricked boot
Kernel hung at "Starting kernel." Recovered via manual eMMC boot (base DTB +
`BB-BONE-eMMC1` overlay + `bootz root=/dev/mmcblk1p3`). **Lesson:** never attach pinmux to
the controller; only to a consumer.

### B5. `devmem2` to padconf silently dropped
Wrote `0x24`, read back `0x28`. **Lesson (Lesson 3):** ARM can't mux from Linux on 6.x.

### B6. STANDBY_INIT wild-goose chase (retired)
We believed `r30` was tri-stated because STANDBY_INIT wasn't cleared. Bit 0 is read-only
IDLE_MODE status; bit 4 is STANDBY_INIT but the PRU-local write didn't land on this image.
**Lesson:** don't generalize "ARM write ignored" to "PRU can't clear it" — and ultimately
`__R30` doesn't need it anyway.

### B7. Shared-RAM address bug (firmware fault)
Firmware read the command from `0x4A310000` (ARM-global) instead of `0x00010000`
(PRU-local) → out-of-range load faulted the core. **Lesson (Lesson 1):** use PRU-local
views inside firmware.

### B8. "Servo moved on P9_16 via gpioset" — ground truth
The one pin the user personally proved drives a servo was **P9_16 = GPIO0_19**, in plain
GPIO mode. That killed the P9_29 mux fight and pointed at a simpler path — but P9_16 has
**no PRU route** (cape), so it can't be the PRU pin.

### B9. Pivot to P9_31 — the win
Cross-checked the **PRU cape DTS**: P9_31 = `mcasp0_aclkx` = pad `0x44E10990`, cape mux
`0x05` = **PRU0 R30_0**. Set `uenvcmd=mw.l 0x44E10990 0x05`, rebooted, loaded
`pru_servo.out` (pure `__R30` sweep), and the servo **moved**. **Lesson:** trust the
vendor cape DTS; pick a pin that is *documented* as a PRU R30 output.

### B10. Tool bugs that masqueraded as hardware faults
Several "servo dead / loopback 0" results were actually parser bugs (wrong mux hex width,
`strrchr` grabbing the wrong token), double-opened libgpiod chips, and sampler/transmitter
aliasing. **Lesson:** before concluding hardware is broken, prove the *test instrument*
with an independent check (the user's own `gpio_sweep` exposed a wrong line number we'd
trusted for 10 turns).

---

# PART C — FINAL REPRODUCIBLE RECIPE

### C1. Permanent mux (already applied; for reflash)
```bash
sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt
printf 'uenvcmd=mw.l 0x44E10990 0x05\n' | sudo tee -a /boot/firmware/uEnv.txt
sudo reboot
# verify after boot:
sudo devmem2 0x44E10990      # -> 0x5
```

### C2. Manual eMMC recovery (if an overlay ever bricks boot — do NOT attach pinctrl-0 to &am33xx_pinmux)
At the `=>` U-Boot prompt (eMMC = `mmc 1`, root = `mmcblk1p3`):
```bash
mmc dev 1
load mmc 1:3 0x82000000 /boot/vmlinuz-6.12.28-bone25
load mmc 1:3 0x88000000 /boot/dtbs/6.12.28-bone25/am335x-boneblack-uboot.dtb
load mmc 1:3 0x8A000000 /boot/dtbs/6.12.28-bone25/BB-BONE-eMMC1-01-00A0.dtbo
fdt addr 0x88000000; fdt resize 0x10000; fdt apply 0x8A000000
load mmc 1:3 0x88080000 /boot/initrd.img-6.12.28-bone25
setenv bootargs console=ttyS0,115200n8 root=/dev/mmcblk1p3 ro rootfstype=ext4 rootwait coherent_pool=1M net.ifnames=0 rng_core.default_quality=100
bootz 0x82000000 0x88080000:${filesize} 0x88000000
```
Then remove the broken `dtb_overlay=` line from `/boot/firmware/uEnv.txt`.

### C3. Build + load (BeagleBone is the build host)
```bash
cd ~/eyespies/pru && git pull
make pru_servo.out                 # pru-gcc, one harmless -Wvolatile-register-var warning
sudo bash load_pru.sh pru0 pru_servo.out
sudo bash load_pru.sh stop         # halt (freezes servo at current angle)
```

### C4. Wiring
- Signal (yellow) → **P9_31**.
- 5 V (red) → a 5 V pin (P9_5 or P9_6). GND (black) → a GND pin (P9_1/P9_2).
- Change wiring only with the board **powered off**.

---

# PART D — FILE REFERENCE & VERIFICATION

### D1. Files
| File | Purpose | Status |
|------|---------|--------|
| `pru/pru_servo.c` | PRU0 firmware: `__R30` bit 0 sweep on P9_31, 50 Hz, 1–2 ms. No OCP/STANDBY. | ✅ working |
| `pru/load_pru.sh` | Discovers PRU node by `/name`; `stop` action halts it. | ✅ working |
| `pru/Makefile` | Builds `pru_servo.out` (+ legacy helpers). | ✅ |
| `pru/resource_table_empty.h` | Minimal PRU resource table. | ✅ |
| `pru/pru_p9_29.dts`, `pru1_servo.pru1.c`, `arm_write_p929.c`, `p929_gpio_test.c`, `loopback_*` | **Retired** investigation artifacts. Kept for reference only. | archived |

### D2. Verification status (honest)
| Artifact | Result |
|----------|--------|
| P9_31 mux via U-Boot `uenvcmd` | ✅ `pin 100 ... 44e10990 00000005` |
| `pru_servo.out` builds + loads | ✅ remoteproc1 "now up" |
| Servo moves under PRU | ✅ verified on board 2026-08-28 |
| `stop` action | ✅ halts PRU, pin frozen, servo stays powered |

### D3. Retired theories (do NOT re-chase)
- PRU self-muxes its pin (OCP can't write Control Module).
- ARM `/dev/mem` sets the mux (dropped on 6.x).
- DT overlay / `config-pin` sets the mux (dead on 6.x).
- STANDBY_INIT bit 0 gates `r30` (bit 0 is read-only status; `__R30` needs no STANDBY anyway).
- P9_29 is the servo pin (wrong pad + mode; P9_16 has no PRU route; P9_31 is the validated PRU R30_0).
