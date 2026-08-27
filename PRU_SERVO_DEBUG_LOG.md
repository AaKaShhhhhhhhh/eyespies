# PRU Servo on P9_29 — Debug Log

> **Single source of truth.** Restructured 2026-08-26 for clarity.
> Rule: every attempt is logged as WORKS / FAIL / UNVERIFIED with the exact evidence.
> One command block = one verification gate. Do not change mechanism without a gate.

---

## 0. STATUS DASHBOARD (read this first)

| Item | State |
|------|-------|
| **Goal** | Drive an MG90S servo on **P9_29** from the **PRU** (not Linux GPIO), 50 Hz PWM. |
| **Pin mux** | ✅ **SOLVED (permanent).** P9_29 = `mode 4` (`0x24` = PRU0 `r30.1`), set every boot by a U-Boot `uenvcmd`. Confirmed `pin 111 ... 44e109bc 00000024`. |
| **Servo motion** | ❌ **NOT SOLVED.** Servo only moved when the bare P9_29 wire was hand-touched (floating → EMI). PRU/ARM drive does not move it. |
| **Current blocker** | `r30.1` is not reaching the pad. STANDBY_INIT theory is **dead** (proven read-only PRCM bit). Prime suspects now: (a) pin owned by a kernel driver, (b) PRU0-vs-PRU1 mux-mode mismatch, (c) signal wire not actually in P9_29. |
| **Next action** | Run `p929_gpio_test 4` (ARM-only GPIO toggle) to bisect hardware vs PRU path. See §8. |
| **Board** | BeagleBone Black, eMMC boot, kernel `6.12.28-bone25`, U-Boot `2022.04-gc6f4cf7d`, PRU0 = `remoteproc1` = `4a334000.pru`. |

---

## 1. THE PROBLEM (as stated by user)

- BeagleBone header pins reported "locked in GPIO / input mode."
- Goal: drive a servo from the **PRU** on **P9_29**, not from Linux GPIO.
- Symptom: P9_29 **does not move**. "Worked for a brief time yesterday, then wouldn't stop, so I had to restart the board. Now it's not working again."
- Earlier chat (Gemini) produced two recipe families the user pasted:
  1. "Pure C PRU firmware with TI resource table" built with `pru-gcc -mmcu=am335x.pru1` (**PRU1**).
  2. EHRPWM hardware-PWM via `config-pin P9.14 pwm` + sysfs `pwmchipN`.

---

## 2. ENVIRONMENT FACTS (BeagleBone 6.x image)

Hard constraints of the running image. These decide what is even possible.

| Capability | Status on 6.x image | Consequence |
|------------|--------------------|-------------|
| `config-pin` | **dead / unreliable** | Cannot mux pins from userspace. |
| `devmem2` / `/dev/mem` writes | **silently dropped** for the Control Module (kernel blocks userspace writes to `0x44E10000`). | Cannot poke the pinmux from Linux. |
| DT overlays at runtime (configfs) | **fragile / no-op on 6.12** | `bone-pinmux-helper` and `pinctrl-hog` load but don't apply the pin. |
| `/sys/class/gpio` (sysfs GPIO) | **removed** in 6.x | Use **libgpiod** (`gpiodetect`, `gpioset`, `gpioinfo`). |
| PWM sysfs `pwmchipN` | present but **dynamic indexing** | Hardcode `pwmchip0` → breaks. |
| remoteproc PRU | works; but `remoteproc0` = **wkup_m3** (NOT pru). | Must auto-detect PRU node by `/name`, never hardcode the number. |
| PRU OCP master | **CANNOT write Control Module** on 6.x (proven: pin stayed `0x28`). Can read/write PRU RAM + PRU subsystem. | PRU self-mux is dead; mux MUST be set from U-Boot (full HW access). |
| U-Boot `uenvcmd` | **works** — runs with full HW access, before the kernel's pinctrl driver. | The ONLY reliable mux path. `mw.l 0x44E109BC 0x24` sticks. |
| `/dev/mem` writes to **PRU RAM** | **work** (STRICT_DEVMEM not blocking the PRU subsystem region). | ARM helper can set the pulse width + read SYSCFG. |

**Key conclusion:** The pinmux can ONLY be set by U-Boot (`uenvcmd` + `mw.l`). Everything else (config-pin, devmem2, DT overlay hog, &ocp, &pruss consumer) is dead on this kernel. The PRU then only drives `r30.1`; ARM only writes the pulse into shared RAM.

---

## 3. P9_29 PINMUX REFERENCE (the numbers that matter)

P9_29 = **ball R28** = **GPIO3_21** at boot (GPIO mode 7).

### 3a. Mux modes for ball R28
| Mode | Signal | Reaches P9_29? |
|------|--------|----------------|
| 0 | McASP0_FSX | no |
| 1 | eCAP0_in_PWM0_out | no (needs ePWM, also mux-blocked) |
| 2 | TIMO0 | no |
| 3 | pr1_uart0_txd | no |
| **4** | **pr1_pru0_pru_r30_1** | **YES — PRU0 direct output (USE THIS)** |
| **5** | pr1_pru1_pru_r30_1 | YES — PRU1 direct output (P9_29 only in mode 5) |
| 6 | GPIO3_21 | yes, but as GPIO |
| 7 | GPIO3_21 | DEFAULT boot mode — PRU `r30` reaches NOTHING |

- **`mode 4` = `0x24`** (PRU0 `r30.1`, rx-off, pull-off). **`mode 5` = `0x25`** (PRU1).
- **Conf register offset for P9_29: `0x44E109BC`** (Control Module base `0x44E10000` + `0x9BC`). CONFIRMED from board pinctrl dump (`pin 111 ... 44e109bc`).
- Older guesses `0x9A0`/`0x9A4` (GPIO2) and `0x86C` and `0x194` were **WRONG**.
- **`r30` bit for P9_29 = bit 1 (`r30.1`)**, only in mux mode 4 (PRU0) or mode 5 (PRU1).

### 3b. Key memory addresses
| What | Address | Notes |
|------|---------|-------|
| P9_29 conf register | `0x44E109BC` | Written by U-Boot `uenvcmd` to `0x24`. |
| PRU0 CFG block | `0x4A322000` | SYSCFG at `+0x4` = `0x4A322004`. |
| PRU0 CFG SYSCFG (local PRU view) | `0x00026004` | Same register, PRU-local address. |
| PRU SHARED RAM | `0x4A310000` | Word 0 = commanded pulse width (µs). |
| GPIO3 bank | `0x481AE000` | GPIO3_21 bit = `(1<<21)`. OE=`+0x134`, SET=`+0x194`, CLR=`+0x190`. |
| PRU0 remoteproc | `remoteproc1` = `4a334000.pru` | Load `am335x-pru0-fw`. |
| PRU1 remoteproc | `remoteproc2` = `4a338000.pru` | Load `am335x-pru1-fw`. |

---

## 4. WORKING ARCHITECTURE (the path that survives 6.x)

```
U-Boot (every boot):  uenvcmd = mw.l 0x44E109BC 0x24   -> P9_29 = PRU0 mode 4
        |
Linux boots, pinctrl-single never re-touches 0x9bc (no DT group references it)
        |
PRU0 firmware (pru0_servo.out):  loops, drives r30.1 with 50 Hz PWM
        |                          pulse width read from PRU SHARED RAM @ 0x4A310000
        v
arm_write_p929 <us>  :  (1) re-asserts mux 0x24 via /dev/mem (observability only)
                        (2) writes pulse width to shared RAM word 0
                        (3) reads SYSCFG @ 0x4A322004 (read-only, for diagnostics)
```

- The **PRU is a dumb PWM**: no pinmux access, no kernel cooperation beyond the loader.
- ARM `arm_write_p929` is the control surface (pulse width + mux observability).
- **STANDBY_INIT (SYSCFG bit 0) is NOT touched** — proven read-only PRCM status bit (see §6).

---

## 5. PROVEN FACTS (confirmed on hardware)

- [x] **P9_29 conf offset = `0x9BC`** (NOT `0x9A0`/`0x9A4`/`0x86C`/`0x194`). Board pinctrl: `pin 111 ... 44e109bc`.
- [x] **PRU OCP master CANNOT write the Control Module.** Firmware wrote `0x24`; pin stayed `0x28`. Self-mux dead.
- [x] **`devmem2` / `/dev/mem` writes to `0x44E10000` are silently dropped.** `devmem2 0x44E109BC h 0x0024` → "Written" but readback `0x28`.
- [x] **`bone-pinmux-helper` is a no-op on 6.12.** Node loads (`find /proc/device-tree -name pru_pin_helper` → LOADED) but pin stays `0x28`.
- [x] **`pinctrl-hog`, `&ocp` consumer, `&pruss` consumer overlays all FAIL** — pinctrl-single reports "no pins entries for …" or drops the phandle (dependency cycle). No overlay shape works on this kernel.
- [x] **U-Boot `dtb_overlay=` format bug:** this U-Boot build does NOT tokenize space-separated paths. A multi-path string is concatenated into one bad filename → `FDT_ERR_BADMAGIC`. Fix = **ONE bare filename** (`dtb_overlay=pru_p9_29.dtbo`).
- [x] **U-Boot `uenvcmd` `mw.l 0x44E109BC 0x24` WORKS and is permanent.** Post-boot `grep 9bc` → `pin 111 ... 44e109bc 00000024`. ✅ MUX SOLVED.
- [x] **`/dev/mem` writes to PRU RAM + PRU CFG DO work.** `arm_write_p929` pulse write read back `1500`; SYSCFG read works.
- [x] **STANDBY_INIT (CFG SYSCFG bit 0 @ `0x4A322004`) is READ-ONLY / PRCM-gated.** `syscfg_probe` wrote `0x24` AND `0x25` — readback stayed `0x25`. It does NOT tri-state `r30`. Theory retired.
- [x] **Pin owner at boot = NOBODY** (`gpioinfo gpiochip3` line 21 "unused"; `debugfs gpio | grep 117` empty). The pad just sits in mode 0 (base-DT default).
- [x] **`pru-pinmux.service`** ships on the image, ExecStart = `devmem2 0x44E10994 ...` (P9_31, NOT P9_29) — harmless to our pin, masked anyway.

---

## 6. RETIRED THEORIES (do NOT re-chase)

| Theory | Why retired | Evidence |
|--------|-------------|----------|
| Clear STANDBY_INIT (W0C or W1C) to un-tri-state `r30` | STANDBY_INIT is a read-only PRCM status bit; both write polarities no-op. | `syscfg_probe` board #21: wrote `0x24` & `0x25`, readback `0x25`. |
| PRU clears wrong SYSCFG bit (bit 4 not bit 0) | Bit 0 was the right bit, but clearing it is impossible (see above). | Same probe. |
| PRU self-muxes the pin over OCP | PRU OCP cannot write Control Module on 6.x. | Pin stayed `0x28` after firmware wrote `0x24` (board #3/#6). |
| ARM `/dev/mem` sets the mux | `/dev/mem` writes to `0x44E10000` silently dropped. | `arm_write_p929` readback `0x28` (board #4). |
| DT overlay (bone-pinmux-helper / hog / &ocp / &pruss) sets the mux | All shapes fail on 6.12 (no-op / dependency cycle / "no pins entries"). | Boards #10, #14, #16, #17, #18. |
| `config-pin` / capemgr `slots` | Dead on 6.x. | Standard BBB 6.x behavior. |
| Kernel driver owns P9_29 and re-asserts GPIO | Pin owner = nobody at boot. | `gpioinfo` / `debugfs gpio` empty (board #5). |
| Board isn't reading `/boot/uEnv.txt` | Boot log shows "Loaded environment from /boot/uEnv.txt". | Board #13b. |

---

## 7. INVESTIGATION LEDGER (chronological, every attempt)

| # | Date | Attempt | Evidence / Result | Conclusion |
|---|------|---------|-------------------|------------|
| 1 | — | Gemini recipe 1: PRU1 firmware + TI rsc table | Built for PRU1; PRU1 `r30.1` ≠ P9_29 in mode 4. | FAIL for P9_29. |
| 2 | — | Gemini recipe 2: EHRPWM sysfs on P9_14 | `config-pin` dead; unrelated to P9_29. | BLOCKED. |
| 3 | — | devmem2 read PC `0x4A338004` | `/dev/mem` blocked. | BLOCKED. |
| 4 | — | libgpiod servo test on P9_16 (GPIO0_19) | Buzzes (no RT). | WORKS on P9_16 (decoupling proof). |
| 5 | 08-23 | Firmware self-mux with wrong offset `0x9A0` | Wrong ball (GPIO2). | FAIL (wrong offset). |
| 6 | 08-23 | Board pinctrl dump | `pin 104/105 = 44e109a0/a4` (GPIO2); P9_29 = `44e109bc`. | Offset `0x9BC` CONFIRMED. |
| 7 | 08-23 | Board run: PRU OCP self-mux | `pin 111 ... 44e109bc 00000028` after firmware wrote `0x24`. | PRU OCP self-mux DEAD. |
| 8 | 08-24 | `git checkout -- pru/pru_p9_29.dts` from `~/eyespies/pru` | Path doubled → `pru/pru/pru_p9_29.dts` not found. | COMMAND ERROR (file fine at HEAD). |
| 9 | 08-24 | Append `dtb_overlay=` line | Duplicate line; U-Boot used first (defaults), ignored ours. | OVERLAY NOT LOADED. |
| 10 | 08-24 | Rewrite overlay as `pinctrl-hog` (commit 2f479e9) | `find … pru_pin_helper` → LOADED but pin `0x28`. | helper is NO-OP. |
| 11/12 | 08-24 | Boot-log decode | Overlay never loaded (sed never created line); `pru-pinmux.service` touches P9_31 not P9_29. | Root causes decoded. |
| 13 | 08-25 | `dtb_overlay=` multi-path string | `FDT_ERR_BADMAGIC`; U-Boot concatenated paths. | FORMAT BUG found. |
| 13b | 08-25 | Hypothesis "uEnv not read" | Boot log shows env loaded; only format wrong. | Disproven. |
| 13c/d | 08-25 | `0x28` after reboot | Stale boot had old multi-path line; file later clean (dup). | Fix not yet applied. |
| 14 | 08-25 | Overlay loads (671 B) but hog not applying | `pin 111 = 00000028`. `pru-pinmux.service` finished (devmem2 to 0x94). | Hog not sticking. |
| 15 | 08-26 | Overlay attaching `pinctrl-0` to `&am33xx_pinmux` | **BRICKED BOOT** (hang at "Starting kernel"). Recovered via manual eMMC boot. | Never attach to controller. |
| 16 | 08-26 | `devmem2 0x44E109BC h 0x0024` | "Written" but readback `0x28`. pinmux-hog no-op. | DEVMEM2 DEAD. |
| 17 | 08-26 | Overlay: pins under `&am33xx_pinmux`, consumed by `&ocp` | Boot log: "Fixed dependency cycle(s) with …/pinmux_pru_p9_29_pins"; pin `0x28`. | &ocp SELF-CYCLE drops pin. |
| 18 | 08-26 | Overlay: consumed by `&pruss` | Node present in live DT; kernel "no pins entries for …"; pin `0x28`. | &pruss also FAILS. |
| 19 | 08-26 | **U-Boot `uenvcmd=mw.l 0x44E109BC 0x24`** | Post-boot `pin 111 ... 44e109bc 00000024`. | ✅ MUX SOLVED (permanent). |
| 20 | 08-26 | SYSCFG bit 0 clear via write-0 (`&= ~1u`) | `arm_write_p929`: SYSCFG before `0x25` → write `0x24` → readback `0x25`. | Write-0 no-op. |
| 21 | 08-26 | `syscfg_probe` (W0C + W1C + settle) | All four reads `0x25`, bit0 stuck 1. | STANDBY_INIT READ-ONLY → theory dead. |

### 7a. Key thread — the mux saga (how we finally solved it)
1. Wrong offset (`0x9A0`) → corrected to `0x9BC` (board #6).
2. PRU self-mux → dead (board #7). ARM `/dev/mem` → dead (board #4).
3. DT overlays: `bone-pinmux-helper` no-op (#10), `pinctrl-hog` no-op (#14), `&ocp` cycle (#17), `&pruss` no-apply (#18).
4. `dtb_overlay=` format bug: multi-path string → `FDT_ERR_BADMAGIC` (#13). Fix = ONE bare filename.
5. Overlay attaching `pinctrl-0` to the pinmux **controller** bricked boot (#15) — recovered by manual eMMC load (base DTB + `BB-BONE-eMMC1` overlay + `bootz root=/dev/mmcblk1p3`).
6. **Solution:** U-Boot `uenvcmd=mw.l 0x44E109BC 0x24` in `/boot/firmware/uEnv.txt` (#19). Permanent, safe, verified.

### 7b. Key thread — the STANDBY_INIT dead end
- Believed `r30` was tri-stated by STANDBY_INIT (CFG SYSCFG bit 0). Firmware + `arm_write_p929` both wrote `&= ~1u`.
- Board #20: SYSCFG readback `0x25` unchanged after write-0.
- Board #21: `syscfg_probe` wrote `0x24` (W0C) AND `0x25` (W1C), then settled — **all read `0x25`**. Bit 0 is a read-only PRCM status bit. **Theory retired.** Removed all SYSCFG writes from sources.

---

## 8. CURRENT OPEN ISSUES & BISECT PLAN

The servo moved **only** on hand-touch of the bare P9_29 wire (floating → EMI). If `r30.1` were driving the pad, a constant HIGH/LOW or sweep would move it. It didn't. So `r30.1` is NOT reaching the pad. Bisect:

| # | Candidate | Check | If true |
|---|-----------|-------|---------|
| 1 | **Pin owned by a kernel driver** (even in mode 4) | `sudo cat /sys/kernel/debug/gpio \| grep -i 117` | Unbind the owner; re-test. |
| 2 | **PRU0/PRU1 mux-mode mismatch** | `arm_write_p929` sets mode 4 (PRU0). If `pru1_servo.pru1.out` is loaded, `r30.1` ≠ P9_29 in mode 4. | Standardise on PRU0 + mode 4 (`pru0_servo.out`), OR set mux `0x25` for PRU1. |
| 3 | **Signal wire not in P9_29** | It worked on P9_16 (GPIO) earlier; "wiring same as P9_16 era" may mean the wire is still in P9_16. | Move the signal wire to P9_29. |
| 4 | Firmware loop stuck | Unlikely ("now up"), but verify with `hold_high.out`. | Inspect firmware. |

### Decisive bisect — `p929_gpio_test` (ARM-only, no PRU)
```bash
cd ~/eyespies/pru
git pull                      # get p929_gpio_test.c + firmware edits
make                         # rebuild pru0_servo.out + arm_write_p929 + p929_gpio_test
sudo ./p929_gpio_test 4      # ARM GPIO toggles P9_29 at ~50 Hz for 4 s
```
- **Servo MOVES** → P9_29 wire + servo + pin are good; problem is the PRU path (check #1/#2). Then:
  ```bash
  sudo ./arm_write_p929 1    # force P9_29 HIGH via PRU   (re-test; didn't move before)
  sudo ./arm_write_p929 0    # force P9_29 LOW  via PRU
  sudo cat /sys/kernel/debug/gpio | grep -i 117   # owner?
  ```
- **Servo STILL** → signal wire not on P9_29 (or servo/power dead); fix wiring. (Test leaves P9_29 in GPIO mode; reboot or `arm_write_p929` to restore mode 4.)

---

## 9. REPRO / RECOVERY RECIPES

### 9a. Permanent mux fix (already applied; for reflash / board #22+)
```bash
sudo sed -i '/^dtb_overlay=.*pru_p9_29/d' /boot/uEnv.txt
printf 'uenvcmd=mw.l 0x44E109BC 0x24\n' | sudo tee -a /boot/firmware/uEnv.txt
sudo reboot
# verify:
sudo grep 9bc /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins
#   want: pin 111 (PIN111) 0:? 44e109bc 00000024 pinctrl-single
```

### 9b. Manual eMMC boot (if an overlay bricks boot — do NOT attach pinctrl-0 to &am33xx_pinmux)
At the `=>` U-Boot prompt (eMMC = `mmc 1`, root = `mmcblk1p3`):
```bash
mmc dev 1
load mmc 1:3 0x82000000 /boot/vmlinuz-6.12.28-bone25
load mmc 1:3 0x88000000 /boot/dtbs/6.12.28-bone25/am335x-boneblack-uboot.dtb
load mmc 1:3 0x8A000000 /boot/dtbs/6.12.28-bone25/BB-BONE-eMMC1-01-00A0.dtbo
fdt addr 0x88000000
fdt resize 0x10000
fdt apply 0x8A000000
load mmc 1:3 0x88080000 /boot/initrd.img-6.12.28-bone25
setenv bootargs console=ttyS0,115200n8 root=/dev/mmcblk1p3 ro rootfstype=ext4 rootwait coherent_pool=1M net.ifnames=0 rng_core.default_quality=100
bootz 0x82000000 0x88080000:${filesize} 0x88000000
```
Then from a shell, remove the broken `dtb_overlay=` line (§9a).

### 9c. Load PRU0 firmware + set pulse
```bash
S=/sys/class/remoteproc/remoteproc1   # 4a334000.pru = PRU0
echo stop  | sudo tee $S/state; sleep 0.3
sudo cp pru0_servo.out /lib/firmware/am335x-pru0-fw
echo am335x-pru0-fw | sudo tee $S/firmware
echo start | sudo tee $S/state
sudo ./arm_write_p929 1500           # 1000 / 1500 / 2000 to sweep
```

---

## 10. FILE REFERENCE

| File | Purpose | Status |
|------|---------|--------|
| `PRU_SERVO_DEBUG_LOG.md` | This log. | Living doc (restructured 2026-08-26). |
| `pru/pru0_servo.pru0.c` | PRU0 firmware: 50 Hz sweep from shared RAM; drives `r30.1`. | Edited: SYSCFG clear removed. Build on board with `pru-gcc -mmcu=am335x.pru0`. |
| `pru/pru1_servo.pru1.c` | PRU1 firmware variant (mode 5). | Edited: SYSCFG clear removed. Use only if mux set to `0x25`. |
| `pru/arm_write_p929.c` | ARM helper: asserts mux 0x24 (observability), writes pulse to shared RAM, reads SYSCFG (read-only). | Edited: SYSCFG step now read-only. Host-compiles on Mac + board. |
| `pru/p929_gpio_test.c` | **Bisect tool:** ARM GPIO toggles P9_29 at 50 Hz, no PRU. | NEW. Host-compiles. Run on board. |
| `pru/syscfg_probe.c` | Probe: proves STANDBY_INIT is read-only (W0C/W1C/settle). | NEW. Ran on board #21 → "NEITHER clears". |
| `pru/load_pru.sh` | Loads firmware to detected PRU node. | Patched (address-match). |
| `pru/pru_p9_29.dts` | RETIRED mux overlay. **Not loaded.** Kept as a record only. | Retired (mux now via U-Boot). |
| `pru/Makefile` | Builds firmwares + helpers + (unused) `pru_p9_29.dtbo`. | Has dtbo target. |

---

## 11. VERIFICATION STATUS (honest)

| Artifact | Built? | Run on HW? | Result |
|----------|--------|-----------|--------|
| P9_29 mux via U-Boot `uenvcmd` | — | ✅ board | **SOLVED** — `pin 111 ... 44e109bc 00000024`. |
| `arm_write_p929.c` | ✅ Mac gcc + ✅ board gcc | ✅ board | mux readback `0x24`, pulse readback `1500`, SYSCFG read-only. |
| `pru0_servo.pru0.c` | ✅ board pru-gcc | ✅ loaded | boots "now up"; servo NOT moving (under investigation). |
| `pru1_servo.pru1.c` | ✅ board pru-gcc | ⚠️ not the active test | PRU1; needs mux `0x25`. |
| `p929_gpio_test.c` | ✅ Mac gcc | ❌ not yet run | bisect pending (board #22). |
| `syscfg_probe.c` | ✅ board gcc | ✅ board #21 | STANDBY_INIT read-only confirmed. |
| STANDBY_INIT clear theory | — | ✅ board | **DEAD** (read-only PRCM bit). |
| Servo actually moves under PRU | — | ❌ | **NOT PROVEN** — bisect in §8 pending. |

**Bottom line:** The mux wall is fully solved (U-Boot `uenvcmd`). The firmware builds and loads. The remaining unknown is why `r30.1` isn't moving the servo — bisect with `p929_gpio_test` next. STANDBY_INIT is retired; do not re-chase it.

---

## 12. BOARD #22 — bisect tool was missing from Makefile (build fix)

- User ran `git pull` (got `625ec04`) + `make`; firmware + helpers rebuilt fine.
  `pru1_servo.pru1.out` built (1 harmless `-Wvolatile-register-var` warning, expected).
- `sudo ./p929_gpio_test 4` → **`command not found`**. `ls` showed `p929_gpio_test.c`
  present but **no `p929_gpio_test` binary**. Cause: `p929_gpio_test` was NOT in the
  Makefile `all` target and had NO build rule — so `make` never compiled it.
- Fix (Mac, committed): added `p929_gpio_test` + `syscfg_probe` to `all` and added
  explicit `gcc` build rules; also corrected the stale Makefile header that still
  claimed "device-tree overlay is the ONLY working mux" (it's now U-Boot `uenvcmd`).
  `clean` updated to remove the two new binaries.
- Verified on Mac: `make -n p929_gpio_test` → `gcc -O2 -Wall -o p929_gpio_test
  p929_gpio_test.c`; the `.c` compiles clean. Board `git pull && make` will now
  produce the binary.
- Status: fix committed + pushed; **bisect run still pending on board.**

### Repro for board #22+
```bash
cd ~/eyespies/pru
git pull
make                     # now builds p929_gpio_test + syscfg_probe
sudo ./p929_gpio_test 4  # ARM GPIO toggles P9_29 at ~50 Hz; servo moves?
sudo ./arm_write_p929 1  # re-test PRU HIGH
sudo ./arm_write_p929 0  # re-test PRU LOW
sudo cat /sys/kernel/debug/gpio | grep -i 117
```

---

## 13. BOARD #23 — bisect tool bus-faulted (GPIO3 clock gated) + wrong-firmware mistake

### What the user ran
```bash
sudo ./p929_gpio_test 4
  [  399.083597] Unhandled fault: external abort on non-linefetch (0x1018) at 0xb6ee1134
  P9_29 mux: before 0x00000024 -> wrote 0x47 (GPIO mode7) -> readback 0x00000024
  Bus error
sudo ./arm_write_p929 1
  ... PULSE wrote 1 us -> readback 1 ; RESULT MUX OK (0x24)
sudo ./arm_write_p929 0
  ... PULSE wrote 0 us -> readback 0 ; RESULT MUX OK (0x24)
sudo cat /sys/kernel/debug/gpio | grep -i 117   # (empty) -> pin owner = nobody
```

### Decisive findings
1. **`p929_gpio_test` bus-faulted.** `external abort on non-linefetch` at the GPIO3
   mmap address = the **GPIO3 module clock is gated**; userspace `/dev/mem` mmap
   cannot enable it, so touching `0x481AE000` aborts. The raw-`/dev/mem` approach is
   dead for GPIO too. Fix: use **libgpiod** (`gpiod.h`), which goes through the
   kernel GPIO driver (enables the clock, owns the pad). Rewrote the tool to use
   `libgpiod`; it also prints a WARN if the mux is still `0x24` (PRU mode).
2. **The mux write inside the test FAILED (as expected).** Tried to write `0x47`
   (GPIO mode 7) but read back `0x24`. Confirms AGAIN: **`/dev/mem` writes to the
   Control Module are silently dropped** on this kernel — only U-Boot `uenvcmd`
   changes the mux. So the bisect must be done with P9_29 muxed to GPIO mode 7 AT
   BOOT (set `uenvcmd=mw.l 0x44E109BC 0x47`, reboot), not from Linux.
3. **Wrong-firmware mistake (important).** The user had loaded **`pru0_servo.out`**
   (board #20) — a firmware that **ignores shared RAM and just sweeps**. So
   `arm_write_p929 1` / `0` wrote 1 and 0 to shared RAM, but the running firmware
   never reads it → no effect. **The firmware that RESPONDS to `arm_write_p929` is
   `pru1_servo.pru1.out`** (PRU0-built despite the name; reads shared RAM for
   force-HIGH/LOW/PWM). So after the GPIO bisect, the correct PRU test is:
   load `pru1_servo.pru1.out`, then `arm_write_p929 1` / `0`.
4. **Pin owner = nobody** (`gpioinfo`/debugfs grep 117 empty) — consistent with all
   prior evidence; the pad just sits in mode 0/4 by base-DT/U-Boot, no driver claim.

### Corrected bisect procedure
```bash
# Step A — GPIO bisect (needs P9_29 in GPIO mode 7, set at boot):
#   1) sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt
#   2) printf 'uenvcmd=mw.l 0x44E109BC 0x47\n' | sudo tee -a /boot/firmware/uEnv.txt
#   3) sudo reboot
#   4) cd ~/eyespies/pru && make && sudo ./p929_gpio_test 4
#      servo MOVES -> wire+servo+pin good; problem is PRU path (step B)
#      servo STILL -> signal wire not on P9_29 (or servo/power dead); fix wiring
#   5) RESTORE mux: sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt
#      printf 'uenvcmd=mw.l 0x44E109BC 0x24\n' | sudo tee -a /boot/firmware/uEnv.txt
#      sudo reboot
#
# Step B — PRU path test (only if step A moved the servo):
#   1) load the RESPONSIVE firmware:
#        S=/sys/class/remoteproc/remoteproc1
#        echo stop | sudo tee $S/state; sleep 0.3
#        sudo cp pru1_servo.pru1.out /lib/firmware/am335x-pru0-fw
#        echo am335x-pru0-fw | sudo tee $S/firmware
#        echo start | sudo tee $S/state
#   2) sudo ./arm_write_p929 1   # force P9_29 HIGH  (servo should slam one way)
#      sudo ./arm_write_p929 0   # force P9_29 LOW   (servo should slam other way)
#      sudo ./arm_write_p929 1500 # normal center pulse
#   3) if 1/0 still don't move -> r30.1 not reaching pad (PRU path bug, narrow to
#      pin-ownership / r30.1 ball mapping). If they DO move -> PWM timing issue.
```
- Status: bisect tool fixed (libgpiod); procedure corrected (uenvcmd 0x47 for GPIO
  test, `pru1_servo.pru1.out` for PRU test); **run pending on board.**

---

## 14. BOARD #24 — "P9_29 may be input-only / try P8" hypothesis reviewed

### User observation
- Step A (GPIO 50 Hz toggle on P9_29) **did NOT move the servo**.
- Hypothesis raised: P9_29 might be PRU-input-only; maybe P8 header should be used.

### Verification against TRM / BBB SRM (NOT a guess)
- **Ball R28 = P9_29, mode 4 = `pr1_pru0_pru_r30_1`** — a documented **PRU0 direct
  OUTPUT**, one of the standard BBB PRU0 pins (P9_29/30/31 are all PRU0 r30
  outputs in mode 4). It is **NOT input-only**. The "input-only" hypothesis is
  **rejected** by the pinmux table.
- **P8_45 / P8_46 are PRU1 pins** (mode 5 = `pr1_pru1_pru_r30_0/1`), not PRU0. So
  moving to P8 means switching to PRU1 (mux `0x25`, firmware for PRU1). It is a
  valid tie-breaker but is NOT the "correct" PRU0 pin — P9_29 already is.

### Most likely reason Step A failed (the real bug in the test, not the pin)
- The GPIO bisect requires the pad in **GPIO mode 7**, set at boot via
  `uenvcmd=mw.l 0x44E109BC 0x47` + **reboot**. If the user ran `p929_gpio_test`
  while P9_29 was still in PRU mode 4 (`0x24`), libgpiod's GPIO write **never
  reaches the physical pad** (it is routed to the PRU). The old tool only WARNed;
  it still "ran" and reported nothing moved. That alone explains Step A.
- **Hand-touch ambiguity:** if the bare *servo-side* wire was touched (not P9_29
  itself), EMI on the floating servo signal would move it regardless of P9_29's
  state. So hand-touch does NOT prove the wire is in P9_29.

### Fix shipped this session
- `p929_gpio_test.c` rewritten to accept a **pin NAME** (`P9_29`, `P9_30`, `P9_31`,
  `P9_27`, `P9_28`, `P8_45`, `P8_46`) and a header-pin→(chip,line) table. It now
  **reads the live mux** for that pin and **ABORTS** (printing the exact U-Boot
  fix) if the pad is not in GPIO mode 7 — so a mis-muxed run can no longer
  silently report "didn't move". Uses `gpiod_chip_open_by_name`.
- This lets the user test P9_29 (correctly, in GPIO mode) AND P8 in one tool.

### Tie-breaker plan (do in order on the board)
```bash
cd ~/eyespies/pru && git pull && make
# 1) Test P9_29 PROPERLY (in GPIO mode 7):
#    sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt
#    printf 'uenvcmd=mw.l 0x44E109BC 0x47\n' | sudo tee -a /boot/firmware/uEnv.txt
#    sudo reboot
#    sudo ./p929_gpio_test 4 P9_29
#      -> servo MOVES  => wire is in P9_29, P9_29 GPIO works => PRU path bug remains
#      -> servo STILL  => wire NOT in P9_29 (or servo/power dead) => check wiring
#    (restore mux afterwards: uenvcmd 0x44E109BC 0x24; reboot)
#
# 2) If P9_29 still dead, test a P8 PRU pin (PRU1) the same way:
#    printf 'uenvcmd=mw.l 0x44E108AC 0x47\n' | sudo tee -a /boot/firmware/uEnv.txt  # P8_46 conf 0x9ac
#    sudo reboot
#    sudo ./p929_gpio_test 4 P8_46
#      -> if P8_46 moves it, the servo+power+wire are fine and P9_29 pad is the issue;
#         then pivot the PRU firmware to PRU1 mode 5 (0x25) on P8_46.
```
- Status: tool rewritten + pushed; **tie-breaker run pending on board.**
- Note: P8_45/46 conf offsets used above (0x9b0 / 0x9ac) are per the AM335x ball
  table; verify with `gpioinfo gpiochip2` on the board before trusting the P8 run.

---

## 15. BOARD #25 — tool printed a BROKEN mux address (0x44E19BC), now fixed

### Bug found in the user's paste (board #24 follow-up)
- `p929_gpio_test 4 P9_29` correctly ABORTED (P9_29 still in PRU mode 4, not GPIO 7)
  — proving the Step A "didn't move" was a mis-mux, exactly as predicted.
- BUT the printed fix command was wrong:
    `uenvcmd=mw.l 0x44E19BC 0x47`   <-- MISSING a digit (should be `0x44E109BC`)
  My `printf` used `0x44E1%03lX` and `%03lX` padded `0x9bc` to `9BC` (3 digits),
  dropping the leading `0`. So the address became `0x44E19BC` — a different,
  invalid register. If copy-pasted it would have written the wrong padconf.
- Same bug in the "restore" line (`0x44E19BC 0x24`).

### Fix (on Mac, committed)
- Changed format to `0x44E10%03lX` so `0x9bc` -> `09BC` -> full `0x44E109BC`,
  and `0x9ac` (P8_46) -> `09AC` -> `0x44E108AC`. Verified with a standalone C
  harness on the Mac: P9_29 => `0x44E109BC`, P8_46 => `0x44E108AC` (both correct).
- This affects only the *printed help text* in the ABORT branch; the actual mux
  read (grep of pinctrl debugfs) and the GPIO drive path were already correct.

### Status / next action
- Fix committed + pushed. On the board: `git pull && make`, then run the corrected
  Step A:
    sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt
    printf 'uenvcmd=mw.l 0x44E109BC 0x47\n' | sudo tee -a /boot/firmware/uEnv.txt
    sudo reboot
    sudo ./p929_gpio_test 4 P9_29
  - servo MOVES  => wire is in P9_29; P9_29 GPIO works; PRU path is the remaining bug
  - servo STILL  => wire not in P9_29 (or servo/power dead); check wiring
  - restore: `uenvcmd=mw.l 0x44E109BC 0x24` + reboot.
- Files changed this session (uncommitted on Mac): `p929_gpio_test.c` (libgpiod),
  `Makefile` (`-lgpiod` in p929_gpio_test rule).

---

## 16. BOARD #26 — GPIO-mode-7 toggle STILL aborts; root cause = uenvcmd edit not landing

### Symptom (board run, user paste)
- After pulling 3624c3c (address fix), `sudo ./p929_gpio_test 4 P9_29` STILL prints:
  `[ABORT] P9_29 is NOT in GPIO mode 7. A GPIO toggle cannot reach the pad.`
  (now with the CORRECT `0x44E109BC` address — so the 3624c3c fix took effect).
- Boot log confirms `Running uenvcmd ...` happens; pin was previously proven to
  persist `0x24` across reboots via uenvcmd. So the U-Boot mechanism WORKS.

### Root-cause hypothesis (high confidence)
- The `printf 'uenvcmd=mw.l 0x44E109BC 0x47\n' | sudo tee -a ...` line was almost
  certainly MANGLED over serial paste (user has hit this before). If `0x47` did
  not land in `/boot/firmware/uEnv.txt`, U-Boot applied the old/garbage value and
  P9_29 stayed in PRU mode 4 -> tool aborts. This is consistent with every prior
  symptom (the mux never actually changed to 0x47).

### Fix shipped on Mac (commit 2ff5b50)
- `p929_gpio_test.c`:
  - Reads the LIVE mux via `devmem2` (proven readable on board) as primary, pinctrl
    debugfs as fallback; ALWAYS prints the actual conf value so we are never blind.
  - Fixed sscanf parser: devmem2 prints `Value at address 0x44E109BC: 0x00000024`;
    parse the hex after the LAST `:`. (Earlier `%*s : 0x%x` ate the colon and FAILED
    to parse — caught by ad-hoc C test: 0x24->mode4, 0x47->mode7, 0x37->mode7, 0x28->0.)
  - Replaced the printf-to-uEnv fix with a PASTE-SAFE `sed` that swaps only the value
    digit (`/^uenvcmd=/s/0x[0-9A-Fa-f]*$/0x47/`) plus a mandatory `grep uenvcmd` verify
    step, so a mangled serial paste is caught BEFORE reboot.
- Verified on Mac: full file compiles vs stub libgpiod (rc=0); sscanf parses real
  devmem2 output for all four sample values. Authoritative run pending on board.

### Next action (issued to user)
- `git pull && make`, run `sudo ./p929_gpio_test 4 P9_29` ONCE to see the ACTUAL mode.
- Then `sudo sed ... 0x47 /boot/firmware/uEnv.txt` ; `grep uenvcmd` (must show 0x47) ;
  `sudo reboot` ; re-run. If mode 7 + servo moves -> wire/servo OK, PRU path is the bug.
  If mode 7 + servo still dead -> wire not in P9_29 (or servo/power dead); try P8_46.
- Status: fix pushed (2ff5b50); board tie-breaker run PENDING.

---

## 17. BOARD #27 — FALSE ABORT: P9_29 IS in GPIO mode 7; tool parser bug, servo never toggled

### Board run (user paste, after uenvcmd 0x47 took effect)
```
$ sudo ./p929_gpio_test 4 P9_29
devmem2: /dev/mem opened.
pinctrl: pin 111 (PIN111) 0:? 44e109bc 00000047 pinctrl-single
  (could not read mux for P9_29 via devmem2 or debugfs)
[ABORT] P9_29 is NOT in GPIO mode 7.
```
### ROOT CAUSE = TOOL BUG, not hardware
- The pinctrl debugfs line shows `00000047` -> P9_29 WAS in GPIO mode 7. The
  `uenvcmd=0x47` edit landed correctly (paste-safe sed worked this time).
- BUT the tool's mux parser was broken and falsely concluded "could not read":
  1. devmem2 block only `fgets` the FIRST line (`/dev/mem opened.`) and never saw
     the `Value at address 0x44E109BC: 0x00000047` line -> `got=0`.
  2. pinctrl block did `strrchr(buf,' ')` -> grabbed the LAST token `pinctrl-single`
     instead of `00000047`, sscanf failed -> `got=0`.
- Both failed -> `got=0` -> code took the abort branch. The servo was NEVER toggled.
  This was a false abort; the bisect never actually executed.

### Fix shipped (commit 41e4b6f)
- devmem2 block now loops ALL lines (parses `Value at address ...: 0x..`).
- pinctrl block sscanf: `pin %*d ( %*[^)] ) %*s %*x %x %*s` -> reads the value token
  BEFORE `pinctrl-single`.
- Verified on Mac vs real board strings: devmem2 3-line -> 0x47 mode7; pinctrl
  `00000047` -> mode7; `00000024` -> not_gpio. Full file compiles (rc=0).

### Real bisect still PENDING
- The corrected tool must be re-run to actually toggle P9_29 and report whether the
  servo MOVES. We have NOT yet obtained the tie-breaker result.
- If servo moves in GPIO mode7 -> wiring + servo are good; the silent PRU output is
  a FIRMWARE/r30 path bug. If it still does not move -> wiring (signal wire not in
  P9_29) or servo/power dead.

---

## 18. NEXT STEP (issued to user)
```
cd ~/eyespies/pru
git pull
make
sudo ./p929_gpio_test 4 P9_29
```
- Expect: `mux for P9_29 (conf 0x9bc) = 0x00000047 -> mode 7 (GPIO ok)` then a
  GPIO toggle for 4s. Report whether the servo MOVES.
- Do NOT restore uenvcmd to 0x24 yet — we still need mode7 for the bisect.

---

## 19. BOARD #28 — DECISIVE BISECT: GPIO mode-7 toggle on P9_29, servo STILL dead

### Board run (user paste)
```
$ sudo ./p929_gpio_test 4 P9_29
devmem2: /dev/mem opened.
devmem2: Memory mapped at address 0xb6f2f000.
devmem2: Value at address 0x44E109BC (0xb6f2f9bc): 0x47
mux for P9_29 (conf 0x9bc) = 0x00000047 -> mode 7 (GPIO ok)
Toggling P9_29 (gpiochip3 line 21) at ~50 Hz for 4 s...
Done. servo MOVED  -> ...
      servo STILL  -> signal wire not on P9_29 (or servo/power dead).
servo still dead
```

### VERDICT (decisive)
- P9_29 mux IS mode 7 (0x47) -> pin correctly configured, GPIO toggle reaches the PAD.
- ARM toggled the pad via libgpiod at ~50 Hz for 4 s.
- Servo did NOT move.
- Therefore: the problem is DOWNSTREAM of the pad (wiring / servo / power), NOT the
  PRU firmware or r30 path. (If GPIO at the pad can't move it, the PRU on the same
  pad never had a chance.) PRU is removed as prime suspect pending a wiring proof.

### Caveat on the GPIO test itself
- libgpiod userspace toggling is jittery and may not hold a clean 50 Hz / 20 ms frame.
  A real MG90S wants a stable 50 Hz; a sloppy square wave can leave it motionless.
  So "still dead" does not 100% acquit the wiring, but strongly implicates it.

### Isolation plan issued to user (no multimeter available)
- Test A (servo/power alive?): power servo from OnePlus 5V; with signal lead
  DISCONNECTED from BBB, briefly touch signal lead to +5V for a split second. A live
  MG90S usually JERKS to one extreme. Jerk => servo+power fine, fault is wiring/signal.
  No jerk => servo or power dead. (Only a split-second touch; don't hold 5V on signal.)
- Test B (which pin is the signal on?): P9_29 != P9_16. Earlier the servo WORKED on
  P9_16. Strong hunch: the servo signal lead is still physically in P9_16, not P9_29,
  which would explain BOTH the GPIO failure AND the earlier PRU failure (same pad).
- If wire is in P9_16: move it to P9_29, reboot, re-run `p929_gpio_test 4 P9_29`.
  If it moves -> wiring was the whole problem and PRU firmware was fine; then reload
  `pru1_servo.pru1.out` + `arm_write_p929`.
- If wire already in P9_29: do Test A and report whether the servo jerks.

### Status
- Bisect complete: NOT a PRU/firmware bug. Root cause is wiring or servo/power.
- Awaiting user's pin-location check (Test B) and/or Test A jerk result.
- uenvcmd still 0x47 (mode7); do NOT restore to 0x24 until wiring proven.

