// GPIO_control_V2.c
// This file contains the implementation of the GPIO PWM discharge functionality on 2 GPIO pins for the DC-DC converter.

#include "GPIO_control_V2.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// --- Pin Definitions ---
#define PWM_PIN_CH1 16
#define PWM_PIN_CH2 17
#define TRIGGER_PIN 18
#define MAX_STEPS 100

// --- Global Variables ---
typedef struct {
    float duty_cycles[MAX_STEPS];
    int num_steps;
} ChannelSequence;

static struct {
    ChannelSequence ch1;
    ChannelSequence ch2;
    uint32_t step_duration_ms;
    bool enabled;
    bool verbose;
    bool debug_mode;
    bool manual_trigger;
    bool invert_output;  // Add this line
} discharge_config = {
    .invert_output = true  // Default to inverting
};

static bool csv_input_mode = false;
static uint slice_ch1, slice_ch2;
static uint chan_ch1, chan_ch2;
static volatile bool sequence_running = false;

// --- PWM Initialization ---
void discharge_pwm_init(void) {
    // Set GPIO pins to PWM function
    gpio_set_function(PWM_PIN_CH1, GPIO_FUNC_PWM);
    gpio_set_function(PWM_PIN_CH2, GPIO_FUNC_PWM);
    
    // Get PWM slice and channel numbers
    slice_ch1 = pwm_gpio_to_slice_num(PWM_PIN_CH1);
    slice_ch2 = pwm_gpio_to_slice_num(PWM_PIN_CH2);
    chan_ch1 = pwm_gpio_to_channel(PWM_PIN_CH1);
    chan_ch2 = pwm_gpio_to_channel(PWM_PIN_CH2);
    
    // Get actual system clock frequency instead of assuming 125MHz
    uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    float clk_freq = (float)sys_clk_hz;
    
    // Calculate wrap value for 50kHz PWM based on actual clock frequency
    const uint32_t target_freq = 50000; // 50kHz
    const uint16_t wrap_value = (uint16_t)((clk_freq / target_freq) - 1);
    
    printf("[DEBUG] GPIO PWM Clock Configuration:\n");
    printf("[DEBUG]   System clock: %.0f Hz\n", clk_freq);
    printf("[DEBUG]   Target PWM frequency: %lu Hz\n", target_freq);
    printf("[DEBUG]   Calculated wrap value: %d\n", wrap_value);
    printf("[DEBUG]   Actual PWM frequency: %.2f Hz\n", clk_freq / (wrap_value + 1));
    
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 1.0f);
    pwm_config_set_wrap(&config, wrap_value);
    
    pwm_init(slice_ch1, &config, true);
    pwm_init(slice_ch2, &config, true);
    
    // Set initial duty based on inversion setting
    uint16_t initial_level = discharge_config.invert_output ? wrap_value : 0;
    pwm_set_chan_level(slice_ch1, chan_ch1, initial_level);
    pwm_set_chan_level(slice_ch2, chan_ch2, initial_level);

    // Setup trigger pin
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_IN);
    gpio_pull_down(TRIGGER_PIN);
    
    printf("[INFO] Discharge PWM initialized at %.2f Hz on pins %d and %d\n", 
           clk_freq / (wrap_value + 1), PWM_PIN_CH1, PWM_PIN_CH2);
}

// --- Core1 Real-time Loop ---
void core1_discharge_loop(void) {
    // Calculate wrap value dynamically based on actual clock frequency
    uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    const uint32_t target_freq = 50000; // 50kHz
    const uint16_t WRAP_VALUE = (uint16_t)((sys_clk_hz / target_freq) - 1);
    
    uint32_t cycle_start_time = 0;
    uint32_t current_step = 0;
    uint32_t step_start_time = 0;
    uint32_t last_step_logged = 0xFFFFFFFF;
    bool was_running = false;
    
    while (true) {
        bool trigger_active = discharge_config.debug_mode ? 
                             discharge_config.manual_trigger : 
                             gpio_get(TRIGGER_PIN);
        
        if (trigger_active && discharge_config.enabled && !sequence_running) {
            sequence_running = true;
            cycle_start_time = to_ms_since_boot(get_absolute_time());
            step_start_time = cycle_start_time;
            current_step = 0;
            last_step_logged = 0xFFFFFFFF;
            if (discharge_config.verbose) {
                printf("[INFO] Discharge sequence started\n");
            }
        } else if (!trigger_active && sequence_running) {
            sequence_running = false;
            pwm_set_chan_level(slice_ch1, chan_ch1, 0);
            pwm_set_chan_level(slice_ch2, chan_ch2, 0);
            current_step = 0;
            if (discharge_config.verbose) {
                printf("[INFO] Discharge sequence stopped\n");
            }
        }
        
        if (sequence_running && discharge_config.step_duration_ms > 0) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            
            // Check if we need to advance to the next step
            if ((now - step_start_time) >= discharge_config.step_duration_ms) {
                current_step++;
                step_start_time = now;
                
                // Find the maximum number of steps across all channels
                uint32_t max_steps = 0;
                if (discharge_config.ch1.num_steps > max_steps) max_steps = discharge_config.ch1.num_steps;
                if (discharge_config.ch2.num_steps > max_steps) max_steps = discharge_config.ch2.num_steps;
                
                // Reset cycle if we've completed all steps
                if (max_steps > 0 && current_step >= max_steps) {
                    current_step = 0;
                    if (discharge_config.verbose && last_step_logged != current_step) {
                        printf("[DEBUG] Sequence cycle completed, restarting\n");
                        last_step_logged = current_step;
                    }
                }
                
                // Log step changes only when they actually change
                if (discharge_config.verbose && last_step_logged != current_step) {
                    printf("[DEBUG] Step %lu: CH1=%.2f, CH2=%.2f\n", 
                           current_step,
                           discharge_config.ch1.num_steps > 0 ? 
                           discharge_config.ch1.duty_cycles[current_step % discharge_config.ch1.num_steps] : 0.0f,
                           discharge_config.ch2.num_steps > 0 ? 
                           discharge_config.ch2.duty_cycles[current_step % discharge_config.ch2.num_steps] : 0.0f);
                    last_step_logged = current_step;
                }
            }
            
            // Update CH1 PWM based on current step
            if (discharge_config.ch1.num_steps > 0) {
                uint32_t ch1_step = current_step % discharge_config.ch1.num_steps;
                float duty = discharge_config.ch1.duty_cycles[ch1_step];
                
                // Apply inversion if enabled
                float final_duty = discharge_config.invert_output ? (1.0f - duty) : duty;
                uint16_t level = (uint16_t)(final_duty * WRAP_VALUE);
                
                pwm_set_chan_level(slice_ch1, chan_ch1, level);
            } else {
                pwm_set_chan_level(slice_ch1, chan_ch1, 0);
            }
            
            // Update CH2 PWM based on current step
            if (discharge_config.ch2.num_steps > 0) {
                uint32_t ch2_step = current_step % discharge_config.ch2.num_steps;
                float duty = discharge_config.ch2.duty_cycles[ch2_step];
                
                // Apply inversion if enabled
                float final_duty = discharge_config.invert_output ? (1.0f - duty) : duty;
                uint16_t level = (uint16_t)(final_duty * WRAP_VALUE);
                
                pwm_set_chan_level(slice_ch2, chan_ch2, level);
            } else {
                pwm_set_chan_level(slice_ch2, chan_ch2, 0);
            }
        }
        
        sleep_us(20); // Update timing - may need adjustment based on actual clock
    }
}

// --- Command Processing Functions ---
void process_discharge_step_command(const char* command) {
    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // Parse step duration
    uint32_t step_ms;
    if (sscanf(cmd_copy, "DC_STEP %lu", &step_ms) != 1 || step_ms == 0) {
        printf("[ERROR] Invalid step duration\n");
        return;
    }
    
    discharge_config.step_duration_ms = step_ms;
    discharge_config.ch1.num_steps = 0;
    discharge_config.ch2.num_steps = 0;
    
    // Make a fresh copy for CH1 parsing
    char ch1_copy[256];
    strncpy(ch1_copy, command, sizeof(ch1_copy) - 1);
    ch1_copy[sizeof(ch1_copy) - 1] = '\0';
    
    // Parse CH1 duties
    char* ch1_pos = strstr(ch1_copy, "CH1");
    if (ch1_pos) {
        ch1_pos += 3;
        while (*ch1_pos == ' ') ch1_pos++;
        
        // Find the end of CH1 section (either CH2 or end of string)
        char* ch1_end = strstr(ch1_pos, "CH2");
        if (ch1_end) {
            *ch1_end = '\0';  // Terminate CH1 section
        }
        
        char* token = strtok(ch1_pos, " ,");
        while (token && discharge_config.ch1.num_steps < MAX_STEPS) {
            float duty = atof(token);
            if (duty >= 0.0f && duty <= 1.0f) {
                discharge_config.ch1.duty_cycles[discharge_config.ch1.num_steps++] = duty;
            }
            token = strtok(NULL, " ,");
        }
    }
    
    // Make a fresh copy for CH2 parsing
    char ch2_copy[256];
    strncpy(ch2_copy, command, sizeof(ch2_copy) - 1);
    ch2_copy[sizeof(ch2_copy) - 1] = '\0';
    
    // Parse CH2 duties
    char* ch2_pos = strstr(ch2_copy, "CH2");
    if (ch2_pos) {
        ch2_pos += 3;
        while (*ch2_pos == ' ') ch2_pos++;
        
        char* token = strtok(ch2_pos, " ,");
        while (token && discharge_config.ch2.num_steps < MAX_STEPS) {
            float duty = atof(token);
            if (duty >= 0.0f && duty <= 1.0f) {
                discharge_config.ch2.duty_cycles[discharge_config.ch2.num_steps++] = duty;
            }
            token = strtok(NULL, " ,");
        }
    }
    
    discharge_config.enabled = (discharge_config.ch1.num_steps > 0 || discharge_config.ch2.num_steps > 0);
    printf("[INFO] Sequence configured: %lu ms steps, CH1=%d steps, CH2=%d steps\n", 
           step_ms, discharge_config.ch1.num_steps, discharge_config.ch2.num_steps);
}

void start_csv_input(uint32_t step_duration) {
    if (step_duration == 0) {
        printf("[ERROR] Invalid step duration\n");
        return;
    }
    
    discharge_config.step_duration_ms = step_duration;
    discharge_config.ch1.num_steps = 0;
    discharge_config.ch2.num_steps = 0;
    csv_input_mode = true;
    
    printf("[COMMAND] CSV mode started. Enter 'CH1_duty,CH2_duty' per line. Send 'DC_CSV_END' to finish.\n");
}

void process_csv_line(const char* line) {
    if (!csv_input_mode) return;
    float duty1, duty2;
    int parsed = sscanf(line, "%f,%f", &duty1, &duty2);
    if (parsed >= 1 && duty1 >= 0.0f && duty1 <= 1.0f && discharge_config.ch1.num_steps < MAX_STEPS) {
        discharge_config.ch1.duty_cycles[discharge_config.ch1.num_steps++] = duty1;
    }
    if (parsed >= 2 && duty2 >= 0.0f && duty2 <= 1.0f && discharge_config.ch2.num_steps < MAX_STEPS) {
        discharge_config.ch2.duty_cycles[discharge_config.ch2.num_steps++] = duty2;
    }
}

void end_csv_input(void) {
    csv_input_mode = false;
    discharge_config.enabled = (discharge_config.ch1.num_steps > 0 || discharge_config.ch2.num_steps > 0);
    
    printf("[COMMAND] CSV input finished. CH1=%d steps, CH2=%d steps\n", 
           discharge_config.ch1.num_steps, discharge_config.ch2.num_steps);
}

// --- Main Command Handler ---
bool process_discharge_command(const char* command) {
    if (csv_input_mode && strcmp(command, "DC_CSV_END") != 0) {
        process_csv_line(command);
        return true;
    }
    
    if (strncmp(command, "DC_STEP", 7) == 0) {
        process_discharge_step_command(command);
        return true;
    } else if (strncmp(command, "DC_CSV ", 7) == 0) {
        uint32_t step_ms = atoi(command + 7);
        start_csv_input(step_ms);
        return true;
    } else if (strcmp(command, "DC_CSV_END") == 0) {
        end_csv_input();
        return true;
    } else if (strncmp(command, "DC_DEBUG ", 9) == 0) {
        bool new_debug_mode = (atoi(command + 9) != 0);
        // Only print if the state actually changes
        if (discharge_config.debug_mode != new_debug_mode) {
            discharge_config.debug_mode = new_debug_mode;
            printf("[DEBUG] Debug mode: %s\n", discharge_config.debug_mode ? "ON" : "OFF");
        }
        return true;
    } else if (strncmp(command, "DC_TRIGGER ", 11) == 0) {
        if (discharge_config.debug_mode) {
            bool new_trigger = (atoi(command + 11) != 0);
            // Only print if the state actually changes
            if (discharge_config.manual_trigger != new_trigger) {
                discharge_config.manual_trigger = new_trigger;
                printf("[DEBUG] Manual trigger: %s\n", discharge_config.manual_trigger ? "ON" : "OFF");
            }
        } else {
            printf("[ERROR] Debug mode required for manual trigger\n");
        }
        return true;
    } else if (strcmp(command, "DC_TRIGGER_STATUS") == 0) {
        bool hw_trigger = gpio_get(TRIGGER_PIN);
        bool effective_trigger = discharge_config.debug_mode ? discharge_config.manual_trigger : hw_trigger;
        printf("[INFO] Hardware trigger: %s, Debug mode: %s, Manual trigger: %s, Effective: %s\n",
               hw_trigger ? "HIGH" : "LOW",
               discharge_config.debug_mode ? "ON" : "OFF",
               discharge_config.manual_trigger ? "ON" : "OFF",
               effective_trigger ? "ACTIVE" : "INACTIVE");
        return true;
    } else if (strncmp(command, "DC_VERBOSE ", 11) == 0) {
        bool new_verbose = (atoi(command + 11) != 0);
        // Only print if the state actually changes
        if (discharge_config.verbose != new_verbose) {
            discharge_config.verbose = new_verbose;
            printf("[DEBUG] Verbose mode: %s\n", discharge_config.verbose ? "ON" : "OFF");
        }
        return true;
    } else if (strcmp(command, "DC_STATUS") == 0) {
        printf("[COMMAND] Discharge Status:\n");
        printf("  Step duration: %lu ms\n", discharge_config.step_duration_ms);
        printf("  CH1 steps: %d\n", discharge_config.ch1.num_steps);
        printf("  CH2 steps: %d\n", discharge_config.ch2.num_steps);
        printf("  Enabled: %s\n", discharge_config.enabled ? "YES" : "NO");
        printf("  Running: %s\n", sequence_running ? "YES" : "NO");
        printf("  Output inversion: %s\n", discharge_config.invert_output ? "ENABLED" : "DISABLED");  // Add this line
        return true;
    } else if (strcmp(command, "DC_HELP") == 0) {
        print_discharge_help();
        return true;
    } else if (strncmp(command, "DC_INVERT ", 10) == 0) {
        bool new_invert = (atoi(command + 10) != 0);
        if (discharge_config.invert_output != new_invert) {
            discharge_config.invert_output = new_invert;
            printf("[COMMAND] Output inversion: %s\n", discharge_config.invert_output ? "ENABLED" : "DISABLED");
            printf("[INFO] Example: Input 0.8 will now output %s\n", 
                   discharge_config.invert_output ? "0.2 (20%)" : "0.8 (80%)");
        }
        return true;
    }
    
    return false; // Command not recognized
}

// --- Initialization Function ---
void discharge_system_init(void) {
    discharge_pwm_init();
    
    // Launch core1 real-time loop
    multicore_launch_core1(core1_discharge_loop);
    printf("[INFO] Core 1 launched for discharge PWM control\n");
    printf("[INFO] Use DC_HELP for commands.\n");
}

// --- Help Function ---
void print_discharge_help(void) {
    printf("[COMMAND]\n");
    printf("--- Discharge Control Help ---\n");
    printf("  DC_STEP <ms> CH1 <d1,..> [CH2 <d1,..>]\n");
    printf("    Defines a sequence in a single line.\n\n");
    printf("  DC_CSV <ms>\n");
    printf("    Starts multi-line CSV input. Each line is 'CH1_duty,CH2_duty'.\n");
    printf("  DC_CSV_END\n");
    printf("    Finishes CSV input and commits the sequence.\n\n");
    printf("  DC_INVERT <0|1>          - Toggle output inversion (0=normal, 1=inverted).\n");  // Add this line
    printf("  DC_DEBUG <0|1>           - Enable/disable manual trigger override.\n");
    printf("  DC_TRIGGER <0|1>         - Manually trigger sequence (requires debug mode).\n");
    printf("  DC_TRIGGER_STATUS        - Show hardware and effective trigger status.\n");
    printf("  DC_VERBOSE <0|1>         - Toggle step-by-step messages from the PWM core.\n");
    printf("  DC_STATUS                - Show the currently programmed sequence.\n");
}

// --- Utility Functions ---
bool is_csv_mode_active(void) {
    return csv_input_mode;
}

bool is_sequence_running(void) {
    return sequence_running;
}

