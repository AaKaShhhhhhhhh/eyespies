#include "detection/motion_detect.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * TUNABLES
 * ------------------------------------------------------------------------- */
#define MOTION_THRESHOLD 30     /* per-pixel brightness delta to count as "moved" */
#define MOTION_MIN_FRAC  150    /* changed pixels must EXCEED (w*h)/MIN_FRAC to
                                   report motion. Higher bar = ignores the faint
                                   per-frame flicker of an auto-exposure UVC
                                   camera (the jitter cause). ~0.7% of frame.   */
#define MOTION_MAX_FRAC  3      /* if changed pixels EXCEED (w*h)/MAX_FRAC the
                                   WHOLE frame moved (servo/camera panned) ->
                                   ignore, or we oscillate around centre.       */
#define MOTION_PERSIST   2      /* require motion in this many CONSECUTIVE frames
                                   before reporting. Random noise won't sustain;
                                   real motion will.                            */

/* Background model: a slowly-adapting per-pixel brightness average.
 * KEY TRICK (running average with a motion veto):
 *   - Normally baseline follows the scene: baseline = baseline*0.9 + frame*0.1
 *   - BUT if a pixel changed a lot, we DO NOT update its baseline. A moving
 *     object therefore never "poisons" the background, and once it leaves, the
 *     background is still correct. This is why we do NOT just copy the previous
 *     frame every time (that copied frame-to-frame flicker and caused jitter). */
static unsigned char *bg = NULL;
static int bg_w = 0, bg_h = 0;
static int persist_count = 0;
static int need_baseline = 0;

static void ensure_bg(int w, int h) {
    if (!bg || bg_w != w || bg_h != h) {
        free(bg);
        bg = (unsigned char *)malloc((size_t)w * (size_t)h);
        bg_w = w;
        bg_h = h;
        need_baseline = 1;
        persist_count = 0;
    }
}

void motion_reset(void) {
    free(bg);
    bg = NULL;
    bg_w = bg_h = 0;
    need_baseline = 0;
    persist_count = 0;
}

/* Read the Y (brightness) byte for pixel (x,y) from a packed YUYV buffer.
 * Layout per pixel pair (x even, x+1): [Y0, U, Y1, V] -> each pixel is 2 bytes. */
static inline unsigned char y_at(const unsigned char *frame, int w, int x, int y) {
    return frame[(size_t)y * (size_t)w * 2 + (size_t)x * 2];
}

Position find_motion_position(unsigned char *frame, int width, int height) {
    Position pos = {0, 0, 0};

    ensure_bg(width, height);
    if (!bg) return pos;

    /* First frame after (re)alloc: seed the background, report nothing. */
    if (need_baseline) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                bg[(size_t)y * width + x] = y_at(frame, width, x, y);
        need_baseline = 0;
        persist_count = 0;
        return pos;
    }

    long sum_x = 0, sum_y = 0, count = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (size_t)y * width + x;
            unsigned char cur = y_at(frame, width, x, y);
            int diff = (int)cur - (int)bg[idx];
            if (diff < 0) diff = -diff;

            if (diff > MOTION_THRESHOLD) {
                sum_x += x;
                sum_y += y;
                count++;
                /* motion veto: do NOT let this pixel pull the background.
                 * This is what stops a moving object (and camera flicker that
                 * looks like motion) from rewriting the reference frame. */
            } else {
                /* quiet pixel: let background adapt slowly to lighting drift */
                int b = bg[idx];
                b = (b * 9 + cur) / 10;
                bg[idx] = (unsigned char)b;
            }
        }
    }

    long min_px = ((long)width * (long)height) / MOTION_MIN_FRAC;
    long max_px = ((long)width * (long)height) / MOTION_MAX_FRAC;

    if (count > max_px) {
        persist_count = 0;     /* whole-frame change = camera moved; ignore */
        return pos;
    }
    if (count > min_px) {
        persist_count++;
        if (persist_count >= MOTION_PERSIST) {
            pos.x = (int)(sum_x / count);
            pos.y = (int)(sum_y / count);
            pos.found = 1;
        }
    } else {
        persist_count = 0;     /* noise / no motion -> reset */
    }
    return pos;
}
