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
| PRU OCP master | can write Control Module (pinmux) + GPIO blocks | PRU can set its OWN mux — this is our escape hatch |

**Key conclusion:** Linux cannot change the pinmux. The PRU itself must
reprogram the pad to a PRU mode over its OCP port. That is the only path that
survives the "no config-pin / no devmem / no DT overlay" wall.

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

- **Conf register offset for P9_29 (ball R28 / gpmc_csn3): `0x44E109A0`**
  (Control Module base `0x44E10000` + `0x9A0`).
  - `0x9A0` = P9_29, `0x9A4` = P9_30, `0x9A8` = P9_31, `0x99C` = P9_28.
  - **HIGH-RISK NUMBER:** if P9_29 stays dead after loading, flip
    `CONF_P9_29_OFF` between `0x9A0` and `0x9A4`. This is the most likely
    single point of failure.
- **r30 bit for P9_29 = bit 1 (`r30.1`)**, but ONLY in mux mode 4 (PRU0) or
  mode 5 (PRU1).
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

---

## 4. WHAT WE CAN TRY NEXT (priority order)

1. **Sync the updated `pru/` files to the BBB** (scp or git pull). The board
   only has the OLD repo — that's why `make` reported "up to date" and
   `load_pru.sh`/`arm_write_p929` were `command not found`. This is the #1
   recurring blocker.
2. **Build & load on the BBB** (from inside `~/eyespies/pru`, no extra `cd`):
   ```bash
   make pru1_servo.pru1.out arm_write_p929
   sudo ./load_pru.sh pru0 pru1_servo.pru1.out
   sudo ./arm_write_p929 1500          # offset auto-defaults to 0x9bc now
   # or explicitly:  sudo ./arm_write_p929 1500 0x9bc   (or 0x44e109bc)
   ```
3. **Confirm the mux actually changed** (pinctrl debugfs):
   ```bash
   sudo grep 9bc /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins
   ```
   Expect: `44e109bc 00000024` (mode 4, pull-disabled). If it reads `...27`,
   the firmware's OCP write didn't land (see kernel-problem #1 — PRU master
   may be blocked from 0x44E10000) → fall back to libgpiod GPIO test.
4. **Decoupling test on P9_29 as GPIO:** run a libgpiod sweep on
   `gpiochip3` line for P9_29. If the servo moves, pin+servo+wiring are GOOD
   and the problem is purely the PRU/mux path.
5. **Sweep the servo** by changing pulse width (no reboot, no reload needed
   once the firmware is looping):
   ```bash
   sudo ./arm_write_p929 1000     # ~0°
   sudo ./arm_write_p929 1500     # ~90°
   sudo ./arm_write_p929 2000     # ~180°
   ```
   Stop the PRU (it loops forever): `echo stop | sudo tee /sys/class/remoteproc/<node>/state`.
6. **Verify STANDBY_INIT cleared** if `/dev/mem` reads work at all:
   `sudo devmem2 0x4A322004` → bit 4 must be 0.

---

## 5. POSSIBLE PROBLEMS (latest BeagleBone 6.x kernel)

- **Conf-offset off-by-one (0x9A0 vs 0x9A4):** muxes the wrong pin → silent
  fail. *Mitigation: try both; confirm via pinctrl debugfs.*
- **PRU OCP cannot reach the Control Module (`0x44E10000`):** if the PRUSS
  OCP master isn't granted that range, the self-mux write is dropped and the
  pad stays GPIO. *Mitigation: a kernel-side `config-pin`/DT overlay would be
  needed instead — but those are blocked. Fallback: drive P9_29 as GPIO via
  libgpiod (works, but buzzes).*
- **STANDBY_INIT not cleared:** outputs stay tri-stated → nothing moves even
  with correct mux. *Mitigation: confirm bit 4 of `0x4A322004` = 0.*
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

- [x] **Conf offset: `0x9A0`/`0x9A4` are WRONG** (board pinctrl proves they're
      GPIO2). True offset for P9_29 (GPIO3) to be read live (step 4.1); best
      candidate now `0x86C` (gpmc_csn3 / ball R28).
- [ ] Does the PRU OCP master have write access to `0x44E10000` on this
      specific image? (If not, self-mux fails and we fall back to GPIO/libgpiod.)
- [ ] Which exact kernel version is on the board? (`uname -a` on the BBB would
      pin down which of the 6.x quirks apply.)
- [ ] Was the "brief success" definitely the PRU, or the userspace
      `servo_pwm_test.c` on P9_16? (Decoupling test #5 answers this.)

---

## 7. VERIFICATION STATUS (honest)

| Artifact | Built? | Run on HW? | Result |
|----------|--------|-----------|--------|
| `arm_write_p929.c` | ✅ host gcc | ❌ no BBB | compiles clean |
| `load_pru.sh` | ✅ bash -n | ❌ no BBB | syntax OK |
| `gpio_hold.c` | ❌ no libgpiod | ❌ | UNVERIFIED |
| `pru1_servo.pru1.c` | ❌ no pru-gcc on Mac | ❌ | **UNVERIFIED — must build on BBB** |
| `Makefile` | ✅ parse | ❌ | parses OK |
| P9_29 conf offset | — | ✅ board pinctrl dump | **0x9A0/0x9A4 WRONG** (GPIO2); true offset TBD |
| P9_29 actually moves | — | ❌ | **NOT PROVEN** |

**Bottom line:** host-checkable pieces pass; the firmware's on-hardware
behavior is **unverified** pending a BeagleBone. The previous `0x9A0` conf
offset was definitively wrong (board pinctrl proves it's GPIO2). The true
offset must be read off the board via step 4.1 before we can claim success.

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
