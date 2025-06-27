// GPIO_pwm_discharge.c
// This file contains the implementation of the GPIO PWM discharge functionality on 2 GPIO pins for the DC-DC converter.

#include "GPIO_pwm_discharge.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global variables
DischargeSequence discharge_seq = {0};
volatile bool trigger_active = false;

// PWM slice numbers
static uint slice_num_1, slice_num_2;
static uint chan_1, chan_2;

void pwm_discharge_init(void) {
    // Initialize GPIO pins for PWM
    gpio_set_function(PWM_PIN_1, GPIO_FUNC_PWM);
    gpio_set_function(PWM_PIN_2, GPIO_FUNC_PWM);
    
    // Get PWM slice numbers
    slice_num_1 = pwm_gpio_to_slice_num(PWM_PIN_1);
    slice_num_2 = pwm_gpio_to_slice_num(PWM_PIN_2);
    chan_1 = pwm_gpio_to_channel(PWM_PIN_1);
    chan_2 = pwm_gpio_to_channel(PWM_PIN_2);
    
    // Calculate clock divider for 10kHz PWM
    // System clock is 125MHz, we want 10kHz with 16-bit resolution
    float clock_div = 125000000.0f / (PWM_FREQ_HZ * 65536);
    
    // Configure PWM slices
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_div);
    pwm_config_set_wrap(&config, 65535); // 16-bit resolution
    
    pwm_init(slice_num_1, &config, false);
    pwm_init(slice_num_2, &config, false);
    
    // Set initial duty cycle to 0
    pwm_set_chan_level(slice_num_1, chan_1, 0);
    pwm_set_chan_level(slice_num_2, chan_2, 0);
    
    // Initialize trigger pin
    gpio_init(TRIGGER_PIN_DISCHARGE);
    gpio_set_dir(TRIGGER_PIN_DISCHARGE, GPIO_IN);
    gpio_pull_up(TRIGGER_PIN_DISCHARGE);
    
    printf("PWM Discharge initialized on pins %d and %d, trigger pin %d\n", 
           PWM_PIN_1, PWM_PIN_2, TRIGGER_PIN_DISCHARGE);
}

void set_pwm_duty_cycle(float duty1, float duty2) {
    uint16_t level1 = (uint16_t)(duty1 * 65535);
    uint16_t level2 = (uint16_t)(duty2 * 65535);
    
    pwm_set_chan_level(slice_num_1, chan_1, level1);
    pwm_set_chan_level(slice_num_2, chan_2, level2);
}

void start_discharge_sequence(void) {
    pwm_set_enabled(slice_num_1, true);
    pwm_set_enabled(slice_num_2, true);
    trigger_active = true;
}

void stop_discharge_sequence(void) {
    pwm_set_enabled(slice_num_1, false);
    pwm_set_enabled(slice_num_2, false);
    set_pwm_duty_cycle(0.0f, 0.0f);
    trigger_active = false;
}

float get_current_duty_for_channel(int channel, uint32_t elapsed_ms) {
    ChannelSequence *seq = &discharge_seq.channels[channel];
    
    if (seq->num_steps == 0) {
        return 0.0f;
    }
    
    // Calculate which step we're in based on elapsed time and step duration
    uint32_t current_step_index = elapsed_ms / discharge_seq.step_duration_ms;
    
    // If we're past the end of this channel's sequence, hold the last duty cycle
    if (current_step_index >= seq->num_steps) {
        return seq->duty_cycles[seq->num_steps - 1];
    }
    
    return seq->duty_cycles[current_step_index];
}

void set_discharge_debug_mode(bool enable) {
    discharge_seq.debug_mode = enable;
    if (enable) {
        printf("DISCHARGE Debug mode ENABLED - Manual trigger control active\n");
        printf("Hardware discharge trigger pin %d is now IGNORED\n", TRIGGER_PIN_DISCHARGE);
    } else {
        printf("DISCHARGE Debug mode DISABLED - Hardware trigger pin %d active\n", TRIGGER_PIN_DISCHARGE);
        discharge_seq.manual_trigger_state = false;
    }
}

void set_manual_discharge_trigger(bool state) {
    if (!discharge_seq.debug_mode) {
        printf("Error: DISCHARGE debug mode not enabled. Use DISCHARGE_DEBUG 1 first.\n");
        return;
    }
    
    discharge_seq.manual_trigger_state = state;
    printf("Manual DISCHARGE trigger set to %s\n", state ? "ACTIVE (LOW)" : "INACTIVE (HIGH)");
}

bool get_effective_discharge_trigger_state(void) {
    if (discharge_seq.debug_mode) {
        // In debug mode, use manual trigger state
        return discharge_seq.manual_trigger_state;
    } else {
        // In normal mode, read hardware pin (active LOW)
        return !gpio_get(TRIGGER_PIN_DISCHARGE);
    }
}

void print_discharge_trigger_status(void) {
    bool hw_trigger = !gpio_get(TRIGGER_PIN_DISCHARGE);
    bool effective_trigger = get_effective_discharge_trigger_state();
    
    printf("DISCHARGE Trigger Status:\n");
    printf("  Debug Mode: %s\n", discharge_seq.debug_mode ? "ENABLED" : "DISABLED");
    printf("  Hardware Pin %d: %s (%s)\n", 
           TRIGGER_PIN_DISCHARGE, 
           hw_trigger ? "ACTIVE" : "INACTIVE",
           hw_trigger ? "LOW" : "HIGH");
    
    if (discharge_seq.debug_mode) {
        printf("  Manual Trigger: %s\n", 
               discharge_seq.manual_trigger_state ? "ACTIVE" : "INACTIVE");
    }
    
    printf("  Effective Trigger: %s\n", effective_trigger ? "ACTIVE" : "INACTIVE");
    printf("  Sequence Running: %s\n", trigger_active ? "YES" : "NO");
}

void core1_entry(void) {
    printf("Core 1 started - PWM Discharge controller\n");
    
    absolute_time_t cycle_start_time;
    bool sequence_running = false;
    uint32_t last_logged_step[NUM_PWM_CHANNELS] = {999, 999};
    
    while (true) {
        // Use effective trigger state (hardware or manual)
        bool trigger_state = get_effective_discharge_trigger_state();
        
        // Check for trigger edge (active = true)
        if (trigger_state && !sequence_running && discharge_seq.max_cycle_duration_ms > 0) {
            // Start new sequence
            sequence_running = true;
            cycle_start_time = get_absolute_time();
            last_logged_step[0] = 999;
            last_logged_step[1] = 999;
            
            printf("Core1: Starting synchronized discharge sequence (%s trigger)\n", 
                   discharge_seq.debug_mode ? "MANUAL" : "HARDWARE");
            printf("Core1: Max duration: %lu ms, step duration: %lu ms\n", 
                   discharge_seq.max_cycle_duration_ms, discharge_seq.step_duration_ms);
            
            // Set initial duty cycles
            float duty1 = get_current_duty_for_channel(0, 0);
            float duty2 = get_current_duty_for_channel(1, 0);
            set_pwm_duty_cycle(duty1, duty2);
            start_discharge_sequence();
        }
        
        // Stop sequence if trigger goes inactive
        if (!trigger_state && sequence_running) {
            sequence_running = false;
            stop_discharge_sequence();
            printf("Core1: Discharge sequence stopped by %s trigger\n", 
                   discharge_seq.debug_mode ? "MANUAL" : "HARDWARE");
        }
        
        // Handle sequence timing
        if (sequence_running && discharge_seq.max_cycle_duration_ms > 0) {
            uint32_t elapsed_ms = absolute_time_diff_us(cycle_start_time, get_absolute_time()) / 1000;
            
            // Check if we've completed the full cycle
            if (elapsed_ms >= discharge_seq.max_cycle_duration_ms) {
                // Loop back to beginning
                cycle_start_time = get_absolute_time();
                elapsed_ms = 0;
                last_logged_step[0] = 999;
                last_logged_step[1] = 999;
                printf("Core1: Restarting synchronized cycle\n");
            }
            
            // Update duty cycles for both channels
            float duty1 = get_current_duty_for_channel(0, elapsed_ms);
            float duty2 = get_current_duty_for_channel(1, elapsed_ms);
            set_pwm_duty_cycle(duty1, duty2);
            
            // Log step changes for debugging (avoid spam)
            for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
                ChannelSequence *seq = &discharge_seq.channels[ch];
                uint32_t current_step = elapsed_ms / discharge_seq.step_duration_ms;
                
                if (seq->num_steps > 0) {
                    if (current_step < seq->num_steps && current_step != last_logged_step[ch]) {
                        printf("Core1: Ch%d Step %lu: %.1f%% (%lu ms mark)\n", 
                               ch + 1, current_step, seq->duty_cycles[current_step] * 100, elapsed_ms);
                        last_logged_step[ch] = current_step;
                    } else if (current_step >= seq->num_steps && last_logged_step[ch] < seq->num_steps) {
                        printf("Core1: Ch%d holding last duty %.1f%% until cycle end\n", 
                               ch + 1, seq->duty_cycles[seq->num_steps - 1] * 100);
                        last_logged_step[ch] = seq->num_steps; // Mark as logged
                    }
                }
            }
        }
        
        sleep_ms(1); // Small delay to prevent busy waiting
    }
}

void calculate_sequence_durations(void) {
    discharge_seq.max_cycle_duration_ms = 0;
    
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        ChannelSequence *seq = &discharge_seq.channels[ch];
        seq->total_duration_ms = seq->num_steps * discharge_seq.step_duration_ms;
        
        if (seq->total_duration_ms > discharge_seq.max_cycle_duration_ms) {
            discharge_seq.max_cycle_duration_ms = seq->total_duration_ms;
        }
    }
}

void set_discharge_sequence_step(const char* command) {
    // Parse: DISCHARGE_STEP 100 CH1 0.5,0.7,0.3 CH2 0.2,0.9,0.1
    char* cmd_copy = malloc(strlen(command) + 1);
    strcpy(cmd_copy, command);
    
    char* token = strtok(cmd_copy, " ");
    if (!token || strcmp(token, "DISCHARGE_STEP") != 0) {
        printf("Invalid format\n");
        free(cmd_copy);
        return;
    }
    
    // Get step duration
    token = strtok(NULL, " ");
    if (!token) {
        printf("Missing step duration\n");
        free(cmd_copy);
        return;
    }
    
    uint32_t step_duration = atoi(token);
    if (step_duration == 0) {
        printf("Invalid step duration: %lu (must be > 0)\n", step_duration);
        free(cmd_copy);
        return;
    }
    
    discharge_seq.step_duration_ms = step_duration;
    
    // Reset sequences
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        discharge_seq.channels[ch].num_steps = 0;
    }
    
    // Parse channel data
    int current_channel = -1;
    while ((token = strtok(NULL, " ")) != NULL) {
        if (strcmp(token, "CH1") == 0) {
            current_channel = 0;
        } else if (strcmp(token, "CH2") == 0) {
            current_channel = 1;
        } else if (current_channel >= 0) {
            // Parse comma-separated duty cycles
            char* duty_list = strdup(token); // Make a copy for strtok
            char* duty_token = strtok(duty_list, ",");
            int step = 0;
            
            while (duty_token && step < MAX_DISCHARGE_STEPS) {
                float duty = atof(duty_token);
                if (duty >= 0.0f && duty <= 1.0f) {
                    discharge_seq.channels[current_channel].duty_cycles[step] = duty;
                    step++;
                } else {
                    printf("Invalid duty cycle: %.3f (must be 0.0-1.0)\n", duty);
                    break;
                }
                duty_token = strtok(NULL, ",");
            }
            
            discharge_seq.channels[current_channel].num_steps = step;
            printf("Channel %d: %d steps programmed\n", current_channel + 1, step);
            
            free(duty_list);
        }
    }
    
    calculate_sequence_durations();
    discharge_seq.enabled = (discharge_seq.max_cycle_duration_ms > 0);
    
    if (discharge_seq.enabled) {
        printf("Step-based discharge sequence programmed:\n");
        printf("  Step duration: %lu ms\n", discharge_seq.step_duration_ms);
        printf("  Max cycle duration: %lu ms\n", discharge_seq.max_cycle_duration_ms);
        
        for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
            ChannelSequence *seq = &discharge_seq.channels[ch];
            if (seq->num_steps > 0) {
                printf("  Channel %d (%d steps, %lu ms total):\n", ch + 1, seq->num_steps, seq->total_duration_ms);
                for (int i = 0; i < seq->num_steps && i < 5; i++) { // Show first 5 steps
                    printf("    Step %d: %.1f%%\n", i, seq->duty_cycles[i] * 100);
                }
                if (seq->num_steps > 5) {
                    printf("    ... and %d more steps\n", seq->num_steps - 5);
                }
                
                if (seq->total_duration_ms < discharge_seq.max_cycle_duration_ms) {
                    printf("    Then hold %.1f%% for remaining %lu ms\n",
                           seq->duty_cycles[seq->num_steps - 1] * 100,
                           discharge_seq.max_cycle_duration_ms - seq->total_duration_ms);
                }
            } else {
                printf("  Channel %d: No sequence programmed\n", ch + 1);
            }
        }
    } else {
        printf("No valid discharge sequence programmed\n");
    }
    
    free(cmd_copy);
}

void start_discharge_csv_mode(uint32_t step_duration_ms) {
    discharge_seq.csv_mode = true;
    discharge_seq.csv_step_count = 0;
    discharge_seq.step_duration_ms = step_duration_ms;
    
    // Reset sequences
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        discharge_seq.channels[ch].num_steps = 0;
    }
    
    printf("CSV mode started. Step duration: %lu ms\n", step_duration_ms);
    printf("Send CSV data in format: duty1,duty2\n");
    printf("Example:\n");
    printf("0.5,0.2\n");
    printf("0.7,0.9\n");
    printf("Then send: DISCHARGE_CSV_END\n");
}

bool add_discharge_csv_line(const char* line) {
    if (!discharge_seq.csv_mode || discharge_seq.csv_step_count >= MAX_DISCHARGE_STEPS) {
        return false;
    }
    
    // Skip header line or empty lines
    if (strstr(line, "CH1") || strstr(line, "CH2") || strlen(line) < 3) {
        return true;
    }
    
    float duty1, duty2;
    if (sscanf(line, "%f,%f", &duty1, &duty2) == 2) {
        if (duty1 >= 0.0f && duty1 <= 1.0f && duty2 >= 0.0f && duty2 <= 1.0f) {
            discharge_seq.channels[0].duty_cycles[discharge_seq.csv_step_count] = duty1;
            discharge_seq.channels[1].duty_cycles[discharge_seq.csv_step_count] = duty2;
            discharge_seq.csv_step_count++;
            return true;
        } else {
            printf("Invalid duty cycle values: %.3f,%.3f (must be 0.0-1.0)\n", duty1, duty2);
        }
    } else {
        printf("Invalid CSV format. Expected: duty1,duty2\n");
    }
    
    return false;
}

void end_discharge_csv_mode(void) {
    discharge_seq.csv_mode = false;
    
    // Set step counts for both channels
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        discharge_seq.channels[ch].num_steps = discharge_seq.csv_step_count;
    }
    
    calculate_sequence_durations();
    discharge_seq.enabled = (discharge_seq.csv_step_count > 0);
    
    if (discharge_seq.enabled) {
        printf("CSV input completed. Sequence programmed:\n");
        printf("  Steps: %d\n", discharge_seq.csv_step_count);
        printf("  Step duration: %lu ms\n", discharge_seq.step_duration_ms);
        printf("  Total cycle duration: %lu ms\n", discharge_seq.max_cycle_duration_ms);
        
        // Show first few steps as preview
        int preview_steps = (discharge_seq.csv_step_count > 5) ? 5 : discharge_seq.csv_step_count;
        printf("  Preview (first %d steps):\n", preview_steps);
        for (int i = 0; i < preview_steps; i++) {
            printf("    Step %d: CH1=%.1f%%, CH2=%.1f%%\n", i, 
                   discharge_seq.channels[0].duty_cycles[i] * 100,
                   discharge_seq.channels[1].duty_cycles[i] * 100);
        }
        if (discharge_seq.csv_step_count > 5) {
            printf("    ... and %d more steps\n", discharge_seq.csv_step_count - 5);
        }
    } else {
        printf("No valid CSV data received\n");
    }
}

void print_discharge_help(void) {
    printf("Discharge PWM Commands:\n");
    printf("  Method 1 - Quick setup:\n");
    printf("    DISCHARGE_STEP <duration_ms> CH1 <d1,d2,d3> CH2 <d1,d2,d3>\n");
    printf("    Example: DISCHARGE_STEP 100 CH1 0.5,0.7,0.3 CH2 0.2,0.9,0.1\n");
    printf("  Method 2 - CSV mode (for large datasets):\n");
    printf("    DISCHARGE_CSV <step_duration_ms>\n");
    printf("    0.5,0.2\n");
    printf("    0.7,0.9\n");
    printf("    0.3,0.1\n");
    printf("    DISCHARGE_CSV_END\n");
    printf("  DISCHARGE_STATUS - Show current sequence\n");
    printf("  Pins: CH1=%d, CH2=%d, Trigger=%d (active LOW)\n", 
           PWM_PIN_1, PWM_PIN_2, TRIGGER_PIN_DISCHARGE);
    printf("  Frequency: %d Hz\n", PWM_FREQ_HZ);
}

