#ifndef PWM_SERVO_H
#define PWM_SERVO_H

void pwm_set_period_ns(const char *pwm_path , unsigned long period_ns);
void pwm_set_duty_cycle_ns(const char *pwm_path , unsigned long duty_cycle_ns);
void pwm_enable(const char *pwm_path , int enable);
unsigned long angle_to_duty_ns(float angle_degrees);
void servo_set_angle(const char *pwm_path , float angle_degrees);

#endif /* PWM_SERVO_H */