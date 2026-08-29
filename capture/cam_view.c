/*
 * capture/cam_view.c  --  LIVE ASCII CAMERA VIEWER for a headless BeagleBone
 *
 * WHY THIS EXISTS
 * ---------------
 * The BeagleBone has no screen, and we don't want to install OpenCV or a
 * streaming server just to SEE what the camera sees. This program grabs frames
 * exactly like the turret does (same v4l2.c helpers) and prints a tiny
 * ASCII-art greyscale preview to the SSH terminal, with:
 *      '+'  = frame centre (where the turret WANTS the target)
 *      'X'  = the motion centroid the detector currently thinks is "you"
 *
 * If 'X' follows your hand when you wave in front of the camera, the DETECTION
 * + centroiding is good, and any servo problem is in the control loop / PRU.
 * If 'X' jumps around randomly or never appears, the detection is the bug.
 *
 * It does NOT touch the servo or the PRU at all -- pure observation.
 *
 * BUILD (on the board):
 *      cd ~/eyespies
 *      make cam_view
 *      ./cam_view            (or:  ./cam_view /dev/video1 )
 *      press Ctrl-C to quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>          /* struct v4l2_buffer, VIDIOC_*, V4L2_* */

#include "capture/capture.h"          /* open/format/buffer/stream + Position */
#include "detection/motion_detect.h"   /* find_motion_position() */

/* ---- ASCII preview geometry (fits a normal 80-col SSH terminal) ----------- */
#define COLS 80
#define ROWS 30
static const char RAMP[] = " .:-=+*#%@";   /* dark -> bright */

/* Read the Y (brightness) byte for pixel (x,y) from a packed YUYV buffer.
 * YUYV = 2 bytes/pixel: [Y0 U Y1 V] -> even/odd pixel Y at byte x*2. */
static inline unsigned char y_at(const unsigned char *f, int w, int x, int y) {
    return f[(size_t)y * (size_t)w * 2 + (size_t)x * 2];
}

int main(int argc, char *argv[]) {
    const char *dev = (argc > 1) ? argv[1] : NULL;

    int fd = find_capture_device(dev);
    if (fd < 0) { fprintf(stderr, "cam_view: no capture device\n"); return 1; }

    const int W = 320, H = 240;
    set_format(fd, W, H);
    int n = request_buffers(fd, 4);
    if (n < 0) { close(fd); return 1; }
    for (int i = 0; i < n; i++) map_buffers(fd, i);
    queue_all_buffers(fd, n);
    start_streaming(fd);
    motion_reset();   /* seed background like the turret does */

    printf("cam_view: streaming %dx%d. Ctrl-C to quit.\n", W, H);

    /* block size in source pixels for each ASCII cell */
    const int bw = W / COLS;   /* 4 */
    const int bh = H / ROWS;   /* 8 */

    while (1) {
        /* dequeue a frame (reuse the same select-with-timeout pattern) */
        fd_set fds; struct timeval tv;
        FD_ZERO(&fds); FD_SET(fd, &fds);
        tv.tv_sec = 2; tv.tv_usec = 0;
        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) { if (r < 0) perror("select"); else printf("(camera timeout)\n"); usleep(100000); continue; }

        struct v4l2_buffer buff;
        memset(&buff, 0, sizeof(buff));
        buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buff.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &buff) < 0) {
            if (errno == ENODEV) { fprintf(stderr, "camera disconnected\n"); break; }
            perror("dqbuf"); usleep(10000); continue;
        }

        const unsigned char *frame = (const unsigned char *)buffer_addresses[buff.index];

        /* ---- run the SAME detector the turret uses, so we test it for real -- */
        Position pos = find_motion_position((unsigned char *)frame, W, H);

        /* ---- render ASCII preview ------------------------------------------- */
        printf("\033[2J\033[H");   /* clear + home (works in a normal SSH term) */
        printf("cam_view  centre=+  motion=X   found=%d  (x=%d y=%d)\n",
               pos.found, pos.x, pos.y);

        const int cx = COLS / 2, cy = ROWS / 2;          /* frame centre cell */
        const int mx = (pos.found) ? pos.x * COLS / W : -1;
        const int my = (pos.found) ? pos.y * ROWS / H : -1;

        for (int ry = 0; ry < ROWS; ry++) {
            for (int rx = 0; rx < COLS; rx++) {
                /* average brightness over the block this cell covers */
                long sum = 0, cnt = 0;
                for (int y = ry * bh; y < (ry + 1) * bh; y++)
                    for (int x = rx * bw; x < (rx + 1) * bw; x++) {
                        sum += y_at(frame, W, x, y);
                        cnt++;
                    }
                int lum = (int)(sum / cnt);             /* 0..255 */
                char ch = RAMP[lum * (int)(sizeof(RAMP) - 2) / 255];

                if (ry == my && rx == mx)      ch = 'X';  /* motion centroid */
                else if (ry == cy && rx == cx) ch = '+';  /* frame centre */
                putchar(ch);
            }
            putchar('\n');
        }

        /* re-queue the buffer */
        if (ioctl(fd, VIDIOC_QBUF, &buff) < 0) perror("qbuf");
        usleep(80000);   /* ~12 fps preview, keeps CPU free */
    }

    cleanup(fd, n);
    return 0;
}
