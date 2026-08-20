#include "detection/motion_detect.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * TUNABLES
 * ------------------------------------------------------------------------- */
#define MOTION_THRESHOLD 25     /* brightness delta to count a pixel as "moved" */
#define MOTION_MIN_FRAC  40     /* changed pixels must EXCEED (w*h)/MIN_FRAC.
                                   Higher = needs MORE motion to count (rejects
                                   the per-frame flicker of an auto-exposure
                                   UVC camera, which is what caused jitter).    */
#define MOTION_MAX_FRAC  3      /* ...but if changed pixels EXCEED (w*h)/MAX_FRAC
                                   the WHOLE frame moved (servo/camera panned) ->
                                   ignore it, or we oscillate around centre.     */
#define MOTION_PERSIST   2      /* require motion in this many CONSECUTIVE frames
                                   before reporting it. Random noise flicker
                                   won't sustain; real motion will.            */

/* Previous frame brightness, as a flat w*h array of Y bytes. */
static unsigned char *prev_y = NULL;
static int prev_w = 0, prev_h = 0;
static int persist_count = 0;

/* When we (re)allocate the baseline buffer, the next detection pass should
 * just record the current frame as the baseline instead of reporting motion
 * (otherwise the very first frame would look like "everything moved"). */
static int need_baseline = 0;

static void ensure_prev(int w, int h) {
    if (!prev_y || prev_w != w || prev_h != h) {
        free(prev_y);
        prev_y = (unsigned char *)malloc((size_t)w * (size_t)h);
        prev_w = w;
        prev_h = h;
        need_baseline = 1;
        persist_count = 0;
    }
}

/* Call this when switching INTO motion mode (or changing resolution) so we
 * don't treat the stale previous frame as "motion". */
void motion_reset(void) {
    free(prev_y);
    prev_y = NULL;
    prev_w = prev_h = 0;
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

    ensure_prev(width, height);
    if (!prev_y) return pos;   /* allocation failed */

    /* First frame after (re)alloc: just record baseline, report nothing. */
    if (need_baseline) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                prev_y[(size_t)y * width + x] = y_at(frame, width, x, y);
        need_baseline = 0;
        persist_count = 0;
        return pos;            /* found = 0 */
    }

    long sum_x = 0, sum_y = 0, count = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char cur = y_at(frame, width, x, y);
            int idx = (size_t)y * width + x;
            int diff = cur - prev_y[idx];
            if (diff < 0) diff = -diff;
            if (diff > MOTION_THRESHOLD) {
                sum_x += x;
                sum_y += y;
                count++;
            }
            prev_y[idx] = cur;   /* update baseline for next frame */
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
        /* else: motion this frame but not yet persistent -> hold last position */
    } else {
        persist_count = 0;     /* noise / no motion -> reset */
    }
    return pos;
}
