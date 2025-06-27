// pwm_control.c
// This file contains the PWM control functions.

#include "pwm_control.h"
#include "phase_pwm.pio.h"

// Initialize all PWM state machines for each phase
void pwm_init_all(PIO pio, uint offset, const uint *pins, uint trigger_pin, int num_phases) {
    for (int i = 0; i < num_phases; ++i) {
        phase_pwm_program_init(pio, i, offset, pins[i], trigger_pin);
    }
}

// Update PWM parameters for a given phase
void pwm_update_params(PIO pio, int phase, float phase_offset, float clk_freq, float period_s, float duty_cycle) {
    uint32_t phase_delay = (uint32_t)(phase_offset * clk_freq);
    uint32_t high_time = (uint32_t)(period_s * duty_cycle * clk_freq);
    uint32_t low_time = (uint32_t)(period_s * (1.0f - duty_cycle) * clk_freq);
    pio_sm_put_blocking(pio, phase, phase_delay);
    pio_sm_put_blocking(pio, phase, high_time);
    pio_sm_put_blocking(pio, phase, low_time);
}

// Enable or disable a PWM state machine
void pwm_enable(PIO pio, int phase, bool enable) {
    pio_sm_set_enabled(pio, phase, enable);
}