#include "pmw_servo.h"
#include <stdio.h>

void pwm_set_period_ns(const char *pwm_path , unsigned long period_ns) {
    char period_path[256];
    snprintf(period_path, sizeof(period_path), "%s/period", pwm_path);
    FILE *f = fopen(period_path, "w");
    if (f) {
        fprintf(f, "%lu", period_ns);
        fclose(f);
    } else {
        perror("Failed to set PWM period");
    }
}

void pwm_set_duty_cycle_ns(const char *pwm_path , unsigned long duty_cycle_ns) {
    char duty_cycle_path[256];
    snprintf(duty_cycle_path, sizeof(duty_cycle_path), "%s/duty_cycle", pwm_path);
    FILE *f = fopen(duty_cycle_path, "w");
    if (f) {
        fprintf(f, "%lu", duty_cycle_ns);
        fclose(f);
    } else {
        perror("Failed to set PWM duty cycle");
    }
}

void pwm_enable(const char *pwm_path , int enable) {
    char enable_path[256];
    snprintf(enable_path, sizeof(enable_path), "%s/enable", pwm_path);
    FILE *f = fopen(enable_path, "w");
    if (f) {
        fprintf(f, "%d", enable);
        fclose(f);
    } else {
        perror("Failed to enable/disable PWM");
    }
}

unsigned long angle_to_duty_ns(float angle_degrees) {
    // Assuming 0 degrees corresponds to 0.5ms and 180 degrees corresponds to 2.5ms
    float duty_cycle_ms = 0.5f + (angle_degrees / 180.0f) * 2.0f;
    return (unsigned long)(duty_cycle_ms * 1000000); // Convert ms to ns
}

void servo_set_angle(const char *pwm_path , float angle_degrees) {
    unsigned long duty_cycle_ns = angle_to_duty_ns(angle_degrees);
    pwm_set_duty_cycle_ns(pwm_path, duty_cycle_ns);
}

int main() {
    const char *pwm_path = "/sys/class/pwm/pwmchip0/pwm0"; // Adjust as necessary
    pwm_set_period_ns(pwm_path, 20000000); // 20ms period for standard servo
    pwm_enable(pwm_path, 1); // Enable PWM

    // Example: Set servo to 90 degrees
    servo_set_angle(pwm_path, 90.0f);

    return 0;
}