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

static inline double absolute(double x) { 
    return x < 0.0 ? -x : x; 
}

static inline uint32_t round_to_uint(double x) { 
    return (uint32_t)(x + 0.5); 
}

// Helper to find best timing parameters for target frequency
static void compute_best_timing(float target_freq, 
                              uint32_t *out_total_cycles,
                              float *out_clkdiv) {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint32_t MAX_CYCLES = 65535;  // Maximum reasonable cycle count
    const double MIN_DIV = 1.0;         // Minimum clock divider
    const double MAX_DIV = 256.0;       // Maximum clock divider
    
    double best_err = 1e12;
    uint32_t best_cycles = 0;
    double best_div = 1.0;

    // Try different cycle counts, preferring larger values for better resolution
    for (uint32_t cycles = MAX_CYCLES; cycles >= 100; cycles--) {
        // Calculate required divider for this cycle count
        double div = (double)sys_hz / ((double)target_freq * (double)cycles);
        
        if (div >= MIN_DIV && div <= MAX_DIV) {
            // Calculate actual frequency with these parameters
            double actual = (double)sys_hz / (div * (double)cycles);
            double err = absolute(actual - target_freq);
            
            if (err < best_err) {
                best_err = err;
                best_cycles = cycles;
                best_div = div;
            }
        }
    }

    *out_total_cycles = best_cycles;
    *out_clkdiv = (float)best_div;
}

void pwm_control_init(float frequency, float duty_cycle_pair1, float duty_cycle_pair2) {
    current_frequency = frequency;
    current_duty_cycle = duty_cycle_pair1;  // Store first duty cycle for compatibility
    
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

    update_pwm_parameters(frequency, duty_cycle_pair1, duty_cycle_pair2);
    
    printf("[INFO] Loaded PIO program at %d\n", offset);
    printf("[INFO] 4 State machines configured and ENABLED:\n");
    printf("[INFO]   SM0 -> Pin %d (trigger: Pin %d) - Pair 1\n", PWM_PINS[0], TRIGGER_PIN);
    printf("[INFO]   SM1 -> Pin %d (trigger: Pin %d) - Pair 2\n", PWM_PINS[1], TRIGGER_PIN);
    printf("[INFO]   SM2 -> Pin %d (trigger: Pin %d) - Pair 1\n", PWM_PINS[2], TRIGGER_PIN);
    printf("[INFO]   SM3 -> Pin %d (trigger: Pin %d) - Pair 2\n", PWM_PINS[3], TRIGGER_PIN);
}

void update_pwm_parameters(float frequency, float duty_cycle_pair1, float duty_cycle_pair2) {
    uint32_t total_cycles;
    float clkdiv;
    compute_best_timing(frequency * 2.0f, &total_cycles, &clkdiv); // 2x for PIO overhead

    const uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    const float effective_freq = (float)sys_clk_hz / (clkdiv * (float)total_cycles);
    
    printf("[DEBUG] ===== PWM PARAMETER CALCULATION =====\n");
    printf("[DEBUG] Target frequency: %.2f Hz\n", frequency);
    printf("[DEBUG] System clock: %lu Hz\n", sys_clk_hz);
    printf("[DEBUG] Chosen parameters: cycles=%lu, clkdiv=%.6f\n", 
           (unsigned long)total_cycles, clkdiv);
    printf("[DEBUG] Effective frequency: %.2f Hz\n", effective_freq);
    
    // Clear FIFOs and update clock dividers
    for (int i = 0; i < 4; ++i) {
        pio_sm_clear_fifos(pio, i);
        pio_sm_set_clkdiv(pio, i, clkdiv);
    }
    
    // Calculate phase shifts (in cycles)
    float phase_period = 1.0f / frequency;  // Use original frequency for phase
    float phase_shift = phase_period / 4.0f;  // 90 degrees
    
    // Configure each state machine
    for (int i = 0; i < 4; ++i) {
        float duty = (i % 2 == 0) ? duty_cycle_pair1 : duty_cycle_pair2;
        
        // Calculate timing in cycles
        uint32_t high_cycles = round_to_uint(duty * (double)total_cycles);
        uint32_t low_cycles = total_cycles - high_cycles;
        uint32_t phase_cycles = round_to_uint((float)i * phase_shift * 
                                            (float)sys_clk_hz / clkdiv);
        
        // Ensure minimum cycles
        if (high_cycles < 1) high_cycles = 1;
        if (low_cycles < 1) low_cycles = 1;
        if (phase_cycles == 0 && i > 0) phase_cycles = 1;

        printf("[DEBUG] SM%d: phase=%lu, high=%lu, low=%lu cycles (duty=%.1f%%)\n",
               i, (unsigned long)phase_cycles, (unsigned long)high_cycles,
               (unsigned long)low_cycles, duty * 100.0f);

        // Update state machine
        pio_sm_put_blocking(pio, i, phase_cycles);
        pio_sm_put_blocking(pio, i, high_cycles);
        pio_sm_put_blocking(pio, i, low_cycles);
    }
    
    current_frequency = frequency;
    current_duty_cycle = duty_cycle_pair1;
    
    printf("[INFO] PWM updated: %.2f Hz (actual: %.2f Hz)\n", 
           frequency, effective_freq);
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

