#ifndef PRU_COMMS_H
#define PRU_COMMS_H

#include <stdint.h>

/*
 * pru_comms.h  --  the contract for the LINUX side of the whiteboard.
 *
 * This header MUST be kept in sync with the struct inside pru_servo.c on the
 * PRU side. Same field order, same magic value. If you change one, change both.
 *
 *   magic  (offset 0)  = password 0x50524F55 ("PROU")
 *   pan_us (offset 4)  = PAN pulse width, microseconds
 *   tilt_us(offset 8)  = TILT pulse width, microseconds
 *   seq    (offset 12) = bumped by 1 every time Linux writes new angles
 *   flags  (offset 16) = spare (firmware does not read it yet)
 */

/* Which servo axis we are talking to.
 * Order matters: PAN must stay 0 and TILT 1 because pru_comms.c writes
 * pan_us into whiteboard slot [1] and tilt_us into slot [2]. */
typedef enum {
    PAN  = 0,
    TILT = 1
} pru_axis_t;

/* Open the shared-RAM whiteboard: mmap /dev/mem at 0x4A310000 and write the
 * "PROU" magic password. Call ONCE, before any pru_set_angle().
 * Returns 0 on success, -1 on failure. */
int pru_init(void);

/* Convert a human angle (0..180 degrees) to a pulse width (500..2500 us),
 * write it into the whiteboard, and bump seq so the PRU acts on it. */
void pru_set_angle(pru_axis_t axis, float angle_degrees);

/* Park both servos at centre. The current firmware ignores the STOP flag, so
 * we park by writing 1500 us (centre) for both axes and bumping seq. */
void pru_stop(void);

#endif /* PRU_COMMS_H */
