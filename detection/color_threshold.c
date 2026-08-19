#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<unistd.h>

#include "capture/capture.h"   /* Position is defined here */

/* ---------------------------------------------------------------------------
 * Frame format note
 * ---------------------------------------------------------------------------
 * The camera (USB2.0 PC CAMERA / GEMBIRD) streams PACKED YUYV (a.k.a. YUY2):
 *   2 bytes per pixel, laid out per pixel-pair as:  Y0 U Y1 V
 *   Every image row occupies  width * 2  bytes.
 * U and V are shared between the two pixels of a pair.
 * This is NOT planar YUV420 (which would be: all Y, then all U, then all V).
 * The original code read the buffer as planar YUV420 -> garbage chroma ->
 * false "target found" every frame. The functions below read packed YUYV.
 * ------------------------------------------------------------------------- */

unsigned char* load_yuv_frame(const char *path , int width , int height) {
    int fd = open(path , O_RDONLY);
    if(fd < 0) {
        perror("open");
        return NULL;
    }

    /* YUYV packed: 2 bytes per pixel */
    size_t expected = (size_t)width * (size_t)height * 2;
    unsigned char *frame = malloc(expected);
    if(!frame) {
        perror("malloc");
        close(fd);
        return NULL;
    }

    ssize_t bytes_read = read(fd, frame, expected);
    if(bytes_read < 0) {
        perror("read");
        free(frame);
        close(fd);
        return NULL;
    }
    if((size_t)bytes_read != expected) {
        fprintf(stderr,
                "short read: expected %zu bytes, got %zd bytes\n",
                expected, bytes_read);
        free(frame);
        close(fd);
        return NULL;
    }

    close(fd);
    return frame;
}

void get_pixel_yuv(unsigned char *frame, int height, int width, int x, int y,
                   unsigned char *out_y, unsigned char *out_u, unsigned char *out_v) {
    if(x < 0 || x >= width || y < 0 || y >= height) {
        fprintf(stderr, "Coordinates out of bounds\n");
        return;
    }

    /* Packed YUYV: 2 bytes per pixel. A pixel pair occupies 4 bytes:
       Y0 U Y1 V. U/V are shared by the pair. */
    int row_bytes = width * 2;
    int base = y * row_bytes + (x / 2) * 4;
    if ((x & 1) == 0) *out_y = frame[base];       /* even pixel -> Y0 */
    else              *out_y = frame[base + 2];   /* odd  pixel -> Y1 */
    *out_u = frame[base + 1];                     /* U (shared) */
    *out_v = frame[base + 3];                     /* V (shared) */
}

/* ---------------------------------------------------------------------------
 * TUNABLE TARGET COLOR
 * ---------------------------------------------------------------------------
 * In YUV, neutral gray is (U=128, V=128). A COLORED object pushes U and/or V
 * AWAY from 128. So we model the target as a CENTER (uc, vc) plus a tolerance,
 * and a pixel matches only if it is CLOSE to that center. Neutral gray is far
 * from any colored center, so it is correctly rejected (no more false positive
 * when pointing at a plain table / covered camera).
 *
 * To find your object's center, build with -DSTANDALONE_TEST and run:
 *     gcc -DSTANDALONE_TEST -I. -I./capture -I./control -I./pmw \
 *         detection/color_threshold.c -o calibrate
 *     ./calibrate <frame.raw> <width> <height>
 * It prints the average U/V of the center box. Put your colored object in
 * front of the camera, capture a raw frame with ffmpeg, then set U_CENTER/V_CENTER
 * to those numbers (give TOL ~+/-25 slack).
 *
 * Default below targets a green-ish object (U low, V low).
 * ------------------------------------------------------------------------- */
#define U_CENTER 90
#define V_CENTER 90
#define TOL     30

int is_target_color(unsigned char y , unsigned char u , unsigned char v) {
    const unsigned char y_min = 40;   /* ignore near-black (covered camera) */
    const unsigned char y_max = 235;

    if (y < y_min || y > y_max) return 0;

    int du = (int)u - U_CENTER;
    int dv = (int)v - V_CENTER;
    if (du < -TOL || du > TOL) return 0;
    if (dv < -TOL || dv > TOL) return 0;
    return 1;
}

Position find_target_position(unsigned char *frame , int width , int height) {
    Position pos = {0, 0, 0};

    long sum_x = 0;
    long sum_y = 0;
    long count = 0;

    for(int yy = 0; yy < height; yy++) {
        for(int xx = 0; xx < width; xx++) {
            unsigned char py, u, v;
            get_pixel_yuv(frame, height, width, xx, yy, &py, &u, &v);
            if(is_target_color(py, u, v)) {
                sum_x += xx;
                sum_y += yy;
                count++;
            }
        }
    }

    /* Require a meaningful amount of matched area. A single stray pixel
       (sensor noise) must NOT claim "target found". 1% of the frame is a
       reasonable floor for a small object; lower it for tiny targets. */
    long min_pixels = (long)width * (long)height / 100;
    if (count > min_pixels) {
        pos.x = (int)(sum_x / count);
        pos.y = (int)(sum_y / count);
        pos.found = 1;
    }

    return pos;
}

void mark_postion_on_frame(unsigned char *frame , int width , int height , Position pos) {
    if(!pos.found) return;

    unsigned char mark_y = 255;
    unsigned char mark_u = 0;
    unsigned char mark_v = 0;

    int row_bytes = width * 2;
    int mark_size = 5;
    for(int dy = -mark_size; dy <= mark_size; dy++) {
        for(int dx = -mark_size; dx <= mark_size; dx++) {
            int mx = pos.x + dx;
            int my = pos.y + dy;
            if(mx >= 0 && mx < width && my >= 0 && my < height) {
                int base = my * row_bytes + (mx / 2) * 4;
                if ((mx & 1) == 0) frame[base]     = mark_y;
                else               frame[base + 2] = mark_y;
                frame[base + 1] = mark_u;
                frame[base + 3] = mark_v;
            }
        }
    }
}

#ifdef STANDALONE_TEST
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <frame.raw> <width> <height>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *path = argv[1];
    int width = atoi(argv[2]);
    int height = atoi(argv[3]);

    unsigned char *frame = load_yuv_frame(path, width, height);
    if(!frame) {
        return EXIT_FAILURE;
    }

    /* Calibration: average U/V over a center box so we can set the threshold. */
    int bw = width/4, bh = height/4;
    int x0 = width/2 - bw/2, y0 = height/2 - bh/2;
    long su = 0, sv = 0, n = 0;
    int umin = 255, umax = 0, vmin = 255, vmax = 0;
    for (int y = y0; y < y0 + bh; y++)
        for (int x = x0; x < x0 + bw; x++) {
            unsigned char py, u, v;
            get_pixel_yuv(frame, height, width, x, y, &py, &u, &v);
            su += u; sv += v; n++;
            if (u < umin) umin = u; if (u > umax) umax = u;
            if (v < vmin) vmin = v; if (v > vmax) vmax = v;
        }
    printf("Center box %dx%d avg U=%.1f V=%.1f  (U %d..%d, V %d..%d)\n",
           bw, bh, (double)su/n, (double)sv/n, umin, umax, vmin, vmax);

    Position pos = find_target_position(frame, width, height);
    if (pos.found) printf("Target found at (%d, %d)\n", pos.x, pos.y);
    else           printf("Target NOT found with current threshold\n");

    free(frame);
    return EXIT_SUCCESS;
}
#endif /* STANDALONE_TEST */
