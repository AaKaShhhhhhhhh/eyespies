#include "capture/v4l2"
#include "detection/color_threshold.h"
#include "websocket/position_sender"

int main() {
    int fd = open_device("/dev/video0");
    if (fd < 0) {   
        perror("Failed to open device");
        return -1;
    }

    set_format(fd, 640, 480);
    int buffer_count = request_buffers(fd, 4);
    if (buffer_count < 0) {
        perror("Failed to request buffers");
        close(fd);
        return -1;
    }

    for (int i = 0; i < buffer_count; i++) {
        map_buffers(fd, i);
    }

    queue_all_buffers(fd, buffer_count);
    start_streaming(fd);

    int sockfd = connect_to_server(BBB_IP , BBB_PORT);

    if (sockfd < 0) {
        perror("Failed to connect to server");
        cleanup(fd, buffer_count);
        return -1;
    }

    capture_loop(fd, buffer_count, sockfd);
     

    cleanup(fd, buffer_count);
    close(sockfd);
    return 0;


}