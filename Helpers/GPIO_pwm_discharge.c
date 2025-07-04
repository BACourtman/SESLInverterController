// GPIO_pwm_discharge.c
// This file contains the implementation of the GPIO PWM discharge functionality on 2 GPIO pins for the DC-DC converter.

#include "GPIO_pwm_discharge.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// --- Global variables ---
DischargeSequence discharge_seq = {0};
volatile bool sequence_running = false;
uint32_t cycle_start_time = 0;
static bool verbose_mode = false;

// PWM slice and channel numbers
static uint slice_num_1, slice_num_2;
static uint chan_1, chan_2;

// --- Corrected PWM Initialization ---
void pwm_discharge_init(void) {
    gpio_set_function(PWM_PIN_1, GPIO_FUNC_PWM);
    gpio_set_function(PWM_PIN_2, GPIO_FUNC_PWM);

    slice_num_1 = pwm_gpio_to_slice_num(PWM_PIN_1);
    slice_num_2 = pwm_gpio_to_slice_num(PWM_PIN_2);
    chan_1 = pwm_gpio_to_channel(PWM_PIN_1);
    chan_2 = pwm_gpio_to_channel(PWM_PIN_2);

    // Configure for exactly 10kHz.
    // System Clock = 125MHz.
    // 125,000,000 Hz / 10,000 Hz = 12,500 clock cycles per PWM cycle.
    // Use clkdiv=1.0 and wrap=12499 for precision.
    const uint16_t wrap_value = 12499; // Counts from 0 to 12499 (12500 total)
    const float clock_div = 1.0f;

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_div);
    pwm_config_set_wrap(&config, wrap_value);

    pwm_init(slice_num_1, &config, true);
    pwm_init(slice_num_2, &config, true);

    pwm_set_chan_level(slice_num_1, chan_1, 0);
    pwm_set_chan_level(slice_num_2, chan_2, 0);

    gpio_init(TRIGGER_PIN_DISCHARGE);
    gpio_set_dir(TRIGGER_PIN_DISCHARGE, GPIO_IN);
    gpio_pull_up(TRIGGER_PIN_DISCHARGE);

    printf("PWM Discharge initialized at 10kHz.\n");
}

// --- Robust Command Parser ---
void set_discharge_sequence_step(const char* command) {
    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    // 1. Parse step duration
    if (sscanf(cmd_copy, "DISCHARGE_STEP %lu", &discharge_seq.step_duration_ms) != 1 || discharge_seq.step_duration_ms == 0) {
        printf("Error: Invalid or missing step duration.\n");
        return;
    }

    // 2. Reset sequences
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        discharge_seq.channels[ch].num_steps = 0;
    }

    // 3. Parse each channel's duty cycle list
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        char tag[5];
        sprintf(tag, "CH%d", ch + 1);
        char* p = strstr(cmd_copy, tag);

        if (p) {
            p += strlen(tag); // Move pointer past "CHx"
            while (*p == ' ') p++; // Skip spaces

            char* end_of_duties = strchr(p, ' ');
            if (end_of_duties) *end_of_duties = '\0'; // Terminate the duty string

            char* duty_token = strtok(p, ",");
            int step_count = 0;
            while (duty_token != NULL && step_count < MAX_DISCHARGE_STEPS) {
                float duty = atof(duty_token);
                if (duty >= 0.0f && duty <= 1.0f) {
                    discharge_seq.channels[ch].duty_cycles[step_count++] = duty;
                }
                duty_token = strtok(NULL, ",");
            }
            discharge_seq.channels[ch].num_steps = step_count;
        }
    }

    // 4. Calculate total cycle duration based on the longest sequence
    discharge_seq.max_cycle_duration_ms = 0;
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        uint32_t ch_duration = discharge_seq.channels[ch].num_steps * discharge_seq.step_duration_ms;
        if (ch_duration > discharge_seq.max_cycle_duration_ms) {
            discharge_seq.max_cycle_duration_ms = ch_duration;
        }
    }

    // 5. Finalize and print status
    discharge_seq.enabled = (discharge_seq.max_cycle_duration_ms > 0);
    if (discharge_seq.enabled) {
        printf("Discharge sequence programmed. Step: %lu ms, Cycle: %lu ms\n",
               discharge_seq.step_duration_ms, discharge_seq.max_cycle_duration_ms);
        for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
            printf("  CH%d has %d steps.\n", ch + 1, discharge_seq.channels[ch].num_steps);
        }
    } else {
        printf("No valid sequence programmed.\n");
    }
}

// --- CSV Input Handling ---
static bool csv_input_mode = false;

bool is_in_csv_mode(void) {
    return csv_input_mode;
}

void start_csv_input(uint32_t step_duration) {
    if (step_duration == 0) {
        printf("Error: CSV step duration cannot be zero.\n");
        return;
    }
    printf("Starting CSV input with %lu ms steps. Enter 'CH1_duty,CH2_duty' per line.\n", step_duration);
    printf("Send 'DISCHARGE_CSV_END' to finish.\n");

    // Reset the sequence
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        discharge_seq.channels[ch].num_steps = 0;
    }
    discharge_seq.step_duration_ms = step_duration;
    csv_input_mode = true;
}

void process_csv_line(const char* line) {
    if (!csv_input_mode) return;

    float duty1 = -1.0f, duty2 = -1.0f;
    int items = sscanf(line, "%f,%f", &duty1, &duty2);

    if (items < 1) {
        printf("Invalid CSV line: %s\n", line);
        return;
    }

    // Add step to CH1 if valid
    if (discharge_seq.channels[0].num_steps < MAX_DISCHARGE_STEPS && duty1 >= 0.0f && duty1 <= 1.0f) {
        discharge_seq.channels[0].duty_cycles[discharge_seq.channels[0].num_steps++] = duty1;
    }

    // Add step to CH2 if valid
    if (items > 1 && discharge_seq.channels[1].num_steps < MAX_DISCHARGE_STEPS && duty2 >= 0.0f && duty2 <= 1.0f) {
        discharge_seq.channels[1].duty_cycles[discharge_seq.channels[1].num_steps++] = duty2;
    }
}

void end_csv_input() {
    if (!csv_input_mode) return;
    csv_input_mode = false;

    // Finalize sequence (calculate durations)
    discharge_seq.max_cycle_duration_ms = 0;
    for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
        uint32_t ch_duration = discharge_seq.channels[ch].num_steps * discharge_seq.step_duration_ms;
        if (ch_duration > discharge_seq.max_cycle_duration_ms) {
            discharge_seq.max_cycle_duration_ms = ch_duration;
        }
    }

    discharge_seq.enabled = (discharge_seq.max_cycle_duration_ms > 0);
    if (discharge_seq.enabled) {
        printf("CSV input finished. Sequence programmed. Cycle: %lu ms\n", discharge_seq.max_cycle_duration_ms);
        for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
            printf("  CH%d has %d steps.\n", ch + 1, discharge_seq.channels[ch].num_steps);
        }
    } else {
        printf("No valid sequence from CSV input.\n");
    }
}

// --- Corrected Core 1 Real-time Loop ---
void core1_entry(void) {
    const uint16_t WRAP_VALUE = 12499; // Must match the value in coinit
    static bool was_running = false;

    while (true) {
        if (sequence_running && discharge_seq.enabled) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            // Use modulo to loop the sequence
            uint32_t elapsed_ms = (now_ms - cycle_start_time) % discharge_seq.max_cycle_duration_ms;
            uint32_t current_step = elapsed_ms / discharge_seq.step_duration_ms;

            // Update both channels based on the current time step
            for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
                ChannelSequence *seq = &discharge_seq.channels[ch];
                float duty_cycle = 0.0f;

                if (seq->num_steps > 0) {
                    // If past the end of this channel's sequence, hold the last value.
                    // Otherwise, use the duty cycle for the current step.
                    uint32_t step_index = (current_step < seq->num_steps) ? current_step : (seq->num_steps - 1);
                    duty_cycle = seq->duty_cycles[step_index];
                }

                uint16_t level = (uint16_t)(duty_cycle * WRAP_VALUE);

                if (ch == 0) {
                    pwm_set_chan_level(slice_num_1, chan_1, level);
                } else {
                    pwm_set_chan_level(slice_num_2, chan_2, level);
                }
            }
            was_running = true;
        } else if (was_running) {
            // Sequence was running but is now stopped. Set outputs to 0.
            pwm_set_chan_level(slice_num_1, chan_1, 0);
            pwm_set_chan_level(slice_num_2, chan_2, 0);
            was_running = false;
        }

        sleep_us(100); // Loop rate
    }
}

// --- Trigger Handling and other functions ---
// (Keep your existing helper functions like get_effective_discharge_trigger_state, etc.)

void handle_discharge_trigger(void) {
    bool trigger_state = get_effective_discharge_trigger_state();

    if (trigger_state && !sequence_running && discharge_seq.enabled) {
        sequence_running = true;
        cycle_start_time = to_ms_since_boot(get_absolute_time());
        if (verbose_mode) printf("Discharge sequence started.\n");
    } else if (!trigger_state && sequence_running) {
        sequence_running = false;
        if (verbose_mode) printf("Discharge sequence stopped.\n");
    }
}

// --- Help and Status Functions ---
void print_discharge_help(void) {
    printf("--- Discharge Control Help ---\n");
    printf("  DISCHARGE_STEP <ms> CH1 <d1,..> [CH2 <d1,..>]\n");
    printf("    Defines a sequence in a single line.\n\n");
    printf("  DISCHARGE_CSV <ms>\n");
    printf("    Starts multi-line CSV input. Each line is 'CH1_duty,CH2_duty'.\n");
    printf("  DISCHARGE_CSV_END\n");
    printf("    Finishes CSV input and commits the sequence.\n\n");
    printf("  DISCHARGE_DEBUG <0|1>           - Enable/disable manual trigger override.\n");
    printf("  DISCHARGE_TRIGGER <0|1>         - Manually trigger sequence (requires debug mode).\n");
    printf("  DISCHARGE_TRIGGER_STATUS        - Show hardware and effective trigger status.\n");
    printf("  DISCHARGE_VERBOSE <0|1>         - Toggle step-by-step messages from the PWM core.\n");
    printf("  DISCHARGE_STATUS                - Show the currently programmed sequence.\n");
}

