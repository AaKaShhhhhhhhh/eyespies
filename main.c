#include "capture/capture.h"
#include "detection/motion_detect.h"
#include "control/control_loop.h"
/* We now drive the servos through the PRU. pru_comms.h gives us pru_init(),
   pru_set_angle(axis, angle) and pru_stop(). The PRU firmware (pru_servo.out)
   must already be loaded by load_pru.sh before turret runs. */
#include "pru/pru_comms.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>


#define WIDTH 320
#define HEIGHT 240

int main(int argc, char *argv[]) {

    const char *dev = (argc > 1) ? argv[1] : NULL;
    int fd = find_capture_device(dev);
    if(fd < 0 ){ fprintf(stderr, "open device: no capture device\n"); return 1;}
    motion_reset();   /* baseline the first frame so we don't report phantom motion */

    set_format(fd , WIDTH , HEIGHT);
    int n = request_buffers( fd , 4);
    if( n< 0 ) {close(fd); return 1;}

    for(int i = 0 ; i < n ; i++) map_buffers(fd , i);
    queue_all_buffers(fd , n);
    start_streaming(fd);


    /* Create two servo "state" structs, both starting at 90 degrees = centre
       (safe starting point so the turret doesn't jerk on power-up). */
    AxisState pan = { .current_angle = 90};
    AxisState tilt = { .current_angle = 90};

    /* Open the PRU shared-RAM whiteboard ONCE (mmap /dev/mem at 0x4A310000 and
       write the "PROU" magic). This must happen before any pru_set_angle().
       If the PRU firmware isn't loaded, this will fail and exit. */
    if (pru_init() != 0) {
        fprintf(stderr, "pru_init failed -- is the PRU firmware loaded?\n");
        cleanup(fd, n);
        return 1;
    }

    /* THE MAIN JOB: capture camera frames forever, detect motion, and move the
       servos to keep the target centred. We hand capture_loop the function
       pru_set_angle so it can write PAN/TILT angles to the whiteboard. */
    capture_loop(fd , n , WIDTH , HEIGHT , &pan , &tilt , pru_set_angle);

    pru_stop();          /* park both servos at centre on exit */
    cleanup(fd , n);
    return 0;

}
