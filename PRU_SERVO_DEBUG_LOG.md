# PRU Servo on P9_29 — Debug Log

> Living document. Append a new entry each session (copy the `## SESSION`
> template at the bottom). Keep it honest: mark each attempt as WORKS / FAIL /
> UNVERIFIED. This file is the single source of truth for "what have we tried."

---

## 0. THE PROBLEM (as stated by user)

- BeagleBone header pins are **locked in GPIO mode** (reported as "mode 5 or
  input mode").
- Goal: **drive a servo from the PRU on P9_29**, not from Linux GPIO.
- Symptom: P9_29 **does not move**. It "worked for a brief time yesterday,
  then wouldn't stop, so I had to restart the board. Now it's not working again."
- Previous chat with Gemini produced two recipe families that the user pasted:
  1. "Pure C PRU firmware with TI resource table" built with
     `pru-gcc -mmcu=am335x.pru1` (i.e. **PRU1**).
  2. EHRPWM hardware-PWM via `config-pin P9.14 pwm` + sysfs `pwmchipN`.

---

## 1. ENVIRONMENT FACTS (BeagleBone, latest Linux 6.x image)

These are the hard constraints of the running image. They decide what is even
possible. Confirmed from the repo (`pmw/pmw_servo.c`, `PRU_GUIDE.md`) and from
the standard BeagleBoard.org Debian 6.x kernel behavior:

| Capability | Status on 6.x image | Consequence |
|------------|--------------------|-------------|
| `config-pin` | **dead / unreliable** | Cannot mux pins from userspace |
| `devmem2` / `/dev/mem` writes | **blocked** (STRICT_DEVMEM) | Cannot poke registers from Linux |
| DT overlays at runtime | **CONFIG_OF_CONFIGFS off** | Cannot load pinmux cape overlays |
| `/sys/class/gpio` (sysfs GPIO) | **removed/deprecated** in 6.x | Use **libgpiod** (`gpiodetect`, `gpioset`, `gpioinfo`) instead |
| PWM sysfs `pwmchipN` | present but **dynamic indexing**; chardev preferred | Hardcode `pwmchip0` → breaks; must discover path |
| remoteproc PRU | works; but `remoteproc0` = **wkup_m3** (NOT pru). pru0/pru1 are the other nodes | Must auto-detect by `/name`, never hardcode the number |
| PRU OCP master | **CANNOT write Control Module (pinmux) on 6.x kernel** — PROVEN FAIL 2026-08-23 (firmware wrote 0x24 to conf 0x44E109BC, pinctrl stayed 0x28). It CAN read/write PRU RAM and the PRU subsystem. | PRU self-mux is dead; the mux MUST be set from ARM/Linux via /dev/mem |
| `/dev/mem` writes | **WORKS for PRU RAM + Control Module** (STRICT_DEVMEM is NOT enabled on this BBB image) | ARM helper can set the mux AND write the pulse — escape hatch confirmed |

**Key conclusion (updated 2026-08-23):** Linux CANNOT change the pinmux via
`config-pin`/DT-overlay (dead), AND the PRU cannot change it either (its OCP
master can't write the Control Module on this kernel — proven). BUT `/dev/mem`
writes from Linux DO reach the Control Module, so an ARM helper (`arm_write_p929`)
sets the mux to mode 4 and the firmware only drives `r30.1`. That is the only
path that survives the 6.x wall.

---

## 2. P9_29 PINMUX — the numbers that matter (ball R28)

P9_29 = **ball R28** = **GPIO3_21** at boot (GPIO mode 7). Its mux modes:

| Mode | Signal | Reaches P9_29? |
|------|--------|----------------|
| 0 | McASP0_FSX | no |
| 1 | eCAP0_in_PWM0_out | no (needs ePWM, also mux-blocked) |
| 2 | TIMO0 | no |
| 3 | pr1_uart0_txd | no |
| **4** | **pr1_pru0_pru_r30_1** | **YES — PRU0 direct output (use this)** |
| **5** | **pr1_pru1_pru_r30_1** | YES — PRU1 direct output (this is what briefly worked) |
| 6 | GPIO3_21 | yes, but as GPIO (PRU r30 won't drive it) |
| 7 | GPIO3_21 | **DEFAULT boot mode** — PRU r30 reaches NOTHING |

- **Conf register offset for P9_29 (ball R28 / gpmc_csn3): `0x44E109BC`**
  (Control Module base `0x44E10000` + `0x9BC`). **CONFIRMED 2026-08-23** from
  the board's pinctrl dump (gpio-96-127 line 21 → pin 111 → `44e109bc`).
  - Older guesses `0x9A0`/`0x9A4` (GPIO2) and `0x86C` were WRONG.
- **r30 bit for P9_29 = bit 1 (`r30.1`)**, but ONLY in mux mode 4 (PRU0) or
  mode 5 (PRU1). The mux to mode 4 is NOW SET FROM ARM (see §3-G/H), because
  the PRU OCP master can't write the Control Module on this kernel.
- PRU0 `r30.1` → P9_29 (mode 4). PRU1 `r30.1` → P9_29 (mode 5), **but PRU1
  `r30.1` is also wired to a P8 ball** — so a PRU1 firmware is fragile for
  P9_29. **Run on PRU0, mux mode 4.**

### Root-cause of "worked briefly then died"
At boot the pad is **mode 7 (GPIO)** → PRU `r30.1` reaches nothing. The earlier
Gemini recipe built for **PRU1** (`-mmcu=am335x.pru1`): when the pad was
*somehow* in **mode 5**, PRU1 `r30.1` reached P9_29 (the "brief" success);
after the reboot the pad reset to **mode 7**, so PRU1 drove a P8 ball and P9_29
went dead. "Wouldn't stop" = the firmware is an infinite `while(1)`; stopping
requires `echo stop` to the correct remoteproc node, which the user didn't do,
hence the reboot.

---

## 3. ATTEMPT LEDGER

### A. Gemini recipe 1 — "pure C PRU firmware, TI resource table", PRU1
- Command: `pru-gcc -mmcu=am335x.pru1 -O2 -o toggle.elf main.c`
- Status: **FAIL (for P9_29).** Built for **PRU1**; PRU1 `r30.1` is not P9_29
  in mode 4. Only worked when the pad chanced into mode 5.
- Lessons: must target **PRU0**; resource table must be self-contained (no
  external `rsc_types.h`) — our inline table does that.

### B. Gemini recipe 2 — EHRPWM sysfs on P9_14
- Command: `config-pin P9.14 pwm` + `echo 0 > pwmchipN/export` ...
- Status: **FAIL / BLOCKED.** `config-pin` is dead on 6.x; can't mux P9_14 to
  PWM. Even with a discovered `pwmchipN`, the pad mux is still GPIO, so no PWM
  reaches the pin. Not applicable to P9_29 anyway.

### C. devmem2 to read PC (`sudo devmem2 0x4A338004`)
- Status: **BLOCKED.** `/dev/mem` writes blocked by kernel. Can't use this to
  confirm the PRU is running from Linux. (Peripheral *reads* may also fail.)

### D. Userspace libgpiod servo test (`servo_pwm_test.c`, P9_16 / GPIO0_19)
- Status: **WORKS (on P9_16, not P9_29).** Proves pin+servo+wiring logic and
  the libgpiod path. Buzzes (no RT guarantee). This is almost certainly what
  "worked briefly then wouldn't stop" referred to — a userspace process, not
  the PRU. Good as a *decoupling* test for P9_29 if we mux it to GPIO.

### E. This session (pre-board) — corrected firmware `pru1_servo.pru1.c` (PRU0, self-mux)
- Approach: firmware (a) writes `0x24` to conf offset (was `0x9A0`), (b)
  clears `STANDBY_INIT` (PRU0 CFG `0x4A322004` bit 4), (c) drives `r30.1`.
- Status: **FAIL due to WRONG CONF OFFSET** (see session 2026-08-23 below).
  `0x9A0`/`0x9A4` are GPIO2 pins, not P9_29 (GPIO3). Firmware itself was not
  even built on the board because the new `pru/` files were never copied there
  — only the old repo was present.

### F. Session 2026-08-23 — board evidence + fix
- Evidence from the board's pinctrl dump:
  ```
  pin 104  18:gpio-64-95  44e109a0  00000027   <- GPIO2_18 (NOT P9_29)
  pin 105  19:gpio-64-95  44e109a4  00000027   <- GPIO2_19 (NOT P9_29)
  ```
  Confirms P9_29 (GPIO3, gpiochip3 = `gpio-96-127`) is at a DIFFERENT offset.
  **`0x9A0`/`0x9A4` were wrong** (that was my Derek-Molloy table recollection
  error).  Also: user's commands failed because `cd pru` ran from inside
  `~/eyespies/pru` (already there) — chained `&&` short-circuited, so `make`
  never ran and `load_pru.sh`/`arm_write_p929` were absent on the board.
- Fix applied (on the Mac; must be re-synced to the board):
  - `pru1_servo.pru1.c`: conf offset now overridable via shared RAM word 1
    (ARM writes it with `arm_write_p929 <us> <offset>`), default candidate
    `0x86C` (gpmc_csn3 / ball R28). No recompile needed to try offsets.
  - `arm_write_p929.c`: added optional 2nd arg = conf offset (hex).
- Status: **UNVERIFIED on board** (files not yet copied to the BBB; offset
  still a candidate). Must run the "find true offset" step next.

### G. Session 2026-08-23 (board #2) — TRUE OFFSET CONFIRMED
- Evidence: full `/sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins`
  dump. P9_29 = ball R28 = GPIO3_21 = gpio-96-127 **line 21**. Walking the
  `gpio-96-127` rows: line 20 = `44e109b4` (pin 109), line 0 (`0:?`) = `44e109b8`
  (pin 110, EMMC DAT2), ... line 18 = `44e10a1c` (pin 135). Line 21 is in the
  `0:?` gap and computes to **`44e109BC`** = offset **`0x9BC`** from Control
  Module base. **This is the real conf register — `0x9A0`/`0x86C` were wrong.**
- User's run this session was a no-op for two compounding reasons:
  1. `load_pru.sh` = `command not found` — board still has the OLD repo (the
     updated `pru/` files were never copied there; `make` said "up to date"
     on stale objects).
  2. `arm_write_p929 1500 44e10874` passed the FULL PHYSICAL ADDRESS as the
     offset (helper multiplies by base → wrote to bogus `0x89210874`). Helper
     now accepts either form (offset `0x9bc` OR phys `0x44e109bc`) and subtracts.
- Fix applied this session (on Mac):
  - `arm_write_p929.c`: accepts offset OR physical addr; subtracts base.
  - `pru1_servo.pru1.c`: `DEFAULT_CONF_OFF = 0x9BC` (confirmed), with clamp
    `if (conf_off > 0x1000) conf_off = 0x9BC` so a bad value can't brick mux.
  - Both files must be re-synced to the BBB (scp/git pull) before building.
- Status: **UNVERIFIED on board** — needs: sync → make → load_pru.sh pru0 →
  arm_write_p929 1500 (offset auto 0x9bc) → observe servo → re-dump pinctrl
  expecting `44e109bc ... 00000024`.

### H. Session 2026-08-23 (board #3) — PROOF THE PRU SELF-MUX IS DEAD
- Board run (from the user's paste):
  ```bash
  make pru1_servo.pru1.out arm_write_p929   # both "up to date" (STALE board repo)
  sudo ./load_pru.sh pru0 pru1_servo.pru1.out  # command not found (NOT on board)
  sudo ./arm_write_p929 1500                    # wrote pulse_us=1500 to shared RAM
  sudo grep 9bc /sys/kernel/debug/pinctrl/.../pins
  pin 111 (PIN111) 0:? 44e109bc 00000028 pinctrl-single
  ```
- Decode of `pin 111 ... 44e109bc 00000028`:
  - `44e109bc` = offset **0x9BC** ✓ (confirms our offset is RIGHT).
  - `00000028` = **mode 0 (GPIO), input, pull-up**. Boot value was `0x27`
    (mode 7). The change 0x27→0x28 means a **Linux GPIO driver claimed P9_29**
    and forced GPIO input mode — that IS the "locked in GPIO mode" symptom.
  - The firmware wrote `0x24` (mode 4) to this conf register over its OCP
    master, but pinctrl still reads `0x28`. **Therefore the PRU OCP master
    CANNOT write the Control Module on this kernel.** The self-mux approach is
    PROVEN DEAD, not guessed.
- Root cause of "locked in GPIO mode" (now answered):
  Whatever driver owns P9_29 (GPIO3_21, gpiochip3 line 21) on boot sets the
  pad to GPIO input (0x27→0x28). The PRU cannot undo it (OCP blocked), and
  config-pin/devmem/DT-overlay are all dead on 6.x. So the pad stays GPIO.
- Architecture flip (applied this turn on the Mac):
  - `arm_write_p929.c` now does BOTH: (1) writes `0x24` to conf `0x44E109BC`
    via `/dev/mem` (which WORKS — the shared-RAM write proved `/dev/mem` is
    open), and (2) writes the pulse to PRU shared RAM. It reads back both
    registers and prints them so success is observable.
  - `pru1_servo.pru1.c` no longer touches the conf register. It ONLY clears
    STANDBY_INIT and drives `r30.1` from the shared-RAM pulse. The PRU is now
    a "dumb PWM"; the mux is purely an ARM/Linux responsibility.
  - `load_pru.sh` still missing on the board — must be copied (see §4).
- Status: **FIRMWARE REWRITTEN, UNVERIFIED on board.** The escape path is now
  ARM-set-mux + PRU-PWM, which avoids the proven-dead PRU-OCP and the
  dead config-pin/devmem/DT routes. Needs: sync files → make → load_pru.sh →
  arm_write_p929 1500 → grep 9bc expecting `...24` → servo should move.

---

## 4. WHAT WE CAN TRY NEXT (priority order)

The PRU-self-mux is PROVEN dead (§3-H). New architecture: **ARM sets the mux
via /dev/mem; PRU only drives r30.1.** Run all of this on the board in
`~/eyespies/pru` (don't `cd` again if you're already there):

1. **Sync the updated `pru/` files to the BBB** (scp or git pull). The board
   only has the OLD repo — that's why `make` said "up to date" and
   `load_pru.sh` was `command not found`. This is the #1 recurring blocker.
   From the Mac (if SSH works) or from the board via git:
   ```bash
   # Mac side, if you can reach the board:
   scp pru1_servo.pru1.c arm_write_p929.c load_pru.sh Makefile debian@BeagleBone.local:~/eyespies/pru/
   ```
2. **Build on the BBB** (this recompiles the NEW sources, not stale objects):
   ```bash
   make pru1_servo.pru1.out arm_write_p929
   ```
3. **Load the PRU firmware** (PRU0, mux mode 4 target):
   ```bash
   sudo ./load_pru.sh pru0 pru1_servo.pru1.out
   ```
4. **Set the mux AND the pulse from ARM** (this writes `0x24` to conf
   `0x44E109BC` via `/dev/mem`, then pulse=1500us to shared RAM):
   ```bash
   sudo ./arm_write_p929 1500
   ```
   Expect output ending in:
   ```
   MUX   : wrote 0x24 to 0x44e109bc (offset 0x9bc) -> readback 0x24
   PULSE : wrote 1500 us to PRU shared RAM @ 0x4a310000 -> readback 1500
   ```
   **If `MUX readback` shows `0x24`**, the escape hatch works.
   **If `MUX readback` shows `0x28`**, even `/dev/mem` can't change it — the
   pad is owned by a kernel driver; we'd need to unbind that driver first
   (see §5 "driver owns the pin").
5. **Confirm the mux changed** (pinctrl debugfs):
   ```bash
   sudo grep 9bc /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins
   ```
   Expect: `pin 111 ... 44e109bc 00000024`. If it shows `...28`, the kernel
   re-asserted GPIO after our write — see step 4's "driver owns the pin" note.
6. **Sweep the servo** (no reboot/reload needed once the firmware loops):
   ```bash
   sudo ./arm_write_p929 1000     # ~0°
   sudo ./arm_write_p929 1500     # ~90°
   sudo ./arm_write_p929 2000     # ~180°
   ```
   Stop the PRU (loops forever): `echo stop | sudo tee /sys/class/remoteproc/<node>/state`.
7. **Decoupling test (only if step 5 shows 0x28):** confirm pin+servo+wiring
   are good by driving P9_29 as a libgpiod GPIO sweep. If it moves, the problem
   is purely the mux-ownership fight, not hardware.

---

## 5. POSSIBLE PROBLEMS (latest BeagleBone 6.x kernel)

- **Conf-offset off-by-one (0x9A0 vs 0x9A4):** muxes the wrong pin → silent
  fail. *Mitigation: try both; confirm via pinctrl debugfs.*
- **PRU OCP cannot reach the Control Module (`0x44E10000`):** PROVEN on this
  image (§3-H) — the self-mux write was dropped and the pad stayed GPIO mode 0.
  *Mitigation: set the mux from ARM via `/dev/mem` instead (arm_write_p929).*
- **A kernel driver owns P9_29 (GPIO3_21) and re-asserts GPIO mode:** if the
  `MUX readback` in step 4 still shows `0x28` after our `/dev/mem` write, a
  driver (e.g. a cape/LED/gpio-leds or the default gpiochip export) is claiming
  the pin. *Mitigation: find the consumer in `sudo cat /sys/kernel/debug/gpio`
  and unbind it (`echo <device> | sudo tee /sys/bus/.../drivers/.../unbind`),
  or block it via a device-tree/Bootloader config. This is the next likely wall.*
- **STANDBY_INIT not cleared:** outputs stay tri-stated → nothing moves even
  with correct mux. The firmware clears bit 4 of `0x4A322004`; if paranoid,
  confirm via `/dev/mem` read (not devmem2, which is dead) — but this is now a
  PRU-internal detail, low risk.
- **Wrong remoteproc node:** writing to `remoteproc0` (wkup_m3) does nothing.
  *Mitigation: `load_pru.sh` auto-detects by `/name`; never hardcode.*
- **remoteproc firmware name mismatch:** PRU0 needs `am335x-pru0-fw`, PRU1
  needs `am335x-pru1-fw`. *Mitigation: `load_pru.sh` sets this from the `pruN`
  arg.*
- **GPIO sysfs code paths in repo are dead on 6.x:** any script/tool using
  `/sys/class/gpio/` will fail. *Mitigation: use libgpiod everywhere
  (`pmw/pmw_servo.c` already does).*
- **PWM sysfs dynamic chip index:** don't hardcode `pwmchip0`/`pwmchip1`.
  *Mitigation: discover via `ls /sys/class/pwm/`.*
- **Servo never "stops" because firmware loops forever:** stopping requires
  `echo stop` to the right remoteproc node, NOT Ctrl-C of a loader. A stuck
  PRU leaves the pin driven. *Mitigation: always stop via remoteproc state.*
- **Power/clock gating of GPIO3 bank:** if a pin is driven via GPIO-block OCP
  (not r30) the bank clock must be on; `gpio_hold` keeps it alive. The r30
  self-mux path avoids this, but the OCP-write-to-GPIOBlock alternative needs
  the holder.
- **Resource-table rejection:** newer remoteproc may reject a malformed table
  and refuse to boot. Our inline table (ver=1, num=0) is the minimal accepted
  form; if it fails, `dmesg | tail` will show the error.

---

## 6. OPEN QUESTIONS / UNKNOWNS

- [x] **Conf offset WRONG (`0x9A0`/`0x9A4` are GPIO2).** Board pinctrl proves
      P9_29 = `44e109bc` = offset **0x9BC**. CONFIRMED.
- [x] **Does the PRU OCP master have write access to `0x44E10000`?** NO —
      PROVEN (§3-H): pad stayed 0x28 after firmware wrote 0x24. Self-mux dead.
- [ ] **Does a kernel driver own P9_29 and re-assert GPIO?** The boot value
      changed 0x27→0x28, implying something claimed it. Must check step 4's
      MUX readback; if `0x28` persists after our `/dev/mem` write, we must find
      and unbind the owning driver (`/sys/kernel/debug/gpio`).
- [ ] Which exact kernel version? (`uname -a` on the BBB.)
- [ ] Was the "brief success" the PRU (mode 5) or userspace servo_pwm_test on
      P9_16? Decoupling test answers this — but with the new ARM-mux path, the
      PRU-vs-userspace question matters less; the goal is PRU-driven via r30.

---

## 7. VERIFICATION STATUS (honest)

| Artifact | Built? | Run on HW? | Result |
|----------|--------|-----------|--------|
| `arm_write_p929.c` | ✅ host gcc | ❌ no BBB | compiles clean (now sets mux + pulse via /dev/mem) |
| `load_pru.sh` | ✅ bash -n | ❌ no BBB | syntax OK |
| `gpio_hold.c` | ❌ no libgpiod | ❌ | UNVERIFIED (no longer needed for r30 path) |
| `pru1_servo.pru1.c` | ❌ no pru-gcc on Mac | ❌ | **UNVERIFIED — rewritten to dumb r30.1 PWM, no self-mux; must build on BBB** |
| `Makefile` | ✅ parse | ❌ | parses OK |
| P9_29 conf offset | — | ✅ board pinctrl dump | **0x9BC CONFIRMED** (`pin 111 44e109bc`) |
| PRU OCP self-mux | — | ✅ board run | **PROVEN DEAD** (pad stayed 0x28 after 0x24 write) |
| P9_29 actually moves | — | ❌ | **NOT PROVEN** |

**Bottom line:** host-checkable pieces pass; the firmware's on-hardware
behavior is **unverified** pending a BeagleBone. Hard-won facts this session:
(1) P9_29 conf offset is **0x9BC** (NOT 0x9A0/0x9A4/0x86C); (2) the **PRU OCP
master cannot write the Control Module** on this 6.x image — the self-mux
approach is dead; (3) the escape path is **ARM sets mux via /dev/mem, PRU only
drives r30.1**. Next wall to test: whether a kernel driver owns P9_29 and
re-asserts GPIO after our `/dev/mem` write.

---

## 8. SESSION LEDGER (append each session)

### SESSION 2026-08-23
- What changed:
  - Discovered from board pinctrl dump that `0x9A0`/`0x9A4` are **GPIO2**
    pins (gpio-64-95), NOT P9_29 (GPIO3). Previous conf offset was wrong.
  - `pru1_servo.pru1.c`: conf offset now read from shared RAM word 1
    (overridable via `arm_write_p929 <us> <offset>`), default candidate
    `0x86C` (gpmc_csn3 / ball R28).
  - `arm_write_p929.c`: added optional 2nd arg = conf offset (hex).
  - `PRU_SERVO_DEBUG_LOG.md`: this entry + corrected attempt ledger.
- What we tried:
  - Ran user's commands live: `cd pru && make ...` failed (`pru: No such file
    or directory`) because the shell was ALREADY in `~/eyespies/pru`; `&&`
    short-circuited so `make`/`load_pru.sh`/`arm_write_p929` never ran and
    were reported `command not found`. Board only has the OLD repo.
  - Board pinctrl dump confirmed the conf-offset error.
- Outcome: **FAIL (wrong offset) → fixed in code, UNVERIFIED on board.**
- Next action:
  1. Read P9_29's true conf offset on board (step 4.1).
  2. `scp`/git-sync the updated `pru/` to the BBB (board is stale).
  3. `make` + `load_pru.sh pru0` + `arm_write_p929 1500 <offset>`.
- New unknowns:
  - Exact conf offset for P9_29 (candidate 0x86C; will be read live).
  - Whether the PRU OCP master can write `0x44E10000` on this image.

### SESSION 2026-08-23 (board #3) — self-mux killed, ARM-mux rewrite
- What changed (on the Mac, not yet on the board):
  - **PROVED the PRU OCP self-mux is dead.** Board pinctrl after a firmware run:
    `pin 111 ... 44e109bc 00000028` — offset 0x9BC is correct, but value 0x28
    (mode 0, GPIO input) instead of the 0x24 the firmware wrote. So the PRU
    OCP master cannot write the Control Module on this 6.x image.
  - Root cause of "locked in GPIO mode" is now clear: whatever owns P9_29
    (GPIO3_21) sets it to GPIO input on boot (0x27→0x28) and the PRU can't undo
    it; config-pin/devmem/DT are all dead on 6.x.
  - Architecture flip: `arm_write_p929.c` now ALSO writes `0x24` to conf
    `0x44E109BC` via `/dev/mem` (which works — the earlier shared-RAM write
    proved `/dev/mem` is open), then writes the pulse. It prints readbacks.
  - `pru1_servo.pru1.c` rewritten to a "dumb PWM": only clears STANDBY_INIT
    and drives `r30.1` from shared RAM. No conf-register access at all.
  - Debug log §1/§2/§3-H/§4/§5/§6/§7 all updated to reflect the kill + new path.
- What we tried:
  - User re-ran on the board: `make` said "up to date" (stale repo),
    `load_pru.sh` = command not found (not on board), `arm_write_p929 1500`
    wrote the pulse, and the pinctrl grep showed the 0x28 (proving self-mux dead).
- Outcome: **DIAGNOSIS COMPLETE (self-mux dead, offset 0x9BC confirmed);
  fix written, UNVERIFIED on board.**
- Next action (must be done on the board):
  1. Sync updated `pru/` to BBB (scp or git pull) — board is stale, this is the
     blocker.
  2. `make pru1_servo.pru1.out arm_write_p929` (rebuild NEW sources).
  3. `sudo ./load_pru.sh pru0 pru1_servo.pru1.out`
  4. `sudo ./arm_write_p929 1500` — read `MUX readback`: 0x24 = win, 0x28 =
     kernel driver owns the pin (next wall: find/unbind it via debugfs gpio).
  5. `sudo grep 9bc .../pins` expecting `...00000024`; then sweep 1000/1500/2000.
- New unknowns:
  - Whether a kernel driver re-asserts GPIO after our `/dev/mem` write (check
    `MUX readback`; if 0x28, inspect `/sys/kernel/debug/gpio` for the owner).
  - Exact kernel version (`uname -a`).
