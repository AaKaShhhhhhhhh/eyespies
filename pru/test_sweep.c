/*
 * test_sweep.c  --  STANDALONE SERVO ISOLATION TEST  (no camera, no AI)
 *
 * WHY THIS EXISTS
 * ---------------
 * The turret (camera + motion detect + control loop) wasn't moving the servo.
 * That could be (a) the PRU firmware / shared-RAM contract, or (b) the turret
 * call path. This test removes the turret from the equation: it drives the
 * servo using ONLY pru_comms.c (the exact same code the turret links).
 *
 * HOW TO READ THE RESULT
 * ----------------------
 *   * Servo SWEEPS (snaps to centre, then moves to 0/45/90/135/180) -> the
 *     firmware, the PROU ABI, and the wiring are ALL good. The turret bug is
 *     then a stale turret binary (rebuild it).
 *   * Servo does NOT move at all -> the firmware/ABI is broken; dig deeper.
 *
 * BUILD ON THE BEAGLEBONE (ARM gcc, NOT pru-gcc):
 *     gcc test_sweep.c pru_comms.c -o test_sweep
 * RUN:
 *     sudo ./test_sweep
 */

#include <stdio.h>
#include <unistd.h>
#include "pru_comms.h"

int main(void) {
    /* Open the shared-RAM whiteboard and write the PROU magic + centre. */
    pru_init();
    printf("pru_init OK -> servo should snap to CENTER (1500 us) right now.\n");
    printf("   (If it does NOT centre now, the loaded firmware is stale.)\n");
    sleep(2);

    /* Sweep PAN through five angles, 1 second each, so you can watch it move. */
    int angles[] = {0, 45, 90, 135, 180, 90};
    for (int i = 0; i < 6; i++) {
        printf(">> PAN = %d deg\n", angles[i]);
        pru_set_angle(PAN, (float)angles[i]);
        sleep(1);
    }

    printf("parking at centre...\n");
    pru_stop();
    return 0;
}
