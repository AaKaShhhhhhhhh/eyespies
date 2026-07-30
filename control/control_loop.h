#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

typedef struct {
    float current_angle;
}AxisState;

float error_to_angle_delta(int target_pixel , int frame_center_pixel , float gain);
float smooth_angle(float previous_angle , float new_angle , float smoothing_factor);
float clamp_angle(float angle , float min_angle , float max_angle);
int should_update(float previous_angle , float new_angle , float deadband_degrees);

#endif // CONTROL_LOOP_H