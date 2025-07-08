#ifndef PWM_CONTROL_H
#define PWM_CONTROL_H

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

extern const uint PWM_PINS[4];
extern const uint TRIGGER_PIN;

void pwm_control_init(float frequency, float duty_cycle);
void update_pwm_parameters(float frequency, float duty_cycle);
void set_pio_debug_mode(bool enable);
void set_manual_pio_trigger(bool state);
bool get_effective_pio_trigger_state(void);
void print_pio_trigger_status(void);
void debug_pio_state_machines(void);
#endif