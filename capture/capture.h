#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include "control/control_loop.h"   /* for AxisState */

typedef struct {
    int x;
    int y;
    int found;
} Position;

int open_device(const char *dev_path);
void query_capabilities(int fd);
void set_format(int fd, int width, int height);
int request_buffers(int fd, int count);
void map_buffers(int fd, int index);
void queue_all_buffers(int fd, int buffers);
void start_streaming(int fd);
void save_to_file(const void *buffer, size_t size);
void capture_loop(int fd, int buffer_count, int width, int height,
                  AxisState *pan, AxisState *tilt,
                  const char *pan_pwm_path, const char *tilt_pwm_path);
void stop_streaming(int fd);
void cleanup(int fd, int buffer_count);

#endif /* CAPTURE_H */