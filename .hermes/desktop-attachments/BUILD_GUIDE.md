# Camera Turret — Complete Build Guide (BBB + Servos + Network Bridge)

This is the single reference document for everything from here forward: BBB setup, physical wiring, pin configuration, and every remaining code file. Code sections are skeletons — function signatures and comments explaining what each piece must do — consistent with how you've built everything so far. You write every body yourself; that's still the point.

---

## Part 1 — BeagleBone Black: First-Time Setup

1. **Power it on for the first time via the USB cable** (not the barrel adapter yet) connected to a computer — it'll boot its factory Debian image and show up as a USB mass-storage device with a getting-started page. Confirm it boots at all before anything else.
2. **Get a real network connection to it**: either plug it into your router via its onboard Ethernet port (simplest, do this if possible), or connect over USB and reach it at `192.168.7.2` by default.
3. **SSH in**: `ssh debian@192.168.7.2` (or its Ethernet IP) — default password is usually `temppwd`, confirm from BeagleBoard's current docs since this occasionally changes between image versions.
4. **Update the system and install build tools**:
   ```
   sudo apt update && sudo apt upgrade -y
   sudo apt install build-essential git -y
   ```
5. **Switch to the dedicated 5V/2A barrel power adapter** once you're past initial setup and comfortable with SSH access — don't rely on USB power once you're attaching servos and a webcam later.
6. **Clone your repo onto the board**: `git clone <your-repo-url>`.

---

## Part 2 — Wiring the Servos to the BBB

**Identify your PWM-capable pins first.** Look up the current BeagleBone Black P9 header pinout diagram and note two PWM-capable pins — commonly used ones are P9_14 and P9_16, but confirm against the diagram for your specific board revision (Rev C) since overlay support can vary.

**Mux those pins into PWM mode using `config-pin`:**
```
config-pin p9.14 pwm
config-pin p9.16 pwm
```
Confirm the sysfs PWM paths now exist:
```
ls /sys/class/pwm/
```
You should see `pwmchipN` directories. Inside, after exporting the correct channel (check your board's specific chip/channel mapping — this varies), you'll get a `pwm-X:Y` folder containing `period`, `duty_cycle`, and `enable` files. **Write down the exact paths for both pan and tilt** — you'll hardcode them as constants.

**Physical wiring, per servo:**
| Servo wire | Connects to |
|---|---|
| Signal (orange/yellow) | The PWM pin you just configured (via male-to-male jumper) |
| Power (red) | Your **separate** dedicated 5V servo power supply — never the BBB's own 5V pin |
| Ground (brown/black) | A BBB ground pin **AND** tied to the external power supply's ground — both grounds must be common |

Do this for both servos (pan + tilt), each to its own PWM pin, both sharing the same external power supply and common ground.

**Before writing any code:** double-check every wire against the MG90S datasheet's color coding, and confirm the external power supply is genuinely 5V before connecting anything.

---

## Part 3 — Repository structure (final)

```
camera-turret/
├── README.md
├── BUILD_GUIDE.md          # this document
├── capture/
│   ├── capture.c            # done — your V4L2 code
│   └── Makefile
├── detection/
│   ├── color_threshold.c    # done
│   └── color_threshold.h
├── control/
│   ├── control_loop.c        # NEW — below
│   └── control_loop.h
├── pwm/
│   ├── pwm_servo.c            # NEW — below
│   └── pwm_servo.h
├── network/
│   ├── position_sender.c      # NEW — Debian laptop side
│   └── position_receiver.c    # NEW — BBB side
├── main_debian.c               # NEW — ties capture + detection + sender
├── main_bbb.c                   # NEW — ties receiver + control + pwm
└── docs/
```

---

## Part 4 — Code Skeletons

### `control/control_loop.h`
```c
#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

typedef struct {
    float current_angle;
} AxisState;

float error_to_angle_delta(int target_pixel, int frame_center_pixel, float gain);
float smooth_angle(float previous_angle, float raw_new_angle, float smoothing_factor);
float clamp_angle(float angle, float min_angle, float max_angle);
int should_update(float previous_angle, float new_angle, float deadband_degrees);

#endif
```

### `control/control_loop.c`
```c
#include "control_loop.h"

float error_to_angle_delta(int target_pixel, int frame_center_pixel, float gain) {
    // how far target_pixel is from frame_center_pixel, scaled by gain,
    // gives you a change in degrees. Sign matters: figure out which
    // direction is positive for your pan/tilt mounting orientation.
}

float smooth_angle(float previous_angle, float raw_new_angle, float smoothing_factor) {
    // exponential moving average between previous_angle and raw_new_angle
}

float clamp_angle(float angle, float min_angle, float max_angle) {
    // constrain angle into [min_angle, max_angle]
}

int should_update(float previous_angle, float new_angle, float deadband_degrees) {
    // return 0 if |new_angle - previous_angle| < deadband_degrees, else 1
}
```

### `pwm/pwm_servo.h`
```c
#ifndef PWM_SERVO_H
#define PWM_SERVO_H

void pwm_set_period_ns(const char *pwm_path, unsigned long period_ns);
void pwm_set_duty_cycle_ns(const char *pwm_path, unsigned long duty_ns);
void pwm_enable(const char *pwm_path, int enable);
unsigned long angle_to_duty_ns(float angle_degrees);
void servo_set_angle(const char *pwm_path, float angle_degrees);

#endif
```

### `pwm/pwm_servo.c`
```c
#include "pwm_servo.h"
#include <stdio.h>

void pwm_set_period_ns(const char *pwm_path, unsigned long period_ns) {
    // open <pwm_path>/period, write period_ns as a string, close
    // standard servo period is 20,000,000 ns (50Hz)
}

void pwm_set_duty_cycle_ns(const char *pwm_path, unsigned long duty_ns) {
    // open <pwm_path>/duty_cycle, write duty_ns as a string, close
}

void pwm_enable(const char *pwm_path, int enable) {
    // open <pwm_path>/enable, write "1" or "0", close
}

unsigned long angle_to_duty_ns(float angle_degrees) {
    // map 0-180 degrees to your servo's actual pulse-width range —
    // check the MG90S datasheet for its real min/max, don't assume
    // the generic 500,000-2,500,000ns range is exactly correct
}

void servo_set_angle(const char *pwm_path, float angle_degrees) {
    // angle_to_duty_ns() then pwm_set_duty_cycle_ns()
}
```

### `network/position_sender.h` / `.c` (Debian laptop)
```c
int connect_to_server(const char *ip, int port);
// socket() + connect(), same pattern as your original chat client

void send_position(int sockfd, int x, int y);
// pack x,y into a message and send() — decide text ("123,45\n") or binary
```

### `network/position_receiver.h` / `.c` (BBB)
```c
int create_server_socket(int port);
// socket() + bind() + listen()

int accept_client(int server_fd);
// accept()

void receive_loop(int client_fd, AxisState *pan, AxisState *tilt,
                   const char *pan_pwm_path, const char *tilt_pwm_path);
// loop: recv() a position, parse x,y, run through control_loop functions
// for both axes, call servo_set_angle() for each if should_update() says yes
```

### `main_debian.c`
```c
#include "capture/capture.h"
#include "detection/color_threshold.h"
#include "network/position_sender.h"

int main(void) {
    int fd = open_device("/dev/video0");
    // set_format, request_buffers, map_buffers, queue_all_buffers, start_streaming

    int sockfd = connect_to_server(BBB_IP, BBB_PORT);

    // modified capture_loop: after find_target_position(), call
    // send_position(sockfd, pos.x, pos.y) each frame
    capture_loop(fd, buffer_count, sockfd);
}
```

### `main_bbb.c`
```c
#include "control/control_loop.h"
#include "pwm/pwm_servo.h"
#include "network/position_receiver.h"

#define PAN_PWM_PATH  "/sys/class/pwm/pwmchipX/pwm-A:B"   // fill in your real path
#define TILT_PWM_PATH "/sys/class/pwm/pwmchipY/pwm-C:D"   // fill in your real path

int main(void) {
    pwm_set_period_ns(PAN_PWM_PATH, 20000000);
    pwm_set_period_ns(TILT_PWM_PATH, 20000000);
    pwm_enable(PAN_PWM_PATH, 1);
    pwm_enable(TILT_PWM_PATH, 1);

    AxisState pan = { .current_angle = 90 };
    AxisState tilt = { .current_angle = 90 };

    int server_fd = create_server_socket(BBB_PORT);
    int client_fd = accept_client(server_fd);

    receive_loop(client_fd, &pan, &tilt, PAN_PWM_PATH, TILT_PWM_PATH);
}
```

### Makefiles
Each folder with a `.c` file needs a simple Makefile compiling it to a `.o`, and one top-level Makefile linking everything for `main_debian` (capture + detection + sender) and one for `main_bbb` (control + pwm + receiver) separately, since they run on different machines with different available headers (V4L2 headers only exist on the Debian side; sysfs PWM code only makes sense on the BBB side).

---

## Part 5 — Build & Test Order (real hardware throughout)

1. **BBB**: run the `config-pin` commands, confirm sysfs PWM paths exist (`ls /sys/class/pwm/`).
2. **BBB**: write `pwm_servo.c`. Write a tiny standalone test `main()` that sets the pan servo to 0°, waits, 90°, waits, 180°, waits. Run it, watch the real servo move. Fix until correct.
3. **BBB**: repeat for the tilt servo on its own PWM path.
4. **Either machine**: finalize `control_loop.c` against `simulate_target_position()`, confirm the printed angle output looks smooth for both a slow sine-wave motion and a sudden jump.
5. **BBB**: write `position_receiver.c` and `main_bbb.c`, wiring receiver → control loop → both servos together. Test by running `position_sender` from your Mac or the Debian laptop with values you type in by hand (a simple manual input loop counts as "real," since you're not simulating detection — you're testing the network+control+PWM chain directly).
6. **Debian laptop**: write `position_sender.c` and `main_debian.c`, combining your finished `capture.c` + `color_threshold.c`. Point it at the BBB's real IP and port.
7. **Full test**: move a colored object in front of the Debian laptop's built-in camera. Watch the BBB's servos respond in real time. This is the real milestone.

---

## Part 6 — Troubleshooting Notes

- **Servo doesn't move at all**: check `cat <pwm_path>/enable` reads `1`, and that `period`/`duty_cycle` were written successfully (no permission errors — you may need `sudo` or correct udev rules for non-root sysfs access).
- **Servo jitters or twitches constantly**: check your `deadband_degrees` in `should_update()` isn't too small, and confirm ground is genuinely shared between the BBB and the servo power supply.
- **Network connection refused**: confirm the BBB's IP address (`ip addr` on the BBB), that `position_receiver` is actually listening before `position_sender` tries to connect, and that both devices are on the same network.
- **Board resets or USB devices drop when servos move**: power draw issue — confirm servos are on their own dedicated supply, not sharing the BBB's own power rail in any way.

---

## Part 7 — Yocto Environment Setup (Windows, WSL2)

This entire part happens on the **Windows machine only**, inside WSL2 — not the Mac, not the BBB.

1. **Enable WSL2**: PowerShell (as Administrator): `wsl --install -d Ubuntu-22.04`. Reboot if prompted.
2. **Open the Ubuntu 22.04 shell** (search "Ubuntu" in the Start menu once installed) and set your Unix username/password.
3. **Install the exact host packages Yocto needs.** Do not guess this list — go to the current official Yocto Project Quick Build guide and copy the `apt install` command shown there for Ubuntu, since required packages change between Yocto releases. It will look roughly like:
   ```
   sudo apt update
   sudo apt install gawk wget git diffstat unzip texinfo gcc build-essential chrpath socat cpio python3 python3-pip python3-pexpect xz-utils debianutils iputils-ping python3-git python3-jinja2 python3-subunit zstd liblz4-tool file locales libacl1
   ```
4. **Generate and set the locale** (Yocto build fails without this): `sudo locale-gen en_US.UTF-8`.
5. **Clone the core build system and the BeagleBone board-support layer:**
   ```
   git clone git://git.yoctoproject.org/poky
   cd poky
   git clone git://git.yoctoproject.org/meta-ti
   ```
   Check meta-ti's README for which Yocto release branch matches your poky checkout (they must be compatible releases — e.g. both on "scarthgap" or whatever the current stable release is at the time you do this).
6. **Initialize the build environment:**
   ```
   source oe-init-build-env
   ```
   This drops you into a new `build/` directory with `conf/local.conf` and `conf/bblayers.conf`.
7. **Add meta-ti to your layers**, either via `bitbake-layers add-layer ../meta-ti/meta-ti-bsp` (and any other required meta-ti sub-layers it depends on — check its README) or by manually editing `conf/bblayers.conf`.
8. **Set your target machine** in `conf/local.conf`:
   ```
   MACHINE = "beaglebone-yocto"
   ```
   Confirm this exact machine name against meta-ti's current documentation — it occasionally differs by release.
9. **Do a first test build with no custom code yet**, just to confirm your whole toolchain works before adding complexity:
   ```
   bitbake core-image-minimal
   ```
   This will take a genuinely long time on first run (potentially 1-3+ hours depending on your machine and internet speed, since it's cross-compiling the entire kernel, bootloader, and base system from source). Let it run to completion before proceeding — don't debug your own recipe against a build that hasn't even proven the baseline works.
10. **Confirm the output image exists**: check `tmp/deploy/images/beaglebone-yocto/` for a `.wic` or similar image file.

---

## Part 8 — Writing Your Custom Layer and App Recipe

Still on Windows/WSL2, inside your `poky` directory:

1. **Scaffold your own layer:**
   ```
   bitbake-layers create-layer ../meta-cameraturret
   bitbake-layers add-layer ../meta-cameraturret
   ```
2. **Create the recipe folder structure:**
   ```
   mkdir -p ../meta-cameraturret/recipes-apps/cameraturret/files
   ```
3. **Write the recipe** at `../meta-cameraturret/recipes-apps/cameraturret/cameraturret_1.0.bb`:
   ```
   SUMMARY = "Camera turret tracking application"
   LICENSE = "CLOSED"

   SRC_URI = "git://<your-repo-url>;protocol=https;branch=main"
   SRCREV = "${AUTOREV}"

   S = "${WORKDIR}/git"

   DEPENDS = ""
   # add tensorflow-lite or other dependencies here once you reach that stage

   do_compile() {
       # compile main.c (your consolidated, standalone version — see Part 11)
       # against control_loop.c, pwm_servo.c, capture.c, color_threshold.c
       # e.g.: ${CC} ${CFLAGS} main.c control/control_loop.c pwm/pwm_servo.c \
       #       capture/capture.c detection/color_threshold.c -o cameraturret
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
   You'll need to fill in the real compile command with your actual source layout and any library flags (e.g. linking against a TFLite runtime once you reach that stage).
4. **Write the systemd unit file** at `../meta-cameraturret/recipes-apps/cameraturret/files/cameraturret.service`:
   ```
   [Unit]
   Description=Camera Turret Tracking Service
   After=network.target

   [Service]
   ExecStart=/usr/bin/cameraturret
   Restart=on-failure

   [Install]
   WantedBy=multi-user.target
   ```
5. **Add your app to the image.** Either append to `conf/local.conf`:
   ```
   IMAGE_INSTALL:append = " cameraturret"
   ```
   or create a proper custom image recipe in your layer — the `local.conf` append is simpler for a first pass.

---

## Part 9 — Building the Final Image

Back in the WSL2 shell, inside your `build/` directory:
```
bitbake core-image-minimal
```
This rebuilds the image, now including your compiled app and its systemd service. Watch the build log for errors specific to your recipe (`do_compile` or `do_install` failures) — these show up clearly labeled with your recipe's name if something's wrong in your compile command or file paths.

---

## Part 10 — Flashing the Image to the BBB

1. **Locate the built image**: `tmp/deploy/images/beaglebone-yocto/` — look for a `.wic` or `.wic.xz` file (some Yocto setups also produce a `.img`).
2. **Copy it out of WSL2** to a normal Windows folder if needed (WSL2 file paths are accessible from Windows at `\\wsl$\Ubuntu-22.04\...`).
3. **Flash it to a microSD card.** On Windows, **balenaEtcher** is the simplest tool — select the image file, select your SD card, flash. (If the image is `.wic.xz`, Etcher can usually flash the compressed file directly without needing to decompress it yourself.)
4. **Boot the BBB from the SD card, not its internal eMMC**: insert the card, hold the BBB's boot button (near the SD slot) while applying power, keep holding for a few seconds after power-on. This forces it to boot from SD instead of the factory image already on the board.
5. **Confirm it boots your custom image**: connect via serial console or Ethernet/SSH and check `cat /etc/os-release` or similar to confirm it's your Yocto build, not the old Debian image.

---

## Part 11 — Consolidating for Standalone Operation (no more network bridge)

Once the webcam and servos are both physically on the BBB, the Debian-laptop/network-bridge split from Part 4-5 is no longer the final architecture — it was there to let you test before everything was on one board. Now write a single consolidated `main.c` that runs entirely on the BBB:

```c
#include "capture/capture.h"
#include "detection/color_threshold.h"
#include "control/control_loop.h"
#include "pwm/pwm_servo.h"

int main(void) {
    int fd = open_device("/dev/video0");
    // set_format, request_buffers, map_buffers, queue_all_buffers, start_streaming

    AxisState pan = { .current_angle = 90 };
    AxisState tilt = { .current_angle = 90 };

    pwm_set_period_ns(PAN_PWM_PATH, 20000000);
    pwm_set_period_ns(TILT_PWM_PATH, 20000000);
    pwm_enable(PAN_PWM_PATH, 1);
    pwm_enable(TILT_PWM_PATH, 1);

    // modified capture_loop: after find_target_position(), directly call
    // your control_loop functions for both axes and servo_set_angle() —
    // no network, no sender/receiver, all in one process now
    capture_loop(fd, buffer_count, &pan, &tilt);
}
```

This is the version your Yocto recipe's `do_compile` step actually builds — not `main_debian.c`/`main_bbb.c`, which were only for the earlier proof-of-concept stage. Keep those two files in your repo/docs as a record of how you validated the approach, but the recipe should compile this consolidated version.

---

## Part 12 — Final End-to-End Verification Checklist

- [ ] BBB boots standalone from the SD card into your custom Yocto image
- [ ] `systemctl status cameraturret` shows the service active and running, with no manual login/command needed
- [ ] Webcam is recognized (`ls /dev/video*`) automatically at boot
- [ ] Moving a colored object in front of the camera visibly moves both servos, with no laptop connected at all
- [ ] (Optional, if you've added it) the WebSocket dashboard is reachable from a browser on the same network, showing live feed + servo state
