// pwm_control.c
// This file contains the PWM control functions

#include "pwm_control.h"
#include "phase_pwm.pio.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"  // Add this include for clock_get_hz()
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

    // Initialize trigger pin as input with pulldown (shared by all SMs)
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_IN);
    gpio_pull_down(TRIGGER_PIN);

    // Initialize each state machine for each phase
    for (int i = 0; i < 4; ++i) {
        // Each SM controls its own output pin (2,3,4,5) but shares trigger pin (6)
        phase_pwm_program_init(pio, i, offset, PWM_PINS[i], TRIGGER_PIN);
        
        // IMPORTANT: Enable the state machine after initialization
        pio_sm_set_enabled(pio, i, true);
    }

    update_pwm_parameters(frequency, duty_cycle);
    
    printf("[INFO] Loaded PIO program at %d\n", offset);
    printf("[INFO] 4 State machines configured and ENABLED:\n");
    printf("[INFO]   SM0 -> Pin %d (trigger: Pin %d)\n", PWM_PINS[0], TRIGGER_PIN);
    printf("[INFO]   SM1 -> Pin %d (trigger: Pin %d)\n", PWM_PINS[1], TRIGGER_PIN);
    printf("[INFO]   SM2 -> Pin %d (trigger: Pin %d)\n", PWM_PINS[2], TRIGGER_PIN);
    printf("[INFO]   SM3 -> Pin %d (trigger: Pin %d)\n", PWM_PINS[3], TRIGGER_PIN);
}

void update_pwm_parameters(float frequency, float duty_cycle) {
    // QUICK FIX: Double the frequency to compensate for PIO overhead
    float adjusted_frequency = frequency * 2.0f;
    float period_s = 1.0f / adjusted_frequency;
    
    // Get actual system clock frequency instead of assuming 125MHz
    uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    float clk_freq = (float)sys_clk_hz;
    
    // Calculate actual high and low times within the period
    float high_time_s = period_s * duty_cycle;
    float low_time_s = period_s * (1.0f - duty_cycle);
    
    // Phase shift: 90 degrees = 1/4 of ORIGINAL period (not adjusted period)
    float original_period_s = 1.0f / frequency;  // Use original frequency for phase shift
    float phase_shift_s = original_period_s / 4.0f;
    
    // Debug output - THIS IS CRUCIAL
    printf("[DEBUG] ===== PWM PARAMETER CALCULATION =====\n");
    printf("[DEBUG] System clock frequency: %.0f Hz\n", clk_freq);
    printf("[DEBUG] Input frequency: %.2f Hz\n", frequency);
    printf("[DEBUG] Adjusted frequency (2x): %.2f Hz\n", adjusted_frequency);
    printf("[DEBUG] Input duty cycle: %.2f\n", duty_cycle);
    printf("[DEBUG] Original period: %.6f s (%.2f μs)\n", original_period_s, original_period_s * 1000000.0f);
    printf("[DEBUG] Adjusted period: %.6f s (%.2f μs)\n", period_s, period_s * 1000000.0f);
    printf("[DEBUG] High time: %.6f s (%.2f μs)\n", high_time_s, high_time_s * 1000000.0f);
    printf("[DEBUG] Low time: %.6f s (%.2f μs)\n", low_time_s, low_time_s * 1000000.0f);
    printf("[DEBUG] Phase shift: %.6f s (%.2f μs)\n", phase_shift_s, phase_shift_s * 1000000.0f);
    
    // Clear FIFOs (SMs stay enabled and running)
    for (int i = 0; i < 4; ++i) {
        pio_sm_clear_fifos(pio, i);
    }
    
    // Configure each state machine for each phase
    for (int i = 0; i < 4; ++i) {
        float phase_delay_s = i * phase_shift_s;
        uint32_t phase_delay = (uint32_t)(phase_delay_s * clk_freq);
        uint32_t high_time = (uint32_t)(high_time_s * clk_freq);
        uint32_t low_time = (uint32_t)(low_time_s * clk_freq);

        // Make sure delays are not zero (minimum 1 clock cycle)
        if (phase_delay == 0 && i > 0) phase_delay = 1;
        if (high_time == 0) high_time = 1;
        if (low_time == 0) low_time = 1;

        printf("[DEBUG] SM%d: phase_delay=%lu cycles (%.2f μs), high_time=%lu cycles (%.2f μs), low_time=%lu cycles (%.2f μs)\n", 
               i, phase_delay, (float)phase_delay / clk_freq * 1000000.0f,
               high_time, (float)high_time / clk_freq * 1000000.0f,
               low_time, (float)low_time / clk_freq * 1000000.0f);

        // Calculate actual output frequency (only high + low time matters)
        uint32_t pulse_period_cycles = high_time + low_time;
        float actual_output_freq = clk_freq / (float)pulse_period_cycles;
        printf("[DEBUG] SM%d: pulse_period=%lu cycles, calculated_freq=%.2f Hz, expected_output=%.2f Hz\n", 
               i, pulse_period_cycles, actual_output_freq, frequency);

        // Fill FIFO with fresh parameters (SMs are continuously running)
        pio_sm_put_blocking(pio, i, phase_delay);
        pio_sm_put_blocking(pio, i, high_time);
        pio_sm_put_blocking(pio, i, low_time);
    }
    
    current_frequency = frequency;
    current_duty_cycle = duty_cycle;
    
    printf("[INFO] PWM parameters updated: %.2f Hz, %.2f%% duty\n", frequency, duty_cycle * 100);
    printf("[INFO] FIFOs filled with fresh parameters\n");
    printf("[INFO] State machines are RUNNING and waiting for triggers\n");
}

void set_manual_pio_trigger(bool state) {
    if (!pio_debug_mode) {
        printf("[ERROR] PIO debug mode not enabled. Use PIO_DEBUG 1 first.\n");
        return;
    }
    
    // If turning trigger ON, check if FIFOs have data
    if (state) {
        bool any_fifo_empty = false;
        for (int i = 0; i < 4; ++i) {
            if (pio_sm_is_tx_fifo_empty(pio, i)) {
                any_fifo_empty = true;
                break;
            }
        }
        
        if (any_fifo_empty) {
            printf("[ERROR] One or more PIO FIFOs are empty!\n");
            printf("[ERROR] Use FREQUENCY command to refill FIFOs before triggering.\n");
            printf("[ERROR] Example: FREQUENCY 100000\n");
            return;
        }
    }
    
    printf("[DEBUG] Setting trigger pin %d to %s\n", TRIGGER_PIN, state ? "HIGH" : "LOW");
    
    // Make sure pin is configured as output
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    
    // Set the pin state
    gpio_put(TRIGGER_PIN, state ? 1 : 0);
    
    // Verify the pin state
    sleep_ms(1);  // Small delay to ensure pin is set
    bool actual_state = gpio_get(TRIGGER_PIN);
    
    manual_pio_trigger_state = state;
    printf("[COMMAND] Manual PIO trigger set to %s (GPIO %d = %s)\n", 
           state ? "ACTIVE" : "INACTIVE", 
           TRIGGER_PIN,
           actual_state ? "HIGH" : "LOW");
           
    if (state != actual_state) {
        printf("[ERROR] Pin state mismatch! Expected %s, got %s\n",
               state ? "HIGH" : "LOW", actual_state ? "HIGH" : "LOW");
    }
}

void set_pio_debug_mode(bool enable) {
    printf("[DEBUG] Setting PIO debug mode to %s\n", enable ? "ON" : "OFF");
    
    pio_debug_mode = enable;
    
    if (enable) {
        printf("[DEBUG] PIO Debug mode ENABLED - Manual trigger control active\n");
        printf("[DEBUG] Configuring trigger pin %d as output\n", TRIGGER_PIN);
        
        // Configure trigger pin as output for manual control
        gpio_init(TRIGGER_PIN);
        gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
        gpio_put(TRIGGER_PIN, 0);  // Start with trigger low
        
        // Verify configuration
        sleep_ms(1);
        bool pin_state = gpio_get(TRIGGER_PIN);
        printf("[DEBUG] Trigger pin initialized to %s\n", pin_state ? "HIGH" : "LOW");
        
        manual_pio_trigger_state = false;
    } else {
        printf("[DEBUG] PIO Debug mode DISABLED - Hardware trigger pin active\n");
        printf("[DEBUG] Configuring trigger pin %d as input\n", TRIGGER_PIN);
        
        // Restore trigger pin to input for hardware control
        gpio_init(TRIGGER_PIN);
        gpio_set_dir(TRIGGER_PIN, GPIO_IN);
        gpio_pull_down(TRIGGER_PIN);
        
        manual_pio_trigger_state = false;
    }
}

bool get_effective_pio_trigger_state(void) {
    if (pio_debug_mode) {
        return manual_pio_trigger_state;
    } else {
        return gpio_get(TRIGGER_PIN);
    }
}

void print_pio_trigger_status(void) {
    bool hw_trigger = gpio_get(TRIGGER_PIN);
    bool effective_trigger = get_effective_pio_trigger_state();
    
    printf("[INFO] PIO Trigger Status:\n");
    printf("  Hardware trigger (GPIO %d): %s\n", TRIGGER_PIN, hw_trigger ? "HIGH" : "LOW");
    printf("  Debug mode: %s\n", pio_debug_mode ? "ON" : "OFF");
    if (pio_debug_mode) {
        printf("  Manual trigger: %s\n", manual_pio_trigger_state ? "ACTIVE" : "INACTIVE");
    }
    printf("  Effective trigger: %s\n", effective_trigger ? "ACTIVE" : "INACTIVE");
}

void debug_pio_state_machines(void) {
    printf("[DEBUG] PIO State Machine Status:\n");
    for (int i = 0; i < 4; ++i) {
        bool rx_empty = pio_sm_is_rx_fifo_empty(pio, i);
        bool tx_full = pio_sm_is_tx_fifo_full(pio, i);
        
        printf("  SM%d: RX_empty=%s, TX_full=%s\n", 
               i, rx_empty ? "YES" : "NO", 
               tx_full ? "YES" : "NO");
    }
    printf("  Trigger Pin %d: %s\n", TRIGGER_PIN, gpio_get(TRIGGER_PIN) ? "HIGH" : "LOW");
}

