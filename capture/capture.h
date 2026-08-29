#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include "control/control_loop.h"   /* for AxisState */
#include "pru/pru_comms.h"          /* for pru_axis_t (PAN/TILT) + pru_set_angle */

typedef struct {
    int x;
    int y;
    int found;
} Position;

int open_device(const char *dev_path);
int find_capture_device(const char *preferred);
void query_capabilities(int fd);
void set_format(int fd, int width, int height);
int request_buffers(int fd, int count);
void map_buffers(int fd, int index);
void queue_all_buffers(int fd, int buffers);
void start_streaming(int fd);
void save_to_file(const void *buffer, size_t size);
/* The loop is told HOW to move a servo via a function pointer. Today that is
   pru_set_angle(axis, angle_degrees) (writes to the PRU whiteboard). Any
   backend with that signature would work, which is why it's a pointer. */
void capture_loop(int fd, int buffer_count, int width, int height,
                  AxisState *pan, AxisState *tilt,
                  void (*pru_set_angle)(pru_axis_t axis, float angle_degrees));
void stop_streaming(int fd);
void cleanup(int fd, int buffer_count);

#endif /* CAPTURE_H */