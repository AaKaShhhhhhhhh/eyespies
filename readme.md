# Camera-Tracking Servo Turret

A pan-tilt camera turret that visually detects an object and tracks it in real time, running entirely on an embedded Linux board. This README is the single reference for the whole project — hardware, software, machine roles, folder structure, and the build order.

---

## 1. What This Project Actually Is

A stationary two-axis (pan + tilt) camera mount that:
1. Captures live video from a camera
2. Locates a target object in the frame
3. Moves two servos to keep that object centered
4. Streams the live feed and system state to a browser dashboard
5. Runs as a self-contained, auto-starting service inside a custom-built embedded Linux image (Yocto)

It is **not** a wheeled robot — nothing drives across the floor. The camera turns; it doesn't chase.

---

## 2. Why This Project (Learning Goals)

- Direct hardware control (PWM/servo timing)
- Linux kernel camera interface (V4L2) — working with `ioctl` and the kernel's video subsystem directly
- Closed-loop control system design (smoothing, avoiding jitter/overshoot)
- On-device inference under real compute constraints (no cloud)
- Custom embedded Linux image construction (Yocto) — not just running Linux, but building your own image
- Network protocol implementation (WebSocket) for live remote monitoring

Every stage is meant to be built and understood well enough to defend in a technical interview — not copied and demoed.

---

## 3. Runtime Architecture

This is what's actually running once the board boots:

```
Camera hardware
      │
      ▼
┌─────────────────────────────────────────────────────────┐
│  Custom Yocto Linux image (runs on the embedded board)  │
│                                                           │
│  V4L2 capture → Object detection → Control loop          │
│                                          │                │
│                          ┌───────────────┴───────────────┐│
│                          ▼                               ▼│
│                    PWM driver                      WS server│
└──────────────────────────┬───────────────────────────┬────┘
                            ▼                           ▼
                     Servos (pan/tilt)          Browser dashboard
```

| Stage | Job |
|---|---|
| V4L2 capture | Reads raw frames from the camera via the Linux V4L2 API |
| Object detection | Locates the target in the frame (color-threshold first, TFLite model later) |
| Control loop | Maps target position to a servo angle, smooths to avoid jitter |
| PWM driver | Sets the actual pulse width that moves the servos |
| WS server | Streams the camera feed + system state to a browser over WebSocket |

**Important distinction:** the diagram above is *what the code does when running*. Yocto is a completely separate concern — it's *how that code becomes a bootable Linux system on the board* in the first place. You build and test almost everything above on your laptops first; Yocto only wraps it into a deployable image at the very end.

---

## 4. Hardware

| Item | Est. cost | Purpose |
|---|---|---|
| Embedded compute board (NPU or quad-core ARM, for real-time inference) | $60–90 | Runs the whole pipeline |
| USB webcam / camera module | $15–25 | Video input |
| 2x micro servos (pan + tilt) | $10–20 | Physical actuation |
| Pan-tilt mounting bracket kit | $8–15 | Holds camera + servos |
| Dedicated 5V servo power supply | $8–15 | Prevents board brownouts under servo load |
| Board power supply | $8–15 | Powers the compute board |
| MicroSD card | $8–12 | Boot media for the Yocto image |
| Breadboard, jumper wires, passives | $8–12 | Wiring |
| Custom 3D-printed pan-tilt mount | $15–25 | Off-the-shelf brackets don't fit this servo/size combo |
| Shipping/import buffer | $20–40 | Real cost for international orders |
| Spares/contingency | $15–25 | Spare servo, extra wires, etc. |

**Total: ~$300**

BeagleBone Black (the general learning board) is *not* the board for this specific project — its single Cortex-A8 core with no NPU will make even a lightweight object-detection model run too slowly (1–3 fps) for smooth tracking.

---

## 5. Machines & Their Roles

Three machines, three different jobs. One shared git repo is the single source of truth — no manually copying files between machines.

### MacBook Air (M5, 24GB) — writing & non-hardware testing
- VS Code + C/C++ extension
- Xcode Command Line Tools (`xcode-select --install`) for a local clang compiler
- **Does NOT run:** V4L2 hardware capture (macOS has no V4L2 — it's Linux-only) or Yocto builds
- **Does run:** detection logic (against saved test images), control-loop simulation, WebSocket server/client (localhost), TFLite model testing on saved images

### Debian laptop (4GB RAM, HDD) — real hardware testing
- `sudo apt install build-essential git v4l-utils`
- `v4l2-ctl` (from v4l-utils) to check camera capabilities before writing capture code
- This is the **only** machine with a real Linux video device — all `/capture` code is written and tested here
- Not used for Yocto builds (HDD + 4GB RAM would make builds painfully slow)

### Windows (i7-13620H, RTX 4060, 16GB) — Yocto builds
- WSL2 with Ubuntu 22.04 (check Yocto docs for currently supported host distros)
- Install the required package list from the official Yocto Project Quick Build guide
- Clone `poky` (core build system) and `meta-ti` (BeagleBone board support) **here only**
- `source oe-init-build-env`, set `MACHINE` in `conf/local.conf`
- `bitbake-layers create-layer` for your own custom app layer
- The RTX 4060 is mostly irrelevant to Yocto itself, but relevant if you ever train/fine-tune a custom detection model instead of using a pretrained one

### 2GB headless Debian server
- Same role as your earlier chat-app work: a lightweight always-on remote endpoint, useful later for testing remote deployment/monitoring alongside the board.

---

## 6. Repository Structure

```
camera-turret/
├── README.md
├── capture/              # Debian laptop only — V4L2 code
│   ├── v4l2_capture.c
│   └── Makefile
├── detection/            # Mac (and Debian later) — color detection logic
│   ├── color_threshold.c
│   ├── color_threshold.h
│   └── test_images/      # sample frames for testing without a live camera
├── control/              # Mac — control loop + simulation
│   ├── control_loop.c
│   ├── control_loop.h
│   └── simulate_control.c   # fake-object test harness, no hardware needed
├── pwm/                  # stub code now, real testing once the board arrives
│   ├── pwm_servo.c
│   └── pwm_servo.h
├── inference/            # Mac — TFLite integration
│   ├── tflite_test.c
│   └── models/           # pretrained .tflite file goes here
├── websocket/            # Mac — server + browser dashboard
│   ├── ws_server.c
│   └── ws_client_test.c
├── yocto-layer/          # Windows/WSL2 only — added much later
│   └── meta-cameraturret/
│       ├── conf/layer.conf
│       └── recipes-apps/cameraturret/cameraturret_1.0.bb
├── scripts/              # build/run helper scripts
└── docs/                 # design-decision notes — the raw material for interview prep
```

---

## 7. Build Order (Milestones)

Each stage is validated alone before combining with the next. Don't skip ahead.

1. **Servo control alone** — PWM sweep test, no camera involved yet
2. **V4L2 capture (Debian)** — open device, capture one raw frame, confirm it's readable
3. **Color-threshold detection (Mac)** — find the largest blob of a target color in a saved frame
4. **Closed-loop tracking** — combine 1–3: detected position drives the servo, add smoothing
5. **Swap in real detection** — replace color-threshold with a pretrained TFLite model (e.g. MobileNet-SSD)
6. **WebSocket dashboard** — stream camera feed + bounding box + servo angle to a browser
7. **Yocto packaging (Windows/WSL2)** — custom layer + recipe, app auto-starts via systemd on boot

### Software prep to do before the board arrives (needs zero hardware)
- V4L2 capture practiced against the Debian laptop's own webcam
- Color-detection logic tested against saved/live laptop frames
- Servo PWM theory studied from a datasheet (duty cycle, 50Hz period, pulse-width-to-angle mapping)
- Control-loop logic fully simulated in software (fake moving object, no real servo)
- WebSocket video-frame streaming practiced on the laptop
- A TFLite model running for inference on a saved image, on the laptop, before ever touching the board

---

## 8. Working Principles (How Code Gets Written Here)

- Think through the logic in plain English first, before writing any code
- Research each small piece separately (man pages, official API docs/specs) rather than asking for a full solution
- AI is used for: algorithm sanity-checks (after attempting it yourself), debugging *hints* (not fixes), and syntax lookups — never for generating whole functions or files
- Every line is written by hand and must be explainable without looking back at notes
- Self-check before moving to the next stage: *could I rebuild this from memory next week, using only docs as reference?*

---

## 9. Status

- [x] C fundamentals — pointers, structs, memory
- [x] Two-way networked chat app (raw sockets, written from scratch)
- [ ] Servo PWM control
- [ ] V4L2 capture
- [ ] Color-threshold detection
- [ ] Closed-loop tracking
- [ ] TFLite integration
- [ ] WebSocket dashboard
- [ ] Yocto image + packaging

*(Update this checklist as stages are completed.)*