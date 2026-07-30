#ifndef POSITION_RECIEVER_H
#define POSITION_RECIEVER_H

#include "../control/control_loop.h"

int create_server_socket(int port);
int accept_client(int server_fd);
void receive_loop(int client_fd, AxisState *pan, AxisState *tilt,
                   const char *pan_pwm_path, const char *tilt_pwm_path);

#endif /* POSITION_RECIEVER_H */