# Camera Turret Project — Plan & Coding Guide

> **Purpose:** a beginner-friendly roadmap for turning the PRU servo win into a
> working camera turret: USB camera → detection → tracking → PRU-driven pan/tilt
> servos. No ML code yet — just the architecture, the changes, and the **templates**
> you fill in. Every C construct that trips people up is flagged with a 💡 CARD and,
> where it matters for **semiconductor/firmware coding interviews**, a 🏢 INTERVIEW tag.
>
> **Companion docs:** `PRU_SERVO_DEBUG_LOG.md` (concepts), `PRU_SERVO_RAW_JOURNAL.md` (raw log).

---

## 0. The one mental model to keep

```
   USB camera ──V4L2 capture──▶ ARM userspace (the "brains")
                                      │
                          motion detect / FOMO  ──▶  target (x,y)
                                      │
                                  PID controller   ──▶  setpoint (pan_us, tilt_us)
                                      │
                       pru_comms writes to PRU shared RAM (0x4A310000)
                                      │
                                      ▼
                        PRU0 firmware  ◀── already muxed on P9_31 + P9_30
                        50 Hz PWM out  ──▶ PAN  servo (P9_31, R30 bit 0)
                                       ──▶ TILT servo (P9_30, R30 bit 1)
```

**Key insight:** the PRU already produces *perfectly stable* 50 Hz PWM. The ARM only
has to tell it *where to point*. Detection can run at 2 FPS or 30 FPS — the servos
stay smooth because the PRU holds the last pulse until the next setpoint.

> 🏢 **INTERVIEW:** this "hard-realtime I/O on a co-processor, logic on the app
> processor" split is exactly how real camera/gimbal/robotics firmware is architected
> (NVIDIA Jetson, Qualcomm, DJI gimbals). Be able to draw this diagram and justify
> *why* the PWM is on the PRU.

---

## 1. The ARM ↔ PRU contract (design this FIRST)

We proved earlier that **ARM `/dev/mem` writes to `0x4A310000` (PRU data RAM, global
view) WORK** on 6.x — only the Control Module is blocked. So the command channel is
plain shared memory. No drivers, no syscalls per frame.

| View | Address | Used by |
|------|---------|---------|
| PRU-local | `0x00010000` | firmware reads here |
| ARM-global | `0x4A310000` | `pru_comms.c` writes here |

### The shared struct (define it identically in BOTH `pru_servo.c` and `pru_comms.c`)

```c
/* shared.h  — include in BOTH the PRU firmware and the ARM comms code */
#define SHM_MAGIC 0xBAD0BAD0u

struct servo_cmd {
    uint32_t magic;     /* must equal SHM_MAGIC or PRU ignores the block   */
    uint32_t seq;       /* increments each time ARM updates setpoint       */
    uint32_t pan_us;    /* 1000..2000  (microseconds of pulse width)       */
    uint32_t tilt_us;   /* 1000..2000                                       */
    uint32_t flags;     /* bit0 = "park at center", bit1 = "halt"           */
};
```

> 💡 **CARD — `struct` is just a labeled block of memory.** The PRU and ARM agree on
> the byte layout; whoever writes `pan_us` at offset 4 must read it at offset 4. Same
> file, same `#pragma`/padding → identical layout. Keep all fields `uint32_t` so there
> is **no alignment/padding surprise** between the two toolchains.
>
> 🏢 **INTERVIEW:** "How do two cores share data without a lock?" → single-writer /
> single-reader (SPSC) shared memory + a sequence number. A classic
> producer–consumer question at NXP/TI/Qualcomm.

**Who writes what:** ARM writes `magic`, `seq`, `pan_us`, `tilt_us`. PRU only *reads*
them (and could echo back `seq` it last applied into `flags` if you want a handshake).

---

## 2. Milestones (your coding checklist)

Work top-to-bottom. Each milestone has: **Goal / Template / Changes / Test**.

### Milestone 1 — Two-axis PRU firmware (no camera yet)

**Goal:** PRU reads `servo_cmd` from `0x00010000` and bit-bangs 50 Hz PWM on R30 bit 0
(P9_31, PAN) and bit 1 (P9_30, TILT). If no valid `magic`, hold center (1500 µs).

**Changes to `pru/pru_servo.c`:**
- Add `#include "shared.h"`.
- Map the command struct: `volatile struct servo_cmd *cmd = (volatile struct servo_cmd *)0x00010000;`
- Loop: for each 20 ms frame, split into PAN pulse then TILT pulse (or interleave),
  using the constant-`__delay_cycles` helper we already use for one channel.
- Read `cmd->pan_us`/`cmd->tilt_us`; clamp to 1000..2000; if `cmd->magic != SHM_MAGIC`
  use 1500.

**Template (skeleton — fill the timing):**

```c
#include "shared.h"
volatile register uint32_t __R30 asm("r30");

/* constant delay so the PRU compiler is happy (no variable __delay_cycles) */
static inline void us_delay(uint32_t us) {
    /* ~5 cycles per loop at 200 MHz -> tune this constant */
    for (uint32_t i = 0; i < us * 40; i++) __delay_cycles(5);
}

int main(void) {
    volatile struct servo_cmd *cmd = (volatile struct servo_cmd *)0x00010000;
    uint32_t pan = 1500, tilt = 1500;

    while (1) {
        if (cmd->magic == SHM_MAGIC) {
            pan  = cmd->pan_us  < 1000 ? 1000 : (cmd->pan_us  > 2000 ? 2000 : cmd->pan_us);
            tilt = cmd->tilt_us < 1000 ? 1000 : (cmd->tilt_us > 2000 ? 2000 : cmd->tilt_us);
        }
        /* PAN pulse */
        __R30 |=  (1u << 0);  us_delay(pan);  __R30 &= ~(1u << 0);  us_delay(20000 - pan);
        /* TILT pulse */
        __R30 |=  (1u << 1);  us_delay(tilt); __R30 &= ~(1u << 1);  us_delay(20000 - tilt);
    }
    return 0;
}
```

> 💡 **CARD — `volatile`** here means "this memory can change without the C code
> knowing" (the ARM writes it). Without `volatile` the compiler may cache the value in
> a register and never re-read it. **This is the single most-asked embedded C question.**
>
> 🏢 **INTERVIEW:** "Why `volatile` on memory-mapped / shared registers?" → prevents
> optimizer from assuming the value is unchanged. Always pair with `volatile` for HW
> regs and SPSC shared memory.
>
> 💡 **CARD — `1u << 0`** is a **bit set**: `1u` shifted left 0 = `0b0001`. `1u << 1`
> = `0b0010`. `|=` sets that bit; `&= ~(1u<<n)` clears it. This is the entire R30
> bit-mapping we fought for — bit N of R30 appears on the ball muxed to PRU R30_N.

**Also:** add the TILT mux to `uEnv.txt`:
```bash
sudo sed -i '/^uenvcmd=/d' /boot/firmware/uEnv.txt
printf 'uenvcmd=mw.l 0x44E10990 0x05; mw.l 0x44E10998 0x05\n' | sudo tee -a /boot/firmware/uEnv.txt
sudo reboot
```
(`0x44E10998` = P9_30 pad, mode 5 = PRU0 R30_1 — confirmed in the cape DTS.)

**Test:** a small ARM CLI (`pru_comms` in M2) writes fixed `pan_us`/`tilt_us`; both
servos move to the angle. No camera involved.

---

### Milestone 2 — `pru_comms.c` (the ARM side of the channel)

**Goal:** a tiny module that `mmap`s `/dev/mem` at `0x4A310000` and writes setpoints.
Everything else in the turret calls this instead of touching PWM sysfs.

**New file `pru/pru_comms.c` + `pru/pru_comms.h`:**

```c
/* pru_comms.h */
#pragma once
#include <stdint.h>
int  pru_comms_init(void);                 /* mmap the shared RAM */
void pru_comms_set(uint32_t pan_us, uint32_t tilt_us);  /* write a setpoint */
void pru_comms_close(void);
```

```c
/* pru_comms.c  — skeleton; fill error handling */
#include "pru_comms.h"
#include "shared.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile struct servo_cmd *g_cmd = NULL;
static int g_memfd = -1;

int pru_comms_init(void) {
    g_memfd = open("/dev/mem", O_RDWR | O_SYNC);   /* O_SYNC = uncached/strongly-ordered */
    if (g_memfd < 0) return -1;
    void *map = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_memfd, 0x4A310000);
    if (map == MAP_FAILED) return -1;
    g_cmd = (volatile struct servo_cmd *)map;
    g_cmd->magic = SHM_MAGIC;
    g_cmd->seq   = 0;
    g_cmd->pan_us = 1500; g_cmd->tilt_us = 1500;
    return 0;
}

void pru_comms_set(uint32_t pan_us, uint32_t tilt_us) {
    if (!g_cmd) return;
    g_cmd->pan_us  = pan_us;
    g_cmd->tilt_us = tilt_us;
    g_cmd->seq++;          /* tell PRU "fresh data is here" */
}
```

> 💡 **CARD — `mmap`** maps a file (here `/dev/mem`, the physical RAM) into your
> process's address space, so you can poke `0x4A310000` with a normal pointer.
> `MAP_SHARED` = writes go straight to the device. `O_SYNC` = don't let the CPU cache
> the writes (critical for HW/shared memory).
>
> 🏢 **INTERVIEW:** "How does userspace talk to hardware with no driver?" →
> `open("/dev/mem") + mmap` (and why that needs `CAP_SYS_RAWIO` / root). Follow-up:
> cache coherency, `O_SYNC`, memory barriers.
>
> 💡 **CARD — pointer cast** `(volatile struct servo_cmd *)map` just says "treat this
> raw byte pointer as a `servo_cmd` struct." The bytes at `0x4A310000` now read as
> `magic`, `seq`, etc.

**Test:** `pru_comms_init(); pru_comms_set(2000, 1200);` → PAN to far right, TILT
slightly up. Confirms the whole ARM→PRU path without any camera.

---

### Milestone 3 — Refactor capture + control to use `pru_comms`

**Goal:** stop writing PWM sysfs; call `pru_comms_set()` instead. Keep the PID logic
intact — only the *output* changes.

**Changes:**
- `main.c`: replace the `pan_pwm_path`/`tilt_pwm_path` args with a `pru_comms_init()`
  call at startup.
- `capture/v4l2.c` `capture_loop(...)`: the `const char *pan_pwm_path` /
  `tilt_pwm_path` params become **unused** — you can drop them and instead pass a
  callback or just call `pru_comms_set()` directly (simplest for now).
- `pmw/pmw_servo.c`: **deprecate** — BBB 6.x has no `pwmchip`, and the PRU replaces it.
  Leave the file but stop calling it (or delete later).
- `control/control_loop.c`: the PID output (currently a PWM % or us value) is fed to
  `pru_comms_set(pan_us, tilt_us)` instead of to the sysfs writer.

> 💡 **CARD — refactor vs rewrite.** You are NOT throwing code away; you are swapping
> the *sink* of the PID. The `AxisState` / centroid math stays. This is the safe way to
> evolve firmware: change one boundary, keep the tested core.

**Test:** run `make turret`, aim camera at a bright moving object → servos track it
(up to motion-detect FPS, but smooth thanks to PRU).

---

### Milestone 4 — End-to-end with motion detection (no ML)

**Goal:** prove the full loop: camera → `motion_detect.c` centroid → PID → PRU servos.
This validates everything *before* adding ML complexity.

**Check:** `detection/motion_detect.h` already exposes a centroid; `control_loop` already
loops. Wire centroid → PID target. Done.

---

### Milestone 5 — Add FOMO (optional, ML upgrade)

**Goal:** replace the motion centroid with a **FOMO monochrome 96×96 quantized TFLite**
detector. Expect ~15–30 FPS (see perf notes in chat). Its bounding-box center becomes
the PID target instead of the motion centroid.

**Build notes:**
- Use a **NEON-enabled TFLite** build for the A8 (Debian `tensorflow-lite` or a TFLM
  build). Plain Eigen backend halves your FPS.
- Run detection in its **own thread**, but remember it's one core — don't expect true
  parallelism with capture+control.
- Grayscale input cuts preprocessing 3×.

> 🏢 **INTERVIEW:** "How would you profile / speed this up?" → measure model-alone FPS
> first (`time ./detect --bench --iters 50`), then pipeline cost; use NEON, fix input
> size, decouple threads, downscale.

---

## 3. C Concept Cards you'll meet (and why interviews love them)

| Construct | Where used | Interview weight |
|-----------|-----------|------------------|
| `volatile` | shared RAM, R30 | ⭐⭐⭐⭐⭐ "why volatile on HW regs" |
| `struct` + identical layout on 2 cores | `servo_cmd` | ⭐⭐⭐⭐ SPSC shared memory |
| pointer cast / `mmap` | `pru_comms.c` | ⭐⭐⭐⭐ userspace HW access |
| bit ops `<<`, `|`, `& ~` | R30 mapping | ⭐⭐⭐⭐ register bitfields |
| `uint32_t` fixed width | all shared data | ⭐⭐⭐ portability / no padding |
| `for` delay loop (constant) | PRU timing | ⭐⭐⭐ why `__delay_cycles` needs a constant |
| `O_SYNC` / `MAP_SHARED` | mmap flags | ⭐⭐⭐ cache coherency |
| sequence number (SPSC) | `seq` | ⭐⭐⭐⭐ lock-free producer/consumer |
| clamp `a<b?b:...` | sanitize us | ⭐⭐ defensive input |

**Practice habit:** for every card above, before coding write *one line* explaining it
in your own words in a `NOTES.md`. That's exactly how firmware interviews at TI/NXP/
Qualcomm probe — they want you to *explain* the bit, not just use it.

---

## 4. Open items to confirm before coding

- [ ] **Servo 5 V/GND pins** actually used (plan assumes P9_5/P9_6 5 V + P9_1/P9_2 GND).
- [ ] **Tilt = P9_30** confirmed (alt PRU R30 balls: P9_28/29/30/31 from cape DTS).
- [ ] **Park-at-center on `stop`?** (firmware sets 1500 µs before halting) — recommended.
- [ ] Keep `pmw/pmw_servo.c` as reference or delete after M3.

---

## 5. Safety (unchanged from the servo work)

- Power the board **off** before changing any P8/P9 wiring (live changes can brown-out
  the PMIC and corrupt eMMC).
- Externally powered servos must share the board's GND or they can back-drive the 3.3 V
  domain.
- Only `/boot/firmware/uEnv.txt` is board-local; everything else is `git pull`.
