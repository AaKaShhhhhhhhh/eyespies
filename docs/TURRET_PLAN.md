# Camera Turret — Full Project Plan (plain language, file-by-file)

> Read this top to bottom once. It tells you EXACTLY which file to open, what to
> change, what to create, and what to delete — for every step. No guessing.

---

## 0. The 30-second truth (read this first)

**What already works (proven on the board):**
- The PRU firmware `pru/pru_servo.c` moves an MG90S servo on **P9_31** by writing
  directly to the PRU's `__R30` register. We confirmed: *"THE SERVO IS MOVINGGGGGG"*.
- `pru/load_pru.sh` loads that firmware safely.

**What the turret does TODAY (the `turret` binary):**
- Camera (`capture/v4l2.c`) grabs frames.
- `detection/motion_detect.c` finds "where something moved" → a pixel position.
- `control/control_loop.c` turns that position into pan/tilt angles.
- **`pmw/pmw_servo.c`** drives the servo — using **libgpiod** (software bit-banged
  PWM on P9_14 / P9_16). **NOT the PRU.**

**The goal of this plan:** move the servo control from `pmw` (libgpiod) to the PRU
(like the proven demo), so the servo runs rock-steady on the PRU, and later swap
the "motion detect" brain for an ML brain (FOMO).

---

## 1. Your two questions, answered

### Q: "What is `color_threshold.c` for? We use FOMO, right?"
- `color_threshold.c` and `color_threshold.h` are **100% commented out** (every line
  starts with `//`). They are dead code from an early "detect by color" idea.
- The top `Makefile` does **not** compile them. Only `detection/Makefile` builds
  them into a `.a` library that **nobody links**. So they do nothing.
- FOMO is the future ML brain — it will replace `detection/motion_detect.c`, not
  `color_threshold.c`. `color_threshold.c` is just clutter.
- **Action: DELETE `detection/color_threshold.c` and `detection/color_threshold.h`.**

### Q: "What is the `pmw` folder for? We use PRU directly, right?"
- **Correction:** the turret does **NOT** use the PRU yet. `main.c` and
  `capture/v4l2.c` call `servo_set_angle(...)` which lives in `pmw/pmw_servo.c`.
  That file uses **libgpiod** to toggle GPIO pins in software (bit-banged PWM).
- The PRU firmware `pru/pru_servo.c` is a *separate* standalone demo, not wired into
  `main.c`.
- So `pmw` **is** in use today. The plan below REPLACES `pmw` with a new
  `pru/pru_comms.c` that talks to the PRU. After the swap, `pmw` can be deleted.

---

## 2. Data flow — today vs target

```
TODAY (libgpiod):
  camera ─▶ motion_detect ─▶ control_loop (angles) ─▶ pmw/pmw_servo.c
                                                              │
                                                        libgpiod GPIO
                                                              │
                                                         servo pins (P9_14/P9_16)

TARGET (PRU):
  camera ─▶ motion_detect/FOMO ─▶ control_loop (angles) ─▶ pru_comms.c
                                                                  │
                                              writes angles into PRU shared RAM
                                                                  │
                                                            PRU firmware
                                                                  │
                                                         servo pins (P9_31/P9_30)
```

The only thing that changes in the *logic* is the last hop: instead of
`servo_set_angle(pwm_path, angle)` (libgpiod), we call
`pru_set_angle(axis, angle)` (writes to PRU RAM). The camera, detection, and angle
math stay the same.

---

## 3. File-by-file map (what each file is, and what we do with it)

| File | What it does today | Plan action |
|------|--------------------|-------------|
| `main.c` | Entry point. Sets PWM paths, starts capture. | **EDIT** — use PRU paths, not `/sys/class/pwm`. |
| `capture/v4l2.c` | Grabs frames, calls motion detect, calls `servo_set_angle`. | **EDIT** — replace `servo_set_angle` with `pru_set_angle`. |
| `capture/capture.h` | Defines `Position` + `AxisState`. | **KEEP** (small add maybe). |
| `detection/motion_detect.c/.h` | Centroid of motion → position. | **KEEP now**; later REPLACE with FOMO. |
| `detection/color_threshold.c/.h` | Dead, commented out. | **DELETE**. |
| `control/control_loop.c/.h` | Angle math (gain/smooth/clamp). | **KEEP** (no change). |
| `pmw/pmw_servo.c/.h` | libgpiod servo driver. | **KEEP now**; DELETE after PRU swap. |
| `pru/pru_servo.c` | PRU firmware (1-axis sweep on P9_31). | **EDIT** — 2 axes, read setpoints from RAM. |
| `pru/load_pru.sh` | Loads firmware safely. | **KEEP** (add P9_30 mux). |
| `pru/Makefile` | Builds `pru_servo.out`. | **KEEP**. |
| `pru/pru_comms.c` + `pru/pru_comms.h` | **NEW** — ARM side writes angles to PRU RAM. | **CREATE**. |
| `Makefile` (top) | Builds `turret` binary. | **EDIT** — drop `pmw`, add `pru_comms`. |

---

## 4. The PRU <-> ARM contract (the shared memory layout)

The ARM (the `turret` program) and the PRU **cannot call each other's functions**.
They talk through a shared block of RAM. On the AM335x:

- PRU sees it at local address `0x00010000`.
- ARM sees the SAME physical RAM at `0x4A310000`.
- ARM opens `/dev/mem` and writes there (this works on kernel 6.x — proven).

We define ONE struct that BOTH sides use, byte-for-byte:

```c
/* Put this identical struct in: pru/pru_servo.c  AND  pru/pru_comms.h */
typedef struct {
    volatile uint32_t magic;   /* must equal 0x50524F55 ("PROU") so PRU knows ARM wrote */
    volatile uint32_t pan_us;  /* pulse width in microseconds for P9_31 (pan)  */
    volatile uint32_t tilt_us; /* pulse width in microseconds for P9_30 (tilt) */
    volatile uint32_t seq;     /* increments each time ARM updates -> PRU detects new data */
    volatile uint32_t flags;   /* bit0 = stop/halt request */
} pru_servo_cmd_t;
```

Angle → microseconds helper (same formula on both sides):
- 0°   → 1000 µs
- 90°  → 1500 µs (center)
- 180° → 2000 µs

```c
static inline uint32_t angle_to_us(float deg) {
    if (deg < 0)   deg = 0;
    if (deg > 180) deg = 180;
    return (uint32_t)(1000.0f + (deg / 180.0f) * 1000.0f);
}
```

---

## 5. Milestone 2 — Edit the PRU firmware (`pru/pru_servo.c`)

**Goal:** make it 2-axis (P9_31 = pan/R30_0, P9_30 = tilt/R30_1) and read the
setpoints from shared RAM instead of sweeping.

Steps:
1. Add the shared struct (copy from section 4) at the top.
2. Point a pointer at the shared RAM: `pru_servo_cmd_t *cmd = (pru_servo_cmd_t *)0x00010000;`
3. Add the second pin: `#define P9_30_R30_BIT (1u << 1)`.
4. In `main()`, loop forever: read `cmd->pan_us` / `cmd->tilt_us`, convert µs →
   PRU cycles (1 cycle = 5 ns at 200 MHz → cycles = us * 200), and emit the pulse
   on the right bit.

Template (fill the blanks):

```c
#include <stdint.h>
#include "resource_table_empty.h"

volatile register uint32_t __R30 asm("r30");

#define P9_31_R30_BIT  (1u << 0)   /* pan  */
#define P9_30_R30_BIT  (1u << 1)   /* tilt */

#define PERIOD_US 20000u           /* 20 ms = 50 Hz */
#define DELAY_UNIT 1000u

typedef struct {
    volatile uint32_t magic, pan_us, tilt_us, seq, flags;
} pru_servo_cmd_t;

static inline void delay_cycles(uint32_t cycles) {
    uint32_t units = cycles / DELAY_UNIT;
    while (units--) __delay_cycles(DELAY_UNIT);
}
/* us -> cycles at 200 MHz: 1 us = 200 cycles */
static inline void pulse(uint32_t bit, uint32_t us) {
    uint32_t on  = us * 200u;
    uint32_t off = (PERIOD_US - us) * 200u;
    __R30 |=  bit;  delay_cycles(on);
    __R30 &= ~bit;  delay_cycles(off);
}

int main(void) {
    pru_servo_cmd_t *cmd = (pru_servo_cmd_t *)0x00010000;
    __R30 &= ~(P9_31_R30_BIT | P9_30_R30_BIT);
    uint32_t last_seq = 0;
    while (1) {
        if (cmd->magic == 0x50524F55u) {          /* "PROU" -> ARM has written */
            if (cmd->seq != last_seq) {            /* new command since last loop */
                last_seq = cmd->seq;
                pulse(P9_31_R30_BIT, cmd->pan_us); /* pan  */
                pulse(P9_30_R30_BIT, cmd->tilt_us);/* tilt */
            } else {
                /* no new command: re-emit last pulse so servo holds position */
                pulse(P9_31_R30_BIT, cmd->pan_us);
                pulse(P9_30_R30_BIT, cmd->tilt_us);
            }
        }
    }
}
```
> Build on the board: `cd ~/eyespies/pru && make`. That produces `pru_servo.out`.

💡 **C CARD — `volatile`:** we mark the struct `volatile` because the PRU reads it
while ARM writes it *outside any function order the compiler can see*. Without
`volatile` the compiler may cache the value in a register and never re-read it.
`volatile` = "this can change behind my back; always read from memory."

🏢 **INTERVIEW:** "Why `volatile` for hardware / shared memory?" → tells the
compiler the value may change asynchronously (by HW or another core), so it must
not optimize away reads/writes.

---

## 6. Milestone 3 — Create `pru/pru_comms.c` + `pru/pru_comms.h` (ARM side)

This is the ONLY new C file. It runs on the ARM (inside the `turret` program) and
writes angles into the PRU's shared RAM.

**`pru/pru_comms.h`:**
```c
#ifndef PRU_COMMS_H
#define PRU_COMMS_H
void pru_comms_init(void);                 /* call once at startup */
void pru_set_angle(int axis, float deg);   /* axis 0=pan,1=tilt; deg 0..180 */
void pru_comms_stop(void);                 /* optional: ask PRU to halt */
#endif
```

**`pru/pru_comms.c`:**
```c
#include "pru_comms.h"
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/* SAME struct as the firmware (byte-for-byte) */
typedef struct {
    volatile uint32_t magic, pan_us, tilt_us, seq, flags;
} pru_servo_cmd_t;

#define PRU_RAM_PHYS 0x4A310000u   /* ARM view of PRU shared RAM */
static pru_servo_cmd_t *cmd = NULL;
static uint32_t seq_counter = 0;

static uint32_t angle_to_us(float deg) {
    if (deg < 0)   deg = 0;
    if (deg > 180) deg = 180;
    return (uint32_t)(1000.0f + (deg / 180.0f) * 1000.0f);
}

void pru_comms_init(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("pru_comms: open /dev/mem"); return; }
    /* map one page of the PRU shared RAM into our address space */
    void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, (off_t)PRU_RAM_PHYS);
    if (map == MAP_FAILED) { perror("pru_comms: mmap"); close(fd); return; }
    cmd = (pru_servo_cmd_t *)map;
    cmd->magic  = 0x50524F55u;   /* tell PRU ARM is alive */
    cmd->pan_us = 1500;           /* center */
    cmd->tilt_us= 1500;
    cmd->seq    = 0;
    cmd->flags  = 0;
    close(fd);  /* fd can close; the mmap stays valid */
}

void pru_set_angle(int axis, float deg) {
    if (!cmd) return;
    uint32_t us = angle_to_us(deg);
    if (axis == 0) cmd->pan_us  = us;
    else            cmd->tilt_us = us;
    cmd->seq = ++seq_counter;    /* bump seq so PRU picks up the new value */
}

void pru_comms_stop(void) {
    if (cmd) cmd->flags |= 1u;   /* PRU firmware can check this to halt */
}
```

💡 **C CARD — `mmap` / `/dev/mem`:** `mmap` asks the OS "map this physical memory
into my program's address space so I can read/write it like a normal variable."
`/dev/mem` is a special Linux file that lets a root program peek at *physical*
RAM/registers. We map `0x4A310000` (the PRU's RAM) and then just write the struct.

🏢 **INTERVIEW:** "How does userspace talk to hardware on Linux?" → via
`/dev/mem` + `mmap` (simple, needs root) or a kernel driver + `ioctl`/`sysfs`
(proper, no root). For a PRU shared-RAM mailbox, `mmap` of `/dev/mem` is the
standard lightweight approach.

---

## 7. Milestone 4 — Rewire `main.c` and `capture/v4l2.c`

We replace the `pmw` calls with `pru_comms` calls. Tiny, surgical edits.

**`main.c` changes:**
- Add `#include "pru/pru_comms.h"` near the top.
- Remove the two `#define ..._PWM_PATH` lines and the `pwm_set_period_ns` /
  `pwm_enable` calls (those belong to `pmw`).
- After `motion_reset();`, call `pru_comms_init();`
- Pass nothing PWM-related into `capture_loop` anymore — change its signature.

**`capture/v4l2.c` changes:**
- Replace `#include "pmw/pmw_servo.h"` with `#include "pru/pru_comms.h"`.
- In `capture_loop`, where it now calls
  `servo_set_angle(pan_pwm_path, pan_new);` → call `pru_set_angle(0, pan_new);`
  and `servo_set_angle(tilt_pwm_path, tilt_new);` → `pru_set_angle(1, tilt_new);`
- Remove the `pan_pwm_path` / `tilt_pwm_path` parameters from `capture_loop` and
  its call in `main.c`.

That's the whole swap. The motion detection and angle math are untouched.

---

## 8. Milestone 5 — Update the top `Makefile`, build, test

**Top `Makefile` edits:**
- Change `PWM_OBJS = pmw/pmw_servo.o` → `PRU_COMMS_OBJS = pru/pru_comms.o`
- Add `pru/pru_comms.o` into `OBJS`.
- Remove `-lgpiod` from `LIBS` (no longer needed after dropping `pmw`).
- Add a rule:
  ```
  pru/pru_comms.o: pru/pru_comms.c pru/pru_comms.h
  	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ pru/pru_comms.c
  ```

**On the board, build + run:**
```bash
# 1) build the turret binary
cd ~/eyespies
make clean
make turret

# 2) load the 2-axis PRU firmware FIRST (before starting turret)
cd ~/eyespies/pru
make                       # builds pru_servo.out
sudo bash load_pru.sh pru0 pru_servo.out

# 3) in another shell, run the turret
cd ~/eyespies
sudo ./turret            # (pass /dev/videoX if auto-detect picks wrong node)
```
Servo should now track motion using the PRU on P9_31 (pan) + P9_30 (tilt).

**Add P9_30 mux** (board-only, one time): in `/boot/firmware/uEnv.txt` add a line
so P9_30 is also PRU mode 5 at boot:
```
uenvcmd=mw.l 0x44E10990 0x05; mw.l 0x44E10998 0x05
```
(P9_31 pad `0x44E10990`, P9_30 pad `0x44E10998`; both mode 5 = PRU0 R30.)

---

## 9. Milestone 6 (later) — FOMO brain replaces motion detect

When you add the ML model:
1. Keep `detection/motion_detect.c` as a *fallback* or delete it.
2. Add `detection/fomo.c` that runs the TFLite model and returns a `Position`
   (same struct `capture.h` already defines).
3. In `capture/v4l2.c`, switch `find_motion_position(...)` → `find_fomo_position(...)`.
   Nothing else changes — the angle math and PRU comms are model-agnostic.
4. `color_threshold.c` is already gone (deleted in step 1), so no conflict.

Recommended model size for the BBB (Cortex-A8, no NPU): **FOMO, 96×96,
monochrome, quantized TFLite with NEON** → ~15–30 FPS. The PRU keeps the servo at
a steady 50 Hz regardless of detection FPS.

---

## 10. Exact delete list (do this now, safe)

```
detection/color_threshold.c
detection/color_threshold.h
```
Optional later (after Milestone 5 works):
```
pmw/pmw_servo.c
pmw/pmw_servo.h
pmw/Makefile
```
(Keep `pmw` until the PRU swap is confirmed working, so you have a fallback.)

---

## 11. Quick concept checklist for interviews

- **`__R30`** — PRU's direct output register; bit N drives PRU pin R30_N. No GPIO
  driver, no device-tree needed. Fast, deterministic (200 MHz).
- **Shared RAM mailbox** — ARM writes a struct at `0x4A310000`; PRU reads the same
  bytes at `0x00010000`. `volatile` + a `seq` counter = lock-free handshake.
- **`mmap("/dev/mem")`** — userspace window into physical memory; needs root.
- **50 Hz servo** — period 20 ms; pulse 1.0–2.0 ms = 0–180°. PRU counts cycles;
  ARM only sends the target angle.
- **Why PRU over libgpiod** — libgpiod is a Linux userspace thread; the scheduler
  can preempt it mid-pulse → servo buzz/jitter. The PRU is a separate real-time
  core that never gets preempted. That's the whole reason we moved to it.

---

## 12. TL;DR for coding

1. **Delete** `color_threshold.c/.h` (dead code).
2. **Edit** `pru/pru_servo.c` → 2-axis, read setpoints from RAM (Milestone 2).
3. **Create** `pru/pru_comms.c/.h` → ARM writes angles to RAM (Milestone 3).
4. **Edit** `main.c` + `capture/v4l2.c` → use `pru_set_angle` not `servo_set_angle`
   (Milestone 4).
5. **Edit** top `Makefile` → drop `pmw`, add `pru_comms`, drop `-lgpiod` (Milestone 5).
6. **Add** P9_30 mux line to `/boot/firmware/uEnv.txt` on the board.
7. Build, load firmware, run `./turret`. Servo tracks via PRU. 🎯
