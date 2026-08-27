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
