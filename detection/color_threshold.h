#ifndef COLOR_THRESHOLD_H
#define COLOR_THRESHOLD_H

#include "capture/capture.h"

unsigned char* load_yuv_frame(const char *path, int width, int height);
void get_pixel_yuv(unsigned char *frame, int height, int width, int x, int y,
                   unsigned char *out_y, unsigned char *out_u, unsigned char *out_v);
int is_target_color(unsigned char y, unsigned char u, unsigned char v);
Position find_target_position(unsigned char *frame, int width, int height);
void mark_postion_on_frame(unsigned char *frame, int width, int height, Position pos);

#endif /* COLOR_THRESHOLD_H */