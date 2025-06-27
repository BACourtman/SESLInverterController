// pwm_control.c
// This file contains the PWM control functions

#include "pwm_control.h"
#include "phase_pwm.pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"  // Add this include for sleep_ms()
#include <stdio.h>

const uint PWM_PINS[4] = {2, 3, 4, 5};
const uint TRIGGER_PIN = 6;

static PIO pio = NULL;
static uint offset = 0;
static float current_frequency = 0;
static float current_duty_cycle = 0;
static bool pio_debug_mode = false;
static bool manual_pio_trigger_state = false;

void pwm_control_init(float frequency, float duty_cycle) {
    current_frequency = frequency;
    current_duty_cycle = duty_cycle;
    
    // Enable PIO Program
    pio = pio0;
    offset = pio_add_program(pio, &phase_pwm_program);

    // Initialize trigger pin as output and set low
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_IN);
    gpio_put(TRIGGER_PIN, 0);

    // Initialize each state machine for each phase
    for (int i = 0; i < 4; ++i) {
        phase_pwm_program_init(pio, i, offset, PWM_PINS[i], TRIGGER_PIN);
    }

    update_pwm_parameters(frequency, duty_cycle);
    
    printf("Loaded PIO program at %d\n", offset);
}

void update_pwm_parameters(float frequency, float duty_cycle) {
    float period_s = 1.0f / frequency;
    float clk_freq = 125000000.0f; // Pico default clock
    float phase_step = period_s / 4.0f;
    
    // After initializing each state machine for each phase:
    for (int i = 0; i < 4; ++i) {
        float phase_offset = i * phase_step;
        uint32_t phase_delay = (uint32_t)(phase_offset * clk_freq);
        uint32_t high_time = (uint32_t)(period_s * duty_cycle * clk_freq);
        uint32_t low_time = (uint32_t)(period_s * (1.0f - duty_cycle) * clk_freq);

        pio_sm_put_blocking(pio, i, phase_delay);
        pio_sm_put_blocking(pio, i, high_time);
        pio_sm_put_blocking(pio, i, low_time);

        pio_sm_set_enabled(pio, i, true); // Enable the SM so it waits for the first trigger
    }
    
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 1);
    sleep_ms(1);
    gpio_put(TRIGGER_PIN, 0);
    gpio_set_dir(TRIGGER_PIN, GPIO_IN); // Restore to input for normal operation
    
    // Disable all SMs after the test pulse
    for (int i = 0; i < 4; ++i) {
        pio_sm_set_enabled(pio, i, false);
    }
    
    current_frequency = frequency;
    current_duty_cycle = duty_cycle;
}

void process_pio_state_machines(PIO pio, float frequency, float duty_cycle) {
    float period_s = 1.0f / frequency;
    float clk_freq = 125000000.0f;
    float phase_step = period_s / 4.0f;
    
    for (int phase = 0; phase < 4; ++phase) {
        if (!pio_sm_is_rx_fifo_empty(pio, phase)) {
            // State machine has signaled "done" and is halted
            uint32_t done = pio_sm_get_blocking(pio, phase); // Clear the RX FIFO

            // Optionally, disable the SM (it should already be halted, but this is safe)
            pio_sm_set_enabled(pio, phase, false);

            // Prepare new timing parameters
            float phase_offset = phase * phase_step;
            uint32_t phase_delay = (uint32_t)(phase_offset * clk_freq);
            uint32_t high_time = (uint32_t)(period_s * duty_cycle * clk_freq);
            uint32_t low_time = (uint32_t)(period_s * (1.0f - duty_cycle) * clk_freq);

            // Push new parameters
            pio_sm_put_blocking(pio, phase, phase_delay);
            pio_sm_put_blocking(pio, phase, high_time);
            pio_sm_put_blocking(pio, phase, low_time);

            // Re-enable the SM so it waits for the next trigger
            pio_sm_set_enabled(pio, phase, true);
            printf("Updated State Machine for Phase %d: Delay = %u, High Time = %u, Low Time = %u\n", 
                   phase, phase_delay, high_time, low_time);
        }
    }
}

void set_pio_debug_mode(bool enable) {
    pio_debug_mode = enable;
    if (enable) {
        printf("PIO Debug mode ENABLED - Manual trigger control active\n");
        printf("Hardware PIO trigger pin is now IGNORED\n");
    } else {
        printf("PIO Debug mode DISABLED - Hardware trigger pin active\n");
        manual_pio_trigger_state = false;
    }
}

void set_manual_pio_trigger(bool state) {
    if (!pio_debug_mode) {
        printf("Error: PIO debug mode not enabled. Use PIO_DEBUG 1 first.\n");
        return;
    }
    
    manual_pio_trigger_state = state;
    printf("Manual PIO trigger set to %s\n", state ? "ACTIVE" : "INACTIVE");
}

bool get_effective_pio_trigger_state(void) {
    if (pio_debug_mode) {
        return manual_pio_trigger_state;
    } else {
        // Read your actual PIO trigger pin here
        // Replace with your actual trigger pin number
        return gpio_get(TRIGGER_PIN); // Adjust based on your setup
    }
}

void print_pio_trigger_status(void) {
    bool hw_trigger = gpio_get(TRIGGER_PIN); // Replace with actual pin
    bool effective_trigger = get_effective_pio_trigger_state();
    
    printf("PIO Trigger Status:\n");
    printf("  Debug Mode: %s\n", pio_debug_mode ? "ENABLED" : "DISABLED");
    printf("  Hardware Pin: %s\n", hw_trigger ? "HIGH" : "LOW");
    
    if (pio_debug_mode) {
        printf("  Manual Trigger: %s\n", manual_pio_trigger_state ? "ACTIVE" : "INACTIVE");
    }
    
    printf("  Effective Trigger: %s\n", effective_trigger ? "ACTIVE" : "INACTIVE");
}

// Update your existing PIO control functions to use get_effective_pio_trigger_state()
// instead of directly reading the hardware pin
