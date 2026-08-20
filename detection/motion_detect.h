#ifndef MOTION_DETECT_H
#define MOTION_DETECT_H

#include "capture/capture.h"   /* Position */

/* ---------------------------------------------------------------------------
 * Motion detection (frame differencing) on the Y (luma) channel of YUYV.
 *
 * The camera already gives us YUYV: 2 bytes per pixel, byte 0 = Y (brightness)
 * for even pixels, byte 2 = Y for odd pixels. So we can read brightness WITHOUT
 * any color conversion or extra library. We keep the PREVIOUS frame's Y values
 * and, for each new frame, count pixels whose brightness changed by more than
 * MOTION_THRESHOLD. The centroid of those changed pixels = "where something
 * moved" = where you are. Returns the same Position struct the control loop
 * already consumes, so nothing downstream changes.
 *
 * Tunables (tweak on the board if it's too jumpy or too deaf):
 *   MOTION_THRESHOLD  - per-pixel brightness change to count as "moved" (0..255)
 *   MOTION_MIN_PIXELS - minimum changed pixels before we say "motion found"
 *                       (scaled to resolution below)
 * ------------------------------------------------------------------------- */
void motion_reset(void);
Position find_motion_position(unsigned char *frame, int width, int height);

#endif /* MOTION_DETECT_H */
