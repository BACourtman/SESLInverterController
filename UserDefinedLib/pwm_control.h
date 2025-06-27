#pragma once
#include "hardware/pio.h"
#include <stdbool.h>

// Initialize all PWM state machines for each phase
void pwm_init_all(PIO pio, uint offset, const uint *pins, uint trigger_pin, int num_phases);

// Update PWM parameters for a given phase
void pwm_update_params(PIO pio, int phase, float phase_offset, float clk_freq, float period_s, float duty_cycle);

// Enable or disable a PWM state machine
void pwm_enable(PIO pio, int phase, bool enable);