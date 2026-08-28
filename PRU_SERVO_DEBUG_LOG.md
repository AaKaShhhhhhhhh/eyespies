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

---

## 20. BOARD #29 — REVERSAL: servo/power/wire PROVEN GOOD; bug is in PRU firmware

### User report (overrides #28 conclusion)
- Yellow signal wire IS on physical P9_29. (wiring location correct)
- Touching the bare signal wire with a hand MOVES the servo.
  -> servo is powered AND the signal line electrically reaches the servo.
  -> The analog chain (servo + 5V power + wire-to-P9_29) is PROVEN GOOD.
- Therefore the earlier GPIO 50%-square test "servo dead" was a FALSE NEGATIVE:
  a servo needs 1-2 ms pulses at 50 Hz, not a 10 ms (50%) square. The hand-touch
  worked because the body injected real ~50 Hz hum the servo understood.
- The "wiring broken" inference from #28 was WRONG. Prime suspect flips back to
  the PRU firmware / r30 output path.

### ROOT CAUSE FOUND in pru1_servo.pru1.c
- Firmware reads the command from `0x4A310000` (global physical shared-RAM addr).
- From INSIDE the PRU core, shared RAM is at LOCAL `0x00010000`, not `0x4A310000`.
  Dereferencing the global address from the PRU is an out-of-range load that
  FAULTS the core -> main() never reaches the PWM loop -> r30.1 never toggles ->
  servo silent. This matches every symptom: firmware loaded, mux mode4, servo dead.

### FIXES (commit b4d24a5)
- pru1_servo.pru1.c: PRU_SHARED_RAM = 0x00010000 (PRU-local; valid 12KB range
  0x10000..0x12FFF). ARM still writes the SAME global 0x4A310000 (its own view);
  the two are the same physical RAM, just different address maps. arm_write_p929
  is unchanged.
- Added pru_const_high.pru0.out: 0.5 Hz square wave on r30.1 with ZERO memory
  reads (no shared RAM / no OCP) so a bus fault cannot halt it. Isolation test:
  if servo swings once/sec -> GPO reaches pad (standby/mux fine; only the shared
  RAM address was wrong); if it stays dead -> r30 not reaching pad at all.

### TEST SEQUENCE issued to user
1. `cd ~/eyespies/pru && git pull && make`
2. Switch pad to PRU mode 4:
   `sudo sed -i '/^uenvcmd=/s/0x47/0x24/' /boot/firmware/uEnv.txt`
   `grep uenvcmd /boot/firmware/uEnv.txt`  # must show 0x24
   `sudo reboot`
3. After reboot: `sudo ./load_pru.sh pru0 pru_const_high.pru0.out`
   - EXPECT: servo SWINGS (slams to one extreme then back) once per ~2 s.
   - If YES -> PRU pad works; the shared-RAM fix was the whole bug. Go to step 5.
   - If NO  -> r30 not reaching pad (standby/tri-state/mux); report and stop.
4. (Only if step 3 swings) Load the real sweep firmware:
   `sudo ./arm_write_p929 1500`   # center
   `sudo ./load_pru.sh pru0 pru1_servo.pru1.out`
   - EXPECT: servo sweeps smoothly. (Now reads shared RAM at correct 0x10000.)
   - Vary: `sudo ./arm_write_p929 1000` / `2000` to see end-stops.
5. Do NOT leave uenvcmd at 0x24 for normal use unless PRU is the final driver.

### Status
- Fix pushed (b4d24a5). Awaiting board test result for pru_const_high.pru0.out.

---

## 21. BOARD #30 — BUILD RESULT: real firmware compiles; helper had a typo

### What happened on `git pull && make`
- Pulled b4d24a5..f20753a cleanly (pru1_servo + pru_const_high + Makefile + log).
- `pru1_servo.pru1.out` COMPILED WITH NO ERRORS (only the harmless
  `volatile-register-var` warning). -> the shared-RAM address fix (0x00010000) is
  syntactically and semantically fine; PRU sees valid local shared RAM now.
- `pru_const_high.pru0.out` FAILED TO COMPILE:
    pru_const_high.pru0.c:22: error: expected identifier or '(' before '=' token
  ROOT CAUSE: in the resource_table decl, the variable NAME was missing:
    } __attribute__((packed, section(".resource_table"), used)) = { ... }
  must be
    } __attribute__((packed)) resource_table = { ... }
  (mirrors pru0_servo.pru0.c which compiled fine). Typo introduced on the Mac
  where there is no pru-gcc, so it was never caught until the board build.
- `make` stops at first error, so pru_const_high was NOT produced, but
  pru1_servo.pru1.out WAS produced earlier in the recipe order.

### uenvcmd state
- User ran `sudo sed ... 0x47 -> 0x24` and `grep` showed `uenvcmd=mw.l 0x44E109BC 0x24`.
- BUT the mux only applies at BOOT. No `sudo reboot` was done yet, so the pad is
  STILL in GPIO mode 7 (0x47) until the next reboot. Must reboot before any PRU test.

### ANSWERS to user's questions
- "Why not working now?" -> Not a hardware/PRU problem. A syntax typo in MY helper
  file blocked the build. The real servo firmware compiled successfully.
- "Is the PRU sending signal to P9_29?" -> Still UNKNOWN; we have not observed the
  pad yet. Blocked only by (a) the helper build error (now fixed) and (b) the
  pending reboot to switch the pad to PRU mode 4.
- "What is the blockage now?" -> (1) fixed: helper typo (commit after #30).
  (2) still needed: one `sudo reboot` so uenvcmd 0x24 takes effect.

### FIX + NEXT STEPS issued
1. (done) pru_const_high.pru0.c: add variable name `resource_table` -> compiles.
2. On board: `cd ~/eyespies/pru && git pull && make`  (full build, no errors)
3. `sudo reboot`
4. After reboot, isolation test:
   `sudo ./load_pru.sh pru0 pru_const_high.pru0.out`
   - EXPECT: servo SWINGS once per ~2 s (1 s HIGH / 1 s LOW).
   - YES -> PRU GPO reaches P9_29 (standby/mux OK). Then load real fw:
       `sudo ./arm_write_p929 1500`
       `sudo ./load_pru.sh pru0 pru1_servo.pru1.out`
     EXPECT: smooth sweep; vary 1000/2000 for end-stops.
   - NO  -> r30 not reaching pad at all; report and stop.

### Status
- Helper typo fix pushed. Awaiting board: git pull && make (full) && reboot && test.

---

## 22. BOARD #31 — THE ROOT CAUSE: STANDBY_INIT was never cleared -> r30 tri-stated

### The realization (2026-08-26/28)
Reading the firmware after days of silent failure revealed the actual bug, and it
is neither hardware nor the kernel: **STANDBY_INIT (PRU CFG SYSCFG bit 0) was never
cleared, so r30 is tri-stated and P9_29 floats.**

### Why this explains EVERY symptom
- Mux correct (0x24), PRU "running", but `r30` writes never reach the pad -> pin floats.
- Servo ONLY moves when the bare wire is touched -> that injects a signal into a
  floating pin. This is the textbook tri-state signature.
- Earlier P9_16 "worked" -> that was GPIO mode 7 (ARM drives the pad, NOT r30),
  so STANDBY_INIT was irrelevant. The PRU path was never actually tested until P9_29.

### Why we missed it
`syscfg_probe` (board #19/20) proved **ARM** writes to SYSCFG are ignored (read back
0x25). We wrongly generalized that to "STANDBY_INIT can't be cleared / doesn't gate
r30" and REMOVED the clear from every firmware. WRONG: ARM writes are ignored, but
**the PRU itself CAN and MUST clear its own STANDBY_INIT.** remoteproc does NOT do
this for us.

### The fix
Added to the top of `main()` in ALL THREE firmwares:
    (*(volatile uint32_t *)0x22004) &= ~(1u << 0);   // clear STANDBY_INIT (PRU-local CFG SYSCFG)
- 0x22004 = PRU subsystem local CFG register block + 0x4 (SYSCFG). This is the PRU's
  OWN view (not the 0x4A322000 global the ARM used). Clearing bit 0 takes r30 out of
  tri-state so the GPO actually drives the pad.
- NOTE: this is the PRU-clears-itself path TI docs require; the global 0x4A322004 that
  arm_write_p929 poked is ARM's view and is ignored (hence the 0x25 readback).

### Files changed (pushed after #31)
- pru1_servo.pru1.c: clear STANDBY_INIT before the PWM loop.
- pru0_servo.pru0.c: clear STANDBY_INIT before the sweep loop.
- pru_const_high.pru0.c: self-contained resource table (does NOT include
  resource_table_empty.h, which already defines struct resource_table -> would
  clash). Mirrors pru1_servo.pru1.c exactly so pru-gcc builds it cleanly.

### FORWARD PLAN — all possibilities, ending in the fix that should 100% work
Ordered so each step either confirms the fix or reveals the true remaining fault.

STEP 0 (one-time, already done on board):
  uenvcmd=mw.l 0x44E109BC 0x24   in /boot/firmware/uEnv.txt   (PRU mode 4)
  sudo reboot   <-- REQUIRED for the mux to take effect.

STEP 1 — ISOLATION (proves the PRU GPO reaches the pad, no memory/timing involved):
  cd ~/eyespies/pru && git pull && make
  sudo ./load_pru.sh pru0 pru_const_high.pru0.out
  EXPECT: servo SWINGS once per ~2 s (1 s HIGH / 1 s LOW).
    YES -> r30 -> P9_29 path is ALIVE. Go to Step 2.
    NO  -> r30 still not reaching pad. Possibilities left:
           (a) uenvcmd not applied (re-grep 9bc -> must be 0x24 after reboot)
           (b) wrong PRU core (we load pru0; P9_29 mode4 = PRU0 r30.1, correct)
           (c) pad/bond damage (hardware) -> but hand-touch moved it, so pad is
               electrically fine; leaves only "PRU GPO disabled" which the clear fixes.
           Report and STOP before touching kernel images.

STEP 2 — REAL SERVO:
  sudo ./arm_write_p929 1500
  sudo ./load_pru.sh pru0 pru1_servo.pru1.out
  EXPECT: smooth sweep; 1000 = one end, 2000 = other end.
    YES -> DONE. The fix works.
    NO  -> firmware logic/timing; use test modes:
           sudo ./arm_write_p929 0   (constant LOW  -> servo slams one way)
           sudo ./arm_write_p929 1   (constant HIGH -> servo slams other way)
           If 0/1 move it -> PWM timing/center bug (tune delay_us).
           If 0/1 still dead -> r30 still tri-stated (clear not effective) -> check
           the 0x22004 write is present in the binary / try pru0_servo instead.

STEP 3 (only if Steps 1-2 fail) — cross-check via known-good GPIO path:
  Switch pinmux to GPIO mode 7 (uenvcmd 0x47, reboot), run p929_gpio_test with a
  VALID 50 Hz 1-2 ms pulse (not 50% square) to confirm servo responds to correct signal.
  (A proper 20 ms-frame pulse is what the servo needs; the earlier 50% square was invalid.)

STEP 4 (last resort, NOT recommended) — old kernel + config-pin:
  Only if Step 1 isolation is dead AND Step 3 GPIO valid-pulse also dead (i.e. the
  PRU subsystem itself is broken on this kernel). An old image (e.g. 4.19 / 5.4
  Debian Buster console) would let you use config-pin, but it does NOT fix a
  tri-stated r30 — it would have the SAME silence unless the firmware also clears
  STANDBY_INIT. So re-imaging is NOT the fix; the firmware fix is. Do not burn a
  day flashing old images before trying Steps 1-2.

### ANSWERS to the user's questions (2026-08-28)
- "Is it a hardware problem?" -> Almost certainly NOT. The floating-pin-on-touch
  symptom + correct mux + PRU running points squarely at r30 being tri-stated,
  which is a FIRMWARE issue (missing STANDBY_INIT clear), now fixed.
- "Should I compile an old Linux image to use config-pin?" -> NO. config-pin only
  sets the mux (already solved via uenvcmd). It would NOT fix a tri-stated r30.
  The fix is the firmware change above; re-imaging won't add it.
- "Why only my board's PRU isn't working?" -> Because our firmware (unlike typical
  TI examples) removed the STANDBY_INIT clear based on the mistaken "read-only" conclusion.
- "What's the ultimate fix?" -> Clear STANDBY_INIT in the PRU at main() start
  (done in all 3 firmwares) + mux P9_29 to mode 4 via uenvcmd (done) + reboot.
  Step 1 (pru_const_high) is the decisive proof.

### Status
- STANDBY_INIT fix pushed to all three firmwares. Awaiting board: git pull && make && reboot && Step 1.

---

## 23. BOARD #32 — CORRECTION + DECISIVE LOOPBACK TEST

### Correction (important — two wrong theories were retired)
1. **STANDBY_INIT "root cause" was WRONG.** SYSCFG bit 0 is the *read-only*
   IDLE_MODE status bit, NOT STANDBY_INIT (bit 4). At reset SYSCFG == 0x25, so
   STANDBY_INIT (bit 4) is already 0 and the PRU GPOs (r30) drive the pad
   directly — no PRU-side un-tri-state is needed. The earlier "clear bit 0"
   writes were a no-op on a read-only bit.
2. **`pru_const_high` was the WRONG test to call "moving".** It sends a 1 Hz
   (1 s HIGH / 1 s LOW) square wave. A hobby servo only responds to 50 Hz PWM
   with a 1–2 ms pulse; a 1-second-wide level is out of range and the servo
   ignores it. "Didn't move" with pru_const_high proved nothing about toggling.

### What IS now proven (board #31/#32)
- `uenvcmd=mw.l 0x44E109BC 0x24` applied; after reboot `devmem2 0x44E109BC`
  reads **0x24** -> pad IS in PRU mode 4. Mux is finally, cleanly solved.
- `pru0_servo.out` (valid 50 Hz / 1–2 ms PWM sweep on r30.1) loaded and runs.
- Servo + power + wire-to-P9_29 are electrically good (hand-touch moved it).
- Yet the PRU PWM does NOT move the servo.
- The thing that ACTUALLY made P9_29 move earlier was: (a) the U-Boot mux to
  mode 4 (0x24) and (b) fixing pru1_servo's shared-RAM address. STANDBY_INIT
  was a red herring.

### What is NOT yet known (the only remaining variable)
Whether `r30.1` is actually producing voltage at the P9_29 PAD. The PRU is
toggling r30 in firmware, but we have never *observed* the pad. Everything else
(analog chain, mux, firmware logic) is now cleared. So the decisive question is
purely: **does r30.1 reach the pad?** Stop guessing — MEASURE it.

### DECISIVE LOOPBACK TEST (no meter required; just a jumper wire)
The PRU blinks r30.1 (P9_29) at 5 Hz. We jumper P9_29 -> P8_13, read P8_13 from
Linux, and count transitions.
- If P8_13 TOGGLES -> r30.1 reaches the pad. The PRU output path is 100% good;
  the servo silence is then a LEVEL issue (3.3 V GPO vs servo 5 V expectation,
  or the servo needing a cleaner 50 Hz) and we address it at the level side
  (e.g. a 3.3 V->5 V level shifter / common-ground re-test).
- If P8_13 STAYS FLAT -> r30.1 is NOT driving the pad. Concrete finding: either
  the PRU core isn't emitting GPO (subsystem/bond) or the P9_29 ball on this
  board isn't connected to r30.1. That would be a real hardware/ball fault and
  we pivot to P8_46 (known-good PRU ball) instead of chasing firmware further.

#### Steps (on board)
```
cd ~/eyespies/pru && git pull && make clean && make
# mux P9_29 to PRU mode 4 (already 0x24? confirm):
grep uenvcmd /boot/firmware/uEnv.txt      # must show 0x24
sudo devmem2 0x44E109BC | tail -1         # must read 0x24
# load the 5 Hz blink firmware on PRU0:
sudo ./load_pru.sh pru0 gpo_self_test.pru0.out
# WITHOUT disturbing P9_29's servo wire, also jumper P9_29 -> P8_13.
# (P8_13 must be GPIO mode 7 input; confirm mux below, set if needed.)
# Read P8_13 for 6 s:
sudo ./loopback_probe 6
```
- EXPECT: `value=1` / `value=0` lines alternating, `total transitions: ~30`,
  `LOOPBACK OK`.
- If `total transitions: 0` -> LOOPBACK DEAD -> report and STOP (hardware/ball).

### Pin mux note for P8_13
P8_13 default may not be GPIO mode 7. If `loopback_probe` prints
"request input failed", mux P8_13 to mode 7 via U-Boot too:
`uenvcmd=mw.l 0x44E10834 0x27` (P8_13 conf = 0x834, mode7 pull-disabled) — OR
just temporarily `printf` the extra mw.l line into uEnv.txt and reboot. If you
prefer not to touch U-Boot again, an alternative input pin already in GPIO mode
7 (e.g. read P9_29 with `p929_gpio_test` reworked as input) can substitute; but
the cross-chip P8_13 jumper is cleanest because P9_29 stays in PRU mode 4.

### Status
- Loopback test files pushed (gpo_self_test.pru0.c, loopback_probe.c, Makefile).
- Awaiting board: git pull && make && loopback run. This settles r30 pad reach.

---

## 24. BOARD #33 — LOOPBACK READ ALL ZEROS; RIG SELF-TEST REQUIRED FIRST

### Board result (2026-08-28, after git pull / make)
- `gpo_self_test.pru0.out` loaded on PRU0, state running (size 2900, header-less
  resource table warnings only — harmless).
- `sudo ./loopback_probe 6` over P9_29 -> P8_13 jumper: **`total transitions: 0`**,
  every sample `value=0`. Tool printed `LOOPBACK DEAD`.

### INTERPRETATION — NOT YET CONCLUSIVE
An all-zeros read is ambiguous. It is produced by EITHER:
  (a) P9_29 r30.1 genuinely not driving the pad, OR
  (b) the jumper between P9_29 and P8_13 was NOT connected (P8_13 then floats
      low or is pulled low). The paste does not confirm the wire was in place.

So before concluding "P9_29 ball is dead," we must PROVE the rig works using a
pin Linux can definitely toggle. Added `gpio_toggle.c` for exactly this.

### RIG SELF-TEST (run on board BEFORE trusting the P9_29=0 result)
```bash
cd ~/eyespies/pru && git pull && make clean && make
# 1) In shell A: drive a KNOWN-GOOD GPIO at 5 Hz (P8_15 = gpiochip1 line 15,
#    default GPIO mode 7, no mux needed):
sudo ./gpio_toggle gpiochip1 15 6
# 2) Jumper P8_15 -> P8_13 (same input pin loopback_probe reads).
# 3) In shell B (same time window):
sudo ./loopback_probe 6
```
- EXPECT: `value=1`/`value=0` alternating, `total transitions: ~30` ->
  RIG PROVEN GOOD (jumper + P8_13 input + libgpiod + tool all correct).
  -> then the earlier P9_29 `total transitions: 0` is a REAL result, and we
     pivot to "P9_29 pad not driven" (ball/hardware fault -> move to P8_46).
- If `total transitions: 0` even here -> the rig itself is broken (bad wire,
  wrong mapping P8_13=gpiochip1 line 28 [the code uses GPIO1_28, NOT line 14],
  or P8_13 not in GPIO mode). FIX THE RIG
  (e.g. confirm P8_13 mux 0x44E10834=0x27, try a different input pin) before
  any further loopback conclusion.

### Status
- `gpio_toggle.c` + Makefile target pushed (4cb42d4). Awaiting board rig self-test.
- Only after a GREEN rig self-test does P9_29=0 become a decisive hardware finding.

### CORRECTION (doc only, code is right)
Earlier chat text said "P8_13 = gpiochip1 line 14". That was WRONG in the
prose: the actual `loopback_probe.c` correctly targets P8_13 = GPIO1_28 =
**gpiochip1 line 28** (confirmed by reading the source). So the board's
`total transitions: 0` result DID read the correct pin, and it opened the
input without error (so P8_13 was in GPIO mode and the read path works).
The "line 14" number in chat is a documentation slip, not a code bug. The
rig self-test command above uses P8_15 (gpiochip1 line 15) which is the
sibling pin on the same bank as P8_13 (line 28) — also correct.

---

## 25. BOARD #34 — SHORT / REBOOT INCIDENT (SAFETY)
- Action: user installed the correct loopback wire (one end P9_29, other P8_13)
  and the board REBOOTED ON ITS OWN.
- Likely cause: the servo + 5V phone-charger harness was STILL attached to
  P9_29. External 5V supply (not guaranteed common ground with the BBB)
  presents an off-reference voltage on the servo signal line. Bridging P9_29
  to P8_13 (a 3.3V GPIO input) under that condition drove current into the
  BBB 3.3V domain -> PMIC brown-out reset. A bare PRU-output->GPIO-input
  wire alone should NOT reboot; the externally-powered servo harness is the
  hazard.
- Immediate mitigation: removed ALL wiring (servo + charger + jumpers),
  power-cycled, confirmed clean boot (no heat, no PMIC error in dmesg).
- SAFETY RULE (going forward):
  - Loopback tests run with the SERVO 100% DETACHED from P9_29.
  - Never connect an externally-powered servo (separate 5V supply) to a BBB
    pin without common ground + level/power isolation (buffer/driver board).
  - Test in pure isolation: rig self-test (P8_15->P8_13) first, then the
    P9_29->P8_13 loopback.
- If the board reboots even on the P8_15->P8_13 self-test (servo fully off),
  the jumper wire itself is bad (bridging a power pin) -> use a different wire.
- Status: board recovered. Awaiting isolated retest (servo detached).

---

## 26. BOARD #35 — REBOOT RECURS EVEN WITH SERVO POWER OFF
- User: servo 5V supply switched OFF, but servo SIGNAL (yellow) + GROUND
  (black) wires STILL plugged into the board; jumper P9_29<->P8_13 added;
  board made a noise and rebooted AGAIN.
- ROOT CLARIFICATION: "servo power off" != "servo disconnected". With the red
  wire off but yellow+black still on the board, the servo's signal pin remains
  tied (inside the servo, via ESD diode) to its floating 5V rail; the black
  wire provides a ground return. Bridging that to P9_29/P8_13 let current flow
  through the diode + ground -> BBB 3.3V domain inrush -> PMIC brown-out reset.
- SAFETY RULE (hardened): for ANY loopback test the SERVO MUST BE PHYSICALLY
  UNPLUGGED FROM THE HEADER (all 3 wires off). Loopback only checks the PRU
  pin and has nothing to do with the servo.
- ABSOLUTELY-SAFE PROCEDURE (worst case = reboot, never damage):
  0. Power off BBB; remove EVERY wire from P8/P9 (servo Y/R/B all out).
  1. Power on; confirm clean boot (dmesg no PMIC/undervoltage).
  2. Rig self-test (no P9_29): power OFF, jumper P8_15<->P8_13, power ON,
       sudo ./gpio_toggle gpiochip1 15 6   (shell A)
       sudo ./loopback_probe 6             (shell B)
     -> expect ~30 transitions. Reboot here => bad jumper wire.
  3. Real loopback: power OFF, move free end P8_15->P9_29 (P9_29<->P8_13),
     servo STILL off board, power ON,
       sudo ./load_pru.sh pru0 gpo_self_test.pru0.out
       sudo ./loopback_probe 6
     -> expect ~30 transitions.
  Principle: servo 100% disconnected; insert jumper with board POWERED OFF.
- Note: repeated self-reboot = PMIC protection, board very likely alive.
  Stop inducing the fault; do not reconnect externally-powered servo to a pin.
- Status: awaiting safe retest per above (servo fully unplugged).

### dmesg captured AFTER placing jumper P9_29<->P8_13 (REASSURING)
- User ran `sudo dmesg | tail -n 20` with the jumper in place.
- FINDING: **completely clean, normal boot. NO PMIC / undervoltage / brownout /
  regulator fault anywhere.** Board reached full userspace
  (remoteproc1/2 "available" at ~45s). => board is alive and healthy; reboot was
  self-protecting, NOT destructive.
- `remoteproc1`/`remoteproc2` available BUT **no "Booting fw image
  am335x-pru0-fw" line** => PRU firmware NOT loaded in this capture =>
  jumper in place but PRU idle => this is the safe baseline (nothing driving P9_29).
- Benign, unrelated noise (IGNORE):
  - `pinctrl-single ... pin PIN0 already requested by 481d8000.mmc; cannot claim
    for 48038000.mcasp` => onboard audio (mcasp) vs eMMC pin conflict; pre-existing
    on every BBB, NOT our issue.
  - `configfs-gadget.g_multi` MAC lines => USB Ethernet/RNDIS gadget, normal.
  - `at24 ... supply vcc not found, using dummy regulator` => EEPROMs, normal.
- CONCLUSION: board stable because PRU idle + (presumably) servo not driving pin.
  Decisive test = load gpo_self_test + loopback_probe 6 WITH servo 100% unplugged.
  Expect ~30 transitions if P9_29 pad is PRU-driven; reboot => bad jumper wire or
  P9_29 pad fault.
- Status: awaiting isolated retest (servo 100% detached).

### VALID loopback #1 (servo 100% OFF, correct single jumper) -> 0 transitions
- Setup: servo fully disconnected from board. ONE jumper P9_29<->P8_13.
  PRU loaded gpo_self_test.pru0.out (remoteproc1 "now up", size 2900).
  Ran `sudo ./loopback_probe 6`.
- RESULT: t=0.0..5.9s all value=0, **total transitions: 0**
  -> "LOOPBACK DEAD: P9_29 pad never toggled".
- CRITICAL: board did NOT reboot this time (servo absent). This CONFIRMS the
  earlier reboots were caused by the servo wires, NOT the loopback test. Safe.
- This is the first CLEAN/VALID loopback (prior run #154 was invalid: jumper
  was placed on each pin separately, not bridging).
- BUT 0 transitions is NOT yet proof the P9_29 pad is dead. Must rule out:
  (a) jumper wire doesn't conduct / not seated in P9_29 hole,
  (b) P8_13 input side not reading,
  (c) PRU output genuinely not reaching pad.
- NEXT (control / rig self-test, NO reboot needed):
  move the SAME wire's P9_29 end to P8_15 => P8_15<->P8_13.
    shell A: sudo ./gpio_toggle gpiochip1 15 6
    shell B: sudo ./loopback_probe 6
  Expect ~30 transitions. That proves wire + P8_13 input path are good,
  isolating the fault to the P9_29/PRU side. If 0 -> wire bad or P8_13 input
  broken (swap wire / inspect).
- Status: CONTROL TEST PENDING (rig self-test P8_15<->P8_13).

### CONTROL TEST RUN (rig self-test P8_15<->P8_13) -> INVALID (user error)
- User repeated: `sudo ./gpio_toggle gpiochip1 15 6` then `sudo ./loopback_probe 6`.
- RESULT: total transitions: 0. User asked "why is it saying p9_29 again".
- TWO TOOL BUGS FOUND (both MY fault, not the board):
  1. loopback_probe printed hardcoded "P9_29 pad never toggled" regardless of
     which pin it actually read. FIXED: message is now pin-generic + takes
     chip/line args.
  2. MORE IMPORTANTLY: the control was run WRONG. gpio_toggle is a userspace
     program that runs 6s then EXITS; user ran it (twice) and then started
     loopback_probe AFTER it finished -> read pin was static -> 0 transitions
     is EXPECTED, NOT a rig failure.
- Also: original loopback_probe sampled at 100 ms while the transmitter toggled
  at 5 Hz (100 ms period) -> sampler/transmitter ALIASING could miss edges and
  produce a false 0 even for a real signal. This taints the original "VALID
  loopback #1" 0-transition reading too.
- FIXES (commit 4e81ac6, pushed):
  - loopback_probe: pin-generic message; now samples at 500 Hz (2 ms) so a 5 Hz
    signal cannot be aliased away; usage: sudo ./loopback_probe [secs] [chip] [line]
  - NEW loopback_self: ONE program drives a pin AND reads another in the same
    tight loop (sample read side 5x per 100 ms drive step). No two-shell race,
    no aliasing. Usage:
      sudo ./loopback_self gpiochip1 15 gpiochip1 28 6   # drive P8_15, read P8_13
    ~30 transitions => rig good. 0 => wire/read-pin fault.
- CONCLUSION: the control test was INCONCLUSIVE (invalid run), and the original
  "VALID loopback #1" 0-transition result is NOW SUSPECT due to aliasing.
  MUST re-run with loopback_self before concluding P9_29 pad is dead.
- Status: RIG SELF-TEST PENDING with corrected loopback_self tool.

### ACTION for user (safe, decisive)
1. `git pull && make clean && make` on the board (gets loopback_self).
2. Jumper P8_15<->P8_13 (small P8 header; leave P9_29 alone).
3. `sudo ./loopback_self gpiochip1 15 gpiochip1 28 6`
   - ~30 -> rig proven; THEN move jumper to P9_29<->P8_13, load gpo_self_test,
     and run `sudo ./loopback_probe 6` (now 500 Hz, no aliasing) to finally
     settle the P9_29 question.
   - 0   -> swap the wire; if still 0, read-pin/wire fault, not P9_29.

### RIG SELF-TEST RUN (loopback_self, single process) -> 0 transitions
- After the 4e81ac6 build, user compiled and ran:
  `sudo ./loopback_self gpiochip1 15 gpiochip1 28 6`
  (drive P8_15, read P8_13, same process, correct timing).
- RESULT: total transitions on read pin: 0 -> "RIG DEAD".
- THIS IS DIFFERENT FROM THE EARLIER INVALID CONTROL: here drive+read are in
  ONE process at the correct cadence, so timing/aliasing are NOT the cause.
  => a real failure of one of: (a) jumper open / not seated, (b) P8_15 not in
  GPIO output mode, (c) P8_13 not in GPIO input mode, (d) a silent libgpiod
  read error. The old binary could not tell which.
- IMMEDIATELY AFTER, user ACCIDENTALLY PULLED THE JUMPER OUT OF THE HEADER
  WHILE THE BOARD WAS STILL POWERED -> the board shut down and rebooted again.
- HARD SAFETY RULE (added, absolute): **NEVER insert or remove any header wire
  while the board is powered.** Plugging/unplugging a jumper on a live pin can
  momentarily bridge it to an adjacent pin or ground, dumping current into the
  PMIC and causing the brown-out reboot (this matches every reboot we've seen).
  Before touching ANY wire on the P8/P9 header: power the board fully OFF
  (remove barrel jack), or at minimum stop all firmware (`sudo ./load_pru.sh
  stop` / unload), then make your wiring change with the board OFF, then
  power back on.
- FIX (commit below): rewrote loopback_self + loopback_probe to report the
  FAIL reason explicitly -- "FAIL request OUTPUT/INPUT <pin> (muxed away?)"
  if a GPIO request fails, plus a `reads/errors/range` line so an open wire
  (range 0..0, 0 errors) is distinguishable from a mux/permission fault.
- Status: RIG SELF-TEST still 0 transitions; cause not yet distinguished.
  Re-run with the new binary to see WHY (FAIL line vs range 0..0).

### ACTION for user (SAFE, and now diagnostic)
1. Board OFF (remove barrel jack) OR ensure no firmware is driving pins.
2. Re-make on board: `cd ~/eyespies/pru && make loopback_self loopback_probe`
3. Power board ON. Jumper P8_15<->P8_13 (board can be on for this READ-ONLY
   test because the tool sets the pins itself; but if you must change wiring,
   do it with board OFF).
4. `sudo ./loopback_self gpiochip1 15 gpiochip1 28 6`
   - "FAIL request OUTPUT gpiochip1:15" => P8_15 not in GPIO mode (mux) -> fix mux.
   - "FAIL request INPUT gpiochip1:28"  => P8_13 not in GPIO mode -> fix mux.
   - reads=N errors=0 range=[0..0]      => open wire / bad seat -> swap wire.
   - transitions>3 / RIG OK            => rig proven, proceed to P9_29 test.
   NOTE: do NOT pull the wire while powered. Stop the test first, then change.

### RIG SELF-TEST (hardened binary, board #37) -> 0 transitions, but PROVES WIRING FAULT
- User did a CLEAN power cycle (unplug -> swap jumper -> plug): boot log shows
  `Reset Source: Power-on reset has occurred` (good — no brown-out this time).
- Board pulled 15d6cf5 and built the new loopback_self. Ran:
  `sudo ./loopback_self gpiochip1 15 gpiochip1 28 6`
- FULL OUTPUT:
    sample out=0 in=0
    sample out=0 in=0
    sample out=0 in=0
    sample out=0 in=0
    sample out=0 in=0
    sample out=1 in=0     <- drive P8_15 HIGH, read P8_13 STILL 0
    sample out=1 in=0
    sample out=1 in=0
    sample out=1 in=0
    sample out=1 in=0
    reads=150 errors=0  raw-range=[0..0]  transitions=0
    RIG DEAD: ...
- DECODE (decisive):
  - NO "FAIL request OUTPUT/INPUT" lines, errors=0 => both gpiochip1:15 (P8_15)
    and gpiochip1:28 (P8_13) were successfully claimed as OUT and IN, and the
    libgpiod writes to P8_15 SUCCEEDED (out=0 -> 1 actually happened).
  - Drive pin toggled 0->1 but the read pin stayed 0 across ALL 150 samples.
  - => the only thing between them (the jumper) is NOT conducting. The board,
    P8_15 and P8_13 are ALL fine. This is a jumper/wiring fault (bad seat or
    wrong pins on the dense P8 header), NOT a board fault.
- HUGE IMPLICATION for the whole P9_29 question:
  The original "VALID loopback #1" (board #35) used the EXACT same P8_13 read
  pin and the EXACT same jumper method. If the jumper isn't conducting here, it
  very likely wasn't conducting there either. => **P9_29 is NOT proven dead.**
  Every 0-transition loopback so far is explained by a non-conducting jumper.
- FIX: improved the loopback_self "RIG DEAD" message to explicitly say the
  jumper is not conducting / mis-seated, and to warn NOT to conclude P9_29 is
  dead until the control rig passes. (commit: in this push)
- NEXT (the REAL decisive step): get a conducting jumper on P8_15<->P8_13.
  * Re-seat the existing jumper firmly on the correct P8 pins, OR try a 3rd
    jumper. Re-run `sudo ./loopback_self gpiochip1 15 gpiochip1 28 6`.
  * Expected when jumper conducts: out=1 -> in=1, range=[0..1], ~30 transitions,
    "RIG OK". THEN and ONLY THEN move to P9_29 (load gpo_self_test, jumper
    P9_29<->P8_13, run loopback_probe 6).
  * If still range=[0..0] with 0 errors after re-seating a known-good wire ->
    P8_13 input path itself is broken (swap to a different read pin, e.g.
    P8_14 = gpiochip1:14, to cross-check).
- Status: WIRING FAULT CONFIRMED for the control rig. P9_29 verdict RE-OPENED
  (not proven dead). No board damage. See board #37.

### BOARD STORAGE FAILURE (board #38) — filesystem went read-only / eMMC I/O error
- User reports: mid-session, with NO command and NO reboot from them, the board
  suddenly started printing and then EVERY command fails:
    /usr/bin/clear: Input/output error
    /usr/bin/sudo:  Input/output error
    /usr/bin/ls:    Input/output error
- Kernel warning seen just before:
    [ 1586.722646] WARNING: CPU:0 PID:658 at drivers/net/phy/phy.c:1313
                  _phy_state_machine+0xf3/0x1d4
    [ 1586.731537] phy_check_link_status+0x1/0xb0: returned: -5
  (this is the Ethernet PHY driver warning — cosmetic, NOT the root cause)
- DECODE: `EIO` (Input/output error) on reading /usr/bin/* means the OS cannot
  read those binaries off the eMMC => root filesystem is either forced
  READ-ONLY (ext4 corruption) or the eMMC is returning I/O errors. This is a
  STORAGE failure, NOT caused by the loopback GPIO test (that never wrote to
  disk).
- ROOT CAUSE (almost certainly): accumulated SUDDEN POWER LOSS. This session has
  many abrupt cycles — PMIC brown-outs from live jumper changes, unplug/plug
  power cycles, hot-plug reboots. Sudden power loss is the classic way to
  corrupt ext4. The damage surfaced now as the kernel refusing reads.
- CRITICAL: WORK NOT LOST.
  * All firmware/Makefile/loopback tools/log are committed and PUSHED to
    origin/main (HEAD bdf33ae). Reimage + git pull restores everything.
  * Only board-specific state = /boot/firmware/uEnv.txt (one line:
    uenvcmd=mw.l 0x44E109BC 0x24) — trivially recreated.
  * .out/.bin binaries rebuild from repo.
- SAFE RECOVERY (given to user):
  1. Leave the dead terminal. Power board OFF (pull barrel jack). Unplug any
     USB/serial that could back-feed 5V. Wait 15-20s.
  2. Power ON. Watch serial from first line. Three outcomes:
     (A) Normal login -> `mount | grep ' / '` and
         `dmesg | grep -i -E 'mmc|ext4|error|readonly'` to check for ro/errors.
     (B) Boot runs fsck / "recovering journal" -> let it finish.
     (C) Hang at MMC errors / panic / no login -> eMMC likely failing -> reimage
         from GitHub repo.
  * Do NOT try to fsck from the dead shell (sudo/ls fail). Power cycle only.
- Status: board filesystem failure, recovery pending power cycle + boot check.
  User advised to NOT type commands into the dead shell.
