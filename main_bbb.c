#include "control/control_loop.h"
#include "pmw/pmw_servo.h"
#include "websocket/position_reciever.h"

#include <stdio.h>
#include <unistd.h>

#define PAN_PWM_PATH  "/sys/class/pwm/pwmchip0/pwm0"
#define TILT_PWM_PATH "/sys/class/pwm/pwmchip0/pwm1"

#define BBB_PORT 8080

int main(void) {
    pwm_set_period_ns(PAN_PWM_PATH, 20000000);
    pwm_set_period_ns(TILT_PWM_PATH, 20000000);
    pwm_enable(PAN_PWM_PATH, 1);
    pwm_enable(TILT_PWM_PATH, 1);

    AxisState pan_state = { .current_angle = 90 };
    AxisState tilt_state = { .current_angle = 90 };

    int server_fd = create_server_socket(BBB_PORT);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        return 1;
    }

    int client_fd = accept_client(server_fd);
    if (client_fd < 0) {
        fprintf(stderr, "Failed to accept client\n");
        close(server_fd);
        return 1;
    }

    receive_loop(client_fd, &pan_state, &tilt_state, PAN_PWM_PATH, TILT_PWM_PATH);

    close(client_fd);
    close(server_fd);
    return 0;
}
