# USB Camera on the BeagleBone Black — Integration & Yocto Packaging Guide

> Audience: you are new to embedded Linux. This document explains *why* the code
> changes, *what* changes, the *concepts* behind each piece, *how to test* before
> packaging, and *how to bake it all into a Yocto image*. Read it top-to-bottom
> once; afterwards it works as a reference.
>
> Companion docs already in the repo:
> - `readme.md` — project overview, hardware, machine roles, build order.
> - `BUILD_GUIDE.md` — BBB first-time setup, servo wiring, and the full Yocto
>   walkthrough (Parts 7–12). This guide updates a few of those parts for the
>   single-board architecture.

---

## 1. What changed and why (the "server camera" is gone)

### 1.1 The old design (camera NOT on the board)

```
Debian laptop                              BeagleBone Black
┌───────────────────────────┐  TCP/IP    ┌───────────────────────────┐
│ capture (V4L2)            │ ─────────► │ websocket receiver        │
│ detection (color)         │  "x,y\n"  │ control loop              │
│ position_sender ──────────┼──────────► │ pwm_servo → servos        │
└───────────────────────────┘            └───────────────────────────┘
   main_debian.c                          main_bbb.c
```

This split existed **only** because the camera lived on a different machine.
The laptop grabbed frames, found the target, and shipped the coordinates to the
board over the network so the board could move the servos.

### 1.2 The new design (USB camera plugged into the BBB)

```
BeagleBone Black (one process, no network)
┌──────────────────────────────────────────────────────────────┐
│  capture (V4L2) → detection (color) → control loop → pwm_servo │
│  main.c                                                       │
└──────────────────────────────────────────────────────────────┘
                          │
                          ▼
                       servos
```

Now the USB webcam is a `/dev/video0` device **on the board itself**. There is
no second machine, so there is nothing to send coordinates to. The camera,
detection, control loop, and PWM all run in a single `main.c` process.

### 1.3 What this makes obsolete

| Obsolete file(s) | Why it's no longer needed |
|---|---|
| `main_debian.c` | It was the *laptop* half: capture + detect + send. No laptop anymore. |
| `main_bbb.c` | It was the *receiver* half: accept socket, then control. Nothing to receive. |
| `websocket/position_sender.c` / `.h` | Sender side of the network bridge. |
| `websocket/position_reciever.c` / `.h` | Receiver side of the network bridge. |
| `websocket/Makefile` | Belongs to the deleted directory. |

These are **deleted from the repo** during the cleanup step (Section 5). Keep
them only in your git history as a record of how you proved the approach worked
in two stages — do not rebuild them.

> **Important nuance:** the WebSocket *dashboard* mentioned in `readme.md`
> (streaming the live feed to a browser) is a **separate** feature from this
> position bridge. This guide removes the *position* bridge. If you later add a
> browser dashboard, that is a new, optional feature — it does not bring back
> `main_debian.c`/`main_bbb.c`.

---

## 2. Embedded-Linux concepts you need (beginner section)

### 2.1 V4L2 — how Linux talks to a camera

**V4L2** = *Video for Linux 2*, the kernel API for video devices. A USB webcam
shows up as `/dev/video0`. Your code never reads pixels from the device file
directly; instead it uses `ioctl()` ("I/O control") calls to tell the kernel
driver what to do:

- `VIDIOC_QUERYCAP` — ask the driver what it supports.
- `VIDIOC_S_FMT` — set resolution + pixel format (you use `V4L2_PIX_FMT_YUYV`,
  a raw YUV format; no compression).
- `VIDIOC_REQBUFS` — ask the kernel to allocate video buffers.
- `VIDIOC_QBUF` / `VIDIOC_DQBUF` — put a buffer back in the queue / take a
  filled buffer out.
- `VIDIOC_STREAMON` / `VIDIOC_STREAMOFF` — start/stop the flow of frames.

**mmap** ("memory map"): the kernel writes frames into memory it shares with
your process, so you get a pointer to pixels without copying. Your
`map_buffers()` does this; `buffer_addresses[]` holds those pointers.

> Why does this matter for Yocto? The kernel on the board must have the V4L2
> driver + your webcam's driver (usually `uvcvideo` for USB webcams) built in.
> A *minimal* Yocto image may not include `uvcvideo` by default — see Section 7.

### 2.2 YUYV pixels and your detection code

A frame is a flat array of bytes. In `V4L2_PIX_FMT_YUYV` the layout is
`Y0 U0 Y1 V0 Y2 U1 Y2 V1 …` — two pixels share one U and one V. Your
`color_threshold.c` already handles this correctly in `get_pixel_yuv()` (it
reads one Y per pixel, and one U/V per 2×2 block). You do **not** need to change
the detection math — only *who calls it*.

### 2.3 sysfs PWM — how Linux moves a servo

There is no special "servo driver" to install. Linux exposes PWM channels as
ordinary files under `/sys/class/pwm/`. Writing a number to a file sets the
period/duty/enable. Your `pmw_servo.c` already does this with `fopen`/`fprintf`
— that is the normal, correct way. No library needed.

### 2.4 Cross-compilation vs native compilation

- **Native:** compile on the same CPU type you run on (e.g. `gcc` on the BBB,
  or `gcc` on your x86 laptop). Simple, but slow on a small board.
- **Cross-compile:** compile on a fast x86 machine but produce binaries for ARM
  (the BBB's CPU). **This is what Yocto does for you** — it sets up an ARM
  toolchain automatically, so you almost never invoke a cross-compiler by hand.

### 2.5 Yocto in one paragraph

Yocto is not an OS; it is a **build system that produces a Linux OS image**.
- **Poky** = the reference build system + a base distro.
- **bitbake** = the command that reads recipes and builds things.
- **layer** = a collection of recipes (e.g. `meta-ti` adds BeagleBone support;
  you make `meta-cameraturret` for your app).
- **recipe** (`.bb`) = a file describing *how to fetch, compile, install, and
  package* one piece of software (your app).
- **do_compile / do_install** = the two tasks you care about: compile your C
  sources, then copy the binary + systemd unit into the image.
- **systemd** = the init system that starts your app as a service at boot.

You already have a Yocto outline in `BUILD_GUIDE.md` Parts 7–12. Section 7 here
only adjusts the **recipe's `do_compile`** for the single-binary architecture.

---

## 3. Files to KEEP and what to do with each

| File | Action | Notes |
|---|---|---|
| `capture/capture.h` | **Modify** | Change `capture_loop()` signature (Section 4.2). |
| `capture/v4l2.c` | **Modify** | Remove the duplicate `Position` struct; remove the `break;` so it loops forever; drive the control loop + PWM (Section 4.2). |
| `capture/Makefile` | **Modify** | Remove the bogus X11/`libv4l2all` link flags (Section 4.4). |
| `detection/color_threshold.h` | Keep | Already includes `capture.h` for `Position`. |
| `detection/color_threshold.c` | **Modify** | Remove the local `Position` redefinition; guard its `main()` test (Section 4.5). |
| `control/control_loop.h` / `.c` | Keep | No change needed. |
| `pmw/pmw_servo.h` / `.c` | **Modify** | Guard its `main()` test so it doesn't clash at link time (Section 4.5). |
| `main.c` | **Create** | The new consolidated program (Section 4.1). You write this. |
| `Makefile` (top level) | **Modify** | Build one `turret` binary instead of `main_debian`/`main_bbb` (Section 4.4). |

---

## 4. The code changes in detail

> The design rule from `readme.md` stands: **you write these bodies yourself.**
> The snippets below are the target — type them in, don't copy-paste blindly, so
> you can explain every line.

### 4.1 New file: `main.c` (the consolidated program)

This replaces both `main_debian.c` and `main_bbb.c`. Everything runs on the BBB.

```c
#include "capture/capture.h"
#include "detection/color_threshold.h"
#include "control/control_loop.h"
#include "pmw/pmw_servo.h"

#include <stdio.h>
#include <unistd.h>

/* These paths come from `config-pin` + exporting the PWM channel on the BBB.
   Confirm them on your board with: ls /sys/class/pwm/  (see BUILD_GUIDE Part 2) */
#define PAN_PWM_PATH  "/sys/class/pwm/pwmchip0/pwm0"
#define TILT_PWM_PATH "/sys/class/pwm/pwmchip0/pwm1"

#define WIDTH  640
#define HEIGHT 480

int main(void) {
    /* 1. Camera */
    int fd = open_device("/dev/video0");
    if (fd < 0) { perror("open device"); return 1; }

    set_format(fd, WIDTH, HEIGHT);
    int n = request_buffers(fd, 4);
    if (n < 0) { close(fd); return 1; }
    for (int i = 0; i < n; i++) map_buffers(fd, i);
    queue_all_buffers(fd, n);
    start_streaming(fd);

    /* 2. Servos (start centered at 90°) */
    AxisState pan  = { .current_angle = 90 };
    AxisState tilt = { .current_angle = 90 };
    pwm_set_period_ns(PAN_PWM_PATH,  20000000);  /* 20 ms = 50 Hz */
    pwm_set_period_ns(TILT_PWM_PATH, 20000000);
    pwm_enable(PAN_PWM_PATH,  1);
    pwm_enable(TILT_PWM_PATH, 1);

    /* 3. The loop: capture → detect → control → PWM, forever */
    capture_loop(fd, n, WIDTH, HEIGHT, &pan, &tilt, PAN_PWM_PATH, TILT_PWM_PATH);

    cleanup(fd, n);
    return 0;
}
```

### 4.2 Modify `capture/capture.h` and `capture/v4l2.c`

**`capture.h`** — `capture_loop` now takes the control state + PWM paths:

```c
void capture_loop(int fd, int buffer_count, int width, int height,
                  AxisState *pan, AxisState *tilt,
                  const char *pan_pwm_path, const char *tilt_pwm_path);
```

**`v4l2.c`** changes:

1. Add includes at the top (so it knows `AxisState`, `error_to_angle_delta`,
   `servo_set_angle`, etc.):
   ```c
   #include "control/control_loop.h"
   #include "pmw/pmw_servo.h"
   ```
2. **Delete** the local `Position` typedef and the forward declaration near the
   top of `v4l2.c`. Instead `#include "capture/capture.h"` (which already defines
   `Position`). This removes a duplicate type definition.
3. **Replace** `capture_loop` with a version that loops forever and drives the
   servos. Tune the gains once you see real motion:

```c
void capture_loop(int fd, int buffer_count, int width, int height,
                  AxisState *pan, AxisState *tilt,
                  const char *pan_pwm_path, const char *tilt_pwm_path) {
    const float pan_gain  = 0.05f;   /* degrees of correction per pixel off-center */
    const float tilt_gain = 0.05f;
    const float smoothing = 0.5f;    /* 0..1, higher = smoother/slower */
    const float deadband  = 0.5f;    /* ignore tiny changes to stop jitter */

    while (1) {
        struct v4l2_buffer buff;
        memset(&buff, 0, sizeof(buff));
        buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buff.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buff) < 0) {
            perror("FAILED TO DEQUEUE BUFFER");
            continue;
        }

        Position pos = find_target_position(buffer_addresses[buff.index], width, height);
        if (pos.found) {
            printf("Target at (%d, %d)\n", pos.x, pos.y);

            /* --- Pan axis (left/right) --- */
            float pan_delta = error_to_angle_delta(pos.x, width / 2, pan_gain);
            float pan_raw   = pan->current_angle + pan_delta;
            float pan_new   = clamp_angle(
                                  smooth_angle(pan->current_angle, pan_raw, smoothing),
                                  0, 180);
            if (should_update(pan->current_angle, pan_new, deadband)) {
                servo_set_angle(pan_pwm_path, pan_new);
                pan->current_angle = pan_new;
            }

            /* --- Tilt axis (up/down) --- */
            float tilt_delta = error_to_angle_delta(pos.y, height / 2, tilt_gain);
            float tilt_raw   = tilt->current_angle + tilt_delta;
            float tilt_new   = clamp_angle(
                                  smooth_angle(tilt->current_angle, tilt_raw, smoothing),
                                  0, 180);
            if (should_update(tilt->current_angle, tilt_new, deadband)) {
                servo_set_angle(tilt_pwm_path, tilt_new);
                tilt->current_angle = tilt_new;
            }
        }

        /* put the buffer back so the driver can fill it again */
        if (ioctl(fd, VIDIOC_QBUF, &buff) < 0) {
            perror("FAILED TO RE-QUEUE BUFFER");
        }
    }
}
```

> **Sign/orientation gotcha:** if the turret moves the *wrong way* (target goes
> left, camera turns right), flip the sign of `pan_gain` or `tilt_gain`. That
> depends only on how you mounted the servos.

### 4.3 Remove the duplicate `Position` struct in `detection/color_threshold.c`

`color_threshold.c` currently redefines `Position` at the top. Since
`color_threshold.h` already `#include "capture/capture.h"` (which defines it),
**delete** that local `typedef struct { ... } Position;` block from
`color_threshold.c`. This prevents a confusing duplicate-type situation once
everything links together.

### 4.4 Fix the Makefiles

**Top-level `Makefile`** — builds a single `turret` binary (no websocket, no
two-machine split):

```make
# Top-level Makefile — Camera Turret (single-board, BeagleBone Black)
CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
INCLUDES = -I. -I./capture -I./control -I./detection -I./pmw
LIBS    = -lpthread -lm -lrt

CAPTURE_OBJS   = capture/v4l2.o
CONTROL_OBJS   = control/control_loop.o
DETECTION_OBJS = detection/color_threshold.o
PWM_OBJS       = pmw/pmw_servo.o
OBJS = $(CAPTURE_OBJS) $(CONTROL_OBJS) $(DETECTION_OBJS) $(PWM_OBJS)

.PHONY: all turret clean

all: $(OBJS)
	@echo "Objects built. Write main.c (see INTEGRATION_GUIDE.md), then: make turret"

# Final on-board binary: camera + detection + control + pwm, no network
turret: main.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ main.c $(OBJS) $(LIBS)

clean:
	rm -f turret main.o
	rm -f capture/*.o capture/v4l2 capture/frame.yuv
	rm -f control/*.o control/*.a
	rm -f detection/*.o detection/*.a
	rm -f pmw/*.o pmw/*.a
```

**`capture/Makefile`** — your current version links against X11 libraries
(`-lX11 -lXext … -lv4l2all`). Those were for an old screen-capture idea and
**do not exist on a headless BBB Yocto image**. V4L2 is used via kernel
`ioctl()`, so no extra library is needed. Simplify it:

```make
CC      = gcc
CFLAGS  = -Wall -Wextra -g

all: v4l2.o

v4l2.o: v4l2.c capture.h
	$(CC) $(CFLAGS) -I. -c -o $@ v4l2.c

clean:
	rm -f *.o v4l2

.PHONY: all clean
```

### 4.5 Guard the leftover `main()` test stubs

Three files contain a `main()` used only for standalone testing:
`detection/color_threshold.c`, `pmw/pmw_servo.c`, and the deleted
`websocket/position_sender.c`. When you later link *everything* into one binary
(`main.c`), having extra `main()` symbols causes a **link error**
(`multiple definition of 'main'`). Fix the two you keep by wrapping their test
`main` in a guard:

```c
#ifdef STANDALONE_TEST
int main() {
    /* ... existing test code ... */
}
#endif
```

Then build a standalone test only when you define the guard, e.g.:
`gcc -DSTANDALONE_TEST detection/color_threshold.c -o test_color && ./test_color`.
The normal `make turret` build does **not** define the guard, so no extra `main`
is compiled.

---

## 5. Cleanup already performed (obsolete files removed)

These were deleted from the repo because the network bridge no longer exists:

```
main_debian.c            # laptop capture+sender proof-of-concept
main_bbb.c               # board receiver+control proof-of-concept
websocket/               # entire directory (position_sender / position_reciever)
```

Also removed: accidentally-committed build artifacts (`*.o`, `*.a`,
`capture/v4l2` binary, `capture/frame.yuv`, empty `detection/test_images`) and
added a `.gitignore` so they never get committed again.

To see what changed: `git status` and `git log --oneline -1`.

---

## 6. How to TEST before touching Yocto

Yocto builds take 1–3+ hours. **Never** debug your app logic inside a Yocto
build. Test natively first, in this order:

### Step A — Detection alone (no camera, no board)
On your Mac or the Debian laptop, generate a `frame.yuv` (e.g. capture one
frame with `save_to_file`, or keep the existing test frame) and run:
```bash
gcc -DSTANDALONE_TEST detection/color_threshold.c -o test_color -lm
./test_color
```
Confirm it prints a sensible target position for a colored object. Adjust the
U/V thresholds in `is_target_color()` for your real object's color.

### Step B — PWM alone (on the BBB, no camera)
Build `pmw_servo.c` with the guard off but a small test, or temporarily call
`servo_set_angle()` from a tiny program. Confirm both servos sweep 0°→90°→180°
smoothly. Verify the sysfs paths with `ls /sys/class/pwm/`. (Full procedure in
`BUILD_GUIDE.md` Part 2 and Part 5, steps 1–3.)

### Step C — Capture alone (on the BBB, no servos)
On the BBB:
```bash
sudo config-pin p9.14 pwm   # if you also need PWM pins later
v4l2-ctl --list-devices     # confirm /dev/video0 exists
gcc -I. capture/v4l2.c -o cap_test
./cap_test                  # watch "Captured frame… Target at (x,y)" print
```
If `/dev/video0` is missing, the kernel lacks `uvcvideo` (see Section 7).

### Step D — Full loop (on the BBB, real hardware)
```bash
make turret                 # builds main.c + all modules
./turret                    # needs root for /sys/class/pwm writes
```
Move a colored object in front of the webcam. Both servos should track it. If
the turret over- or under-corrects, tune `pan_gain`/`tilt_gain`/`smoothing` in
`capture_loop`. Only once this runs cleanly do you package it.

---

## 7. Compile & pack into a Yocto image (BeagleBone Black)

All of this happens on your **Windows/WSL2** machine (the Yocto host), following
`BUILD_GUIDE.md` Parts 7–10. Below are the **adjustments** for the
single-binary architecture.

### 7.1 Make sure the image has camera + PWM kernel support

A `core-image-minimal` may omit:
- `uvcvideo` (USB webcam driver) — without it, no `/dev/video0`.
- PWM / `config-pin` userspace tools.

Add to `conf/local.conf` (or your custom image recipe):
```
IMAGE_INSTALL:append = " cameraturret v4l-utils"
```
and confirm `meta-ti` enables PWM for the BeagleBone (it generally does).
`v4l-utils` gives you `v4l2-ctl` on the board for debugging. If `/dev/video0`
still doesn't appear after flashing, you need the `uvcvideo` kernel module —
enable it in the kernel config fragment for your layer and rebuild.

### 7.2 The recipe (`meta-cameraturret/recipes-apps/cameraturret/cameraturret_1.0.bb`)

Update `do_compile` to build the **consolidated** `main.c` (not the old
`main_debian.c`/`main_bbb.c`):

```bitbake
SUMMARY = "Camera turret tracking application (single-board)"
LICENSE = "CLOSED"

SRC_URI = "git://<your-repo-url>;protocol=https;branch=main"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

DEPENDS = ""

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        main.c \
        capture/v4l2.c \
        detection/color_threshold.c \
        control/control_loop.c \
        pmw/pmw_servo.c \
        -I. -I./capture -I./control -I./detection -I./pmw \
        -lpthread -lm -lrt \
        -o cameraturret
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 cameraturret ${D}${bindir}/cameraturret

    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/cameraturret.service ${D}${systemd_unitdir}/system/
}

inherit systemd
SYSTEMD_SERVICE:${PN} = "cameraturret.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${systemd_unitdir}/system/cameraturret.service"
```

> Note: the recipe compiles `main.c` directly. The `STANDALONE_TEST` guards in
> `color_threshold.c`/`pmw_servo.c` are **not** defined here, so their test
> `main()`s are excluded — no link conflict.

### 7.3 The systemd unit (`files/cameraturret.service`)

Your app writes to `/sys/class/pwm`, which needs root. systemd runs services as
root by default, so this is fine:

```ini
[Unit]
Description=Camera Turret Tracking Service
After=network.target

[Service]
ExecStart=/usr/bin/cameraturret
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

### 7.4 Build, flash, verify

```bash
# inside poky/build on WSL2, after source oe-init-build-env
bitbake core-image-minimal     # rebuilds image WITH your app + service
```

1. Find the image: `tmp/deploy/images/beaglebone-yocto/*.wic` (or `.wic.xz`).
2. Flash to microSD with balenaEtcher (Windows).
3. Hold the BBB boot button while powering on to boot from SD.
4. SSH in, then verify:
   - `ls /dev/video0` → exists (camera driver present).
   - `systemctl status cameraturret` → `active (running)`, no manual login.
   - Move a colored object in front of the camera → both servos track it, with
     **no laptop connected**.

---

## 8. Final end-to-end checklist

- [ ] `main_debian.c`, `main_bbb.c`, `websocket/` deleted from repo
- [ ] `.gitignore` added; no `*.o`/`*.a`/`frame.yuv` committed
- [ ] Duplicate `Position` struct removed from `v4l2.c` and `color_threshold.c`
- [ ] `capture_loop()` loops forever and drives pan+tilt servos
- [ ] `capture/Makefile` no longer links X11 / `libv4l2all`
- [ ] Test `main()`s guarded with `STANDALONE_TEST`
- [ ] `make turret` builds on the BBB; `./turret` tracks a real object
- [ ] Yocto recipe builds `main.c` consolidated; `cameraturret.service` enabled
- [ ] Flashed image: camera present, service auto-starts, tracking works headless

---

## 9. Quick command reference (BBB)

```bash
# confirm camera
v4l2-ctl --list-devices

# confirm PWM sysfs after config-pin
ls /sys/class/pwm/

# build & run
make turret
sudo ./turret

# check the service after flashing Yocto
systemctl status cameraturret
journalctl -u cameraturret -f
```
