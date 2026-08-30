/*
 * capture/mjpg_stream.c  --  LIVE VIDEO STREAM for a headless BeagleBone
 *
 * WHY THIS EXISTS
 * ---------------
 * cam_view.c draws a tiny ASCII preview in the SSH terminal. That proves the
 * detector works, but it is NOT a real "video you can watch". This program is
 * the real thing: it grabs camera frames with the SAME v4l2.c helpers the
 * turret uses, compresses each frame to JPEG with libjpeg, and serves them as
 * an MJPEG stream over HTTP. You open a browser on your laptop to:
 *
 *        http://<BeagleBone-IP>:8080/
 *
 * and you see live video. The motion centroid is marked with a bright box so
 * you can confirm the turret is "looking at" the right thing.
 *
 * It does NOT touch the servo / PRU at all -- pure observation, safe to run
 * while the servo is disconnected.
 *
 * BUILD (on the board):
 *      cd ~/eyespies
 *      make mjpg_stream
 *      ./mjpg_stream            (or:  ./mjpg_stream /dev/video1 )
 *      press Ctrl-C to quit
 *
 * If the build says "jpeglib.h: No such file" install the dev package once:
 *      sudo apt-get update && sudo apt-get install -y libjpeg-dev
 *
 * VIEWING FROM YOUR LAPTOP:
 *   - If your laptop is on the SAME network as the Bone, just open
 *     http://<IP>:8080/  (find the IP with:  hostname -I )
 *   - If the Bone is only reachable via SSH, tunnel the port:
 *     ssh -L 8080:localhost:8080 debian@<Bone-IP>
 *     then open http://localhost:8080/ on your laptop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/videodev2.h>          /* struct v4l2_buffer, VIDIOC_* */

#include <jpeglib.h>                   /* libjpeg: rock-solid JPEG encoder */

#include "capture/capture.h"           /* open/format/buffer/stream + Position */
#include "detection/motion_detect.h"    /* find_motion_position() */

#define WIDTH  320
#define HEIGHT 240
#define PORT   8080

/* ---------------------------------------------------------------------------
 * Compress a WxH 8-bit GREYSCALE plane (one brightness byte per pixel) into a
 * JPEG. libjpeg owns the output buffer; call free() on *out_buf when done.
 * Returns the buffer, sets *out_size. Returns NULL on failure.
 * ------------------------------------------------------------------------- */
static uint8_t *encode_jpeg(const uint8_t *grey, int w, int h, unsigned long *out_size) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    uint8_t *out_buf = NULL;
    unsigned long out_sz = 0;
    jpeg_mem_dest(&cinfo, &out_buf, &out_sz);   /* libjpeg allocates out_buf */

    cinfo.image_width      = w;
    cinfo.image_height     = h;
    cinfo.input_components = 1;                 /* 1 = grayscale */
    cinfo.in_color_space   = JCS_GRAYSCALE;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 70, TRUE);         /* 70 = good preview quality */

    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row = malloc(w);
    while (cinfo.next_scanline < (JDIMENSION)h) {
        const uint8_t *src = grey + cinfo.next_scanline * w;
        memcpy(row, src, w);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    free(row);

    *out_size = out_sz;
    return out_buf;
}

/* Write the whole string to a socket (handles partial writes). */
static void write_str(int fd, const char *s) {
    size_t n = strlen(s);
    while (n) {
        ssize_t k = write(fd, s, n);
        if (k <= 0) return;          /* caller checks for disconnect */
        s += k; n -= (size_t)k;
    }
}

int main(int argc, char *argv[]) {
    const char *dev = (argc > 1) ? argv[1] : NULL;

    int fd = find_capture_device(dev);
    if (fd < 0) { fprintf(stderr, "mjpg_stream: no capture device\n"); return 1; }

    set_format(fd, WIDTH, HEIGHT);
    int n = request_buffers(fd, 4);
    if (n < 0) { close(fd); return 1; }
    for (int i = 0; i < n; i++) map_buffers(fd, i);
    queue_all_buffers(fd, n);
    start_streaming(fd);
    motion_reset();   /* seed background like the turret does */

    /* ---- HTTP server ---------------------------------------------------- */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); cleanup(fd, n); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); cleanup(fd, n); return 1;
    }
    listen(srv, 4);
    printf("mjpg_stream: open  http://<this-board-IP>:%d/  in your browser "
           "(hostname -I for the IP). Ctrl-C to quit.\n", PORT);

    signal(SIGPIPE, SIG_IGN);   /* don't die if a browser closes mid-frame */

    uint8_t *grey = malloc((size_t)WIDTH * HEIGHT);

    while (1) {
        int cli = accept(srv, NULL, NULL);
        if (cli < 0) { if (errno == EINTR) continue; else { perror("accept"); break; } }

        write_str(cli,
            "HTTP/1.0 200 OK\r\n"
            "Connection: close\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "\r\n");

        int running = 1;
        while (running) {
            /* --- grab one frame (same select-with-timeout as the turret) --- */
            fd_set fds; struct timeval tv;
            FD_ZERO(&fds); FD_SET(fd, &fds);
            tv.tv_sec = 2; tv.tv_usec = 0;
            int r = select(fd + 1, &fds, NULL, NULL, &tv);
            if (r <= 0) { if (r < 0) { if (errno == EINTR) continue; perror("select"); } usleep(100000); continue; }

            struct v4l2_buffer buff;
            memset(&buff, 0, sizeof(buff));
            buff.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buff.memory  = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_DQBUF, &buff) < 0) {
                if (errno == ENODEV) { fprintf(stderr, "camera disconnected\n"); running = 0; break; }
                perror("dqbuf"); usleep(10000); continue;
            }

            const unsigned char *frame = (const unsigned char *)buffer_addresses[buff.index];

            /* YUYV packs 2 bytes/pixel; the EVEN byte of each pair is Y (brightness). */
            for (int i = 0; i < WIDTH * HEIGHT; i++)
                grey[i] = frame[(size_t)i * 2];

            /* Mark the motion centroid with a bright 5x5 box so you can SEE
               what the detector is tracking. (Pure cosmetic overlay.) */
            Position pos = find_motion_position((unsigned char *)frame, WIDTH, HEIGHT);
            if (pos.found) {
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int x = pos.x + dx, y = pos.y + dy;
                        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
                            grey[(size_t)y * WIDTH + x] = 255;
                    }
            }

            unsigned long jpgsz = 0;
            uint8_t *jpg = encode_jpeg(grey, WIDTH, HEIGHT, &jpgsz);

            /* re-queue the V4L2 buffer immediately (don't block on the socket) */
            if (ioctl(fd, VIDIOC_QBUF, &buff) < 0) perror("qbuf");

            if (jpg) {
                char hdr[128];
                int hl = snprintf(hdr, sizeof hdr,
                    "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %lu\r\n\r\n",
                    jpgsz);
                write_str(cli, hdr);
                size_t left = jpgsz;
                while (left) {
                    ssize_t k = write(cli, jpg + (jpgsz - left), left);
                    if (k <= 0) { running = 0; break; }   /* browser closed */
                    left -= (size_t)k;
                }
                write_str(cli, "\r\n");
                free(jpg);
            }

            usleep(66000);   /* ~15 fps stream, leaves CPU for nothing else (debug-only) */
        }
        close(cli);
    }

    free(grey);
    close(srv);
    cleanup(fd, n);
    return 0;
}
