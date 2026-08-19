#include "capture/capture.h"
#include "detection/color_threshold.h"
#include "control/control_loop.h"
#include "pmw/pmw_servo.h"

#include <stdio.h>
#include <unistd.h>


#define TILT_PWM_PATH  "/sys/class/pwm/pwmchip1/pwm1"
#define PAN_PWM_PATH   "/sys/class/pwm/pwmchip1/pwm0"

#define WIDTH 320
#define HEIGHT 240

int main() {

    int fd = open_device("/dev/video0");
    if(fd < 0 ){ perror("open device"); return 1;}

    set_format(fd , WIDTH , HEIGHT);
    int n = request_buffers( fd , 4);
    if( n< 0 ) {close(fd); return 1;}

    for(int i = 0 ; i < n ; i++) map_buffers(fd , i);
    queue_all_buffers(fd , n);
    start_streaming(fd);


    AxisState pan = { .current_angle = 90};
    AxisState tilt = { .current_angle = 90};
    pwm_set_period_ns(PAN_PWM_PATH , 20000000);
    pwm_set_period_ns(TILT_PWM_PATH , 20000000);

    pwm_enable(PAN_PWM_PATH , 1);
    pwm_enable(TILT_PWM_PATH , 1);

    capture_loop(fd , n , WIDTH , HEIGHT , &pan , &tilt , PAN_PWM_PATH , TILT_PWM_PATH );

    cleanup(fd , n);
    return 0;

}
