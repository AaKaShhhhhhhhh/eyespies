#include "control_loop.h"
#include <math.h>

float error_to_angle_delta(int target_pixel, int frame_center_pixel, float gain) {
    int pixel_error = target_pixel - frame_center_pixel;
    return pixel_error * gain;


}

float smooth_angle(float previous_angle, float new_angle, float smoothing_factor) {
    return (smoothing_factor * previous_angle) + ((1 - smoothing_factor) * new_angle);
}

float clamp_angle(float angle, float min_angle, float max_angle) {
    if (angle < min_angle) return min_angle;
    if (angle > max_angle) return max_angle;
    return angle;
}

int should_update(float previous_angle, float new_angle, float deadband_degrees) {
    return fabs(new_angle - previous_angle) > deadband_degrees;
}