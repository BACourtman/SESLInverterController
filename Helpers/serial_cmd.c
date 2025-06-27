// serial_cmd.c
// This file contains the serial command handling functions

#include "serial_cmd.h"
#include "thermocouple.h"
#include "GPIO_pwm_discharge.h"
#include "pwm_control.h"  // Add this include
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

void print_help(void) {
    printf("Available commands:\n");
    printf("  FREQ <frequency> <duty_cycle>   - Set frequency and duty cycle\n");
    printf("  TC_ON 0|1                       - Toggle thermocouple auto print\n");
    printf("  TC_CSV                          - Print thermocouple log as CSV\n");
    printf("  DISCHARGE_STEP <duration> CH1 <duties> CH2 <duties> - Quick discharge setup\n");
    printf("  DISCHARGE_CSV <step_duration>   - Start CSV discharge input mode\n");
    printf("  DISCHARGE_CSV_END               - End CSV input and commit sequence\n");
    printf("  DISCHARGE_STATUS                - Show current discharge sequence\n");
    printf("  DISCHARGE_DEBUG 0|1             - Enable/disable manual discharge trigger\n");
    printf("  DISCHARGE_TRIGGER 0|1           - Set manual discharge trigger (debug mode)\n");
    printf("  DISCHARGE_TRIGGER_STATUS        - Show discharge trigger status\n");
    printf("  PIO_DEBUG 0|1                   - Enable/disable manual PIO trigger\n");
    printf("  PIO_TRIGGER 0|1                 - Set manual PIO trigger (debug mode)\n");
    printf("  PIO_TRIGGER_STATUS              - Show PIO trigger status\n");
    printf("  HELP                            - Show this help message\n");
}

bool process_serial_commands(float *frequency, float *duty_cycle, int *auto_tc_print) {
    static char cmd[1024] = {0};
    static int chars = 0;
    bool updated = false;
    
    // Read command from serial input
    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT && chars < sizeof(cmd) - 1) {
        if (c == '\n' || c == '\r') break;
        cmd[chars++] = (char)c;
        c = getchar_timeout_us(0);
    }
    
    if (chars >= sizeof(cmd) - 1) {
        printf("Warning: Command too long, truncated\n");
    }
    
    cmd[chars] = '\0';
    
    if (chars > 0) {
        // Check if we're in CSV mode first
        if (discharge_seq.csv_mode && strncmp(cmd, "DISCHARGE_CSV_END", 17) != 0) {
            if (add_discharge_csv_line(cmd)) {
                printf("CSV line %d added\n", discharge_seq.csv_step_count);
            } else {
                printf("Invalid CSV line format\n");
            }
        }
        // Regular command processing
        else if (strncmp(cmd, "FREQ", 4) == 0) {
            float new_freq, new_duty;
            if (sscanf(cmd + 4, "%f %f", &new_freq, &new_duty) == 2) {
                if (new_freq <= 0 || new_freq >= 1e6 || new_duty < 0 || new_duty > 0.5) {
                    printf("Invalid frequency or duty cycle values.\n");
                } else {
                    *frequency = new_freq;
                    *duty_cycle = new_duty;
                    printf("Updated: Frequency = %.2f Hz, Duty Cycle = %.2f\n", *frequency, *duty_cycle);
                    updated = true;
                }
            } else {
                printf("Invalid FREQ command. Usage: FREQ <frequency> <duty_cycle>\n");
            }
        } else if (strncmp(cmd, "TC_ON", 5) == 0) {
            int tcon_val;
            if (sscanf(cmd + 5, "%d", &tcon_val) == 1 && (tcon_val == 0 || tcon_val == 1)) {
                *auto_tc_print = tcon_val;
                printf("Thermocouple auto print %s\n", *auto_tc_print ? "ON" : "OFF");
            } else {
                printf("Invalid TC_ON command. Usage: TC_ON 0|1\n");
            }
        } else if (strcmp(cmd, "TC_CSV") == 0) {
            printf("TC_CSV command received. Printing thermocouple log...\n");
            print_tc_log_csv();
        } else if (strncmp(cmd, "DISCHARGE_STEP", 14) == 0) {
            set_discharge_sequence_step(cmd);
        } else if (strncmp(cmd, "DISCHARGE_CSV ", 14) == 0) {
            uint32_t step_duration;
            if (sscanf(cmd + 14, "%lu", &step_duration) == 1) {
                start_discharge_csv_mode(step_duration);
                printf("CSV mode started. Step duration: %lu ms\n", step_duration);
                printf("Send CSV data (duty1,duty2 format), then DISCHARGE_CSV_END\n");
            } else {
                printf("Invalid CSV command. Usage: DISCHARGE_CSV <step_duration_ms>\n");
            }
        } else if (strcmp(cmd, "DISCHARGE_CSV_END") == 0) {
            if (discharge_seq.csv_mode) {
                end_discharge_csv_mode();
                printf("CSV input completed. Sequence programmed with %d steps.\n", 
                       discharge_seq.csv_step_count);
            } else {
                printf("Not in CSV mode\n");
            }
        } else if (strcmp(cmd, "DISCHARGE_STATUS") == 0) {
            if (discharge_seq.max_cycle_duration_ms > 0) {
                printf("Current step-based discharge sequence:\n");
                printf("  Step duration: %lu ms\n", discharge_seq.step_duration_ms);
                printf("  Max cycle duration: %lu ms\n", discharge_seq.max_cycle_duration_ms);
                
                for (int ch = 0; ch < NUM_PWM_CHANNELS; ch++) {
                    ChannelSequence *seq = &discharge_seq.channels[ch];
                    if (seq->num_steps > 0) {
                        printf("  Channel %d (%d steps, %lu ms total):\n", ch + 1, seq->num_steps, seq->total_duration_ms);                        
                        // Show first 5 steps as preview
                        int preview_steps = (seq->num_steps > 5) ? 5 : seq->num_steps;
                        for (int i = 0; i < preview_steps; i++) {
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
                printf("No discharge sequence programmed\n");
            }
        } else if (strcmp(cmd, "HELP") == 0) {
            print_help();
            print_discharge_help();

        // DISCHARGE trigger commands
        } else if (strncmp(cmd, "DISCHARGE_DEBUG", 15) == 0) {
            int debug_enable;
            if (sscanf(cmd + 15, "%d", &debug_enable) == 1 && (debug_enable == 0 || debug_enable == 1)) {
                set_discharge_debug_mode(debug_enable == 1);
            } else {
                printf("Invalid DISCHARGE_DEBUG command. Usage: DISCHARGE_DEBUG 0|1\n");
            }
        }
        else if (strncmp(cmd, "DISCHARGE_TRIGGER", 17) == 0) {
            int trigger_state;
            if (sscanf(cmd + 17, "%d", &trigger_state) == 1 && (trigger_state == 0 || trigger_state == 1)) {
                set_manual_discharge_trigger(trigger_state == 1);
            } else {
                printf("Invalid DISCHARGE_TRIGGER command. Usage: DISCHARGE_TRIGGER 0|1\n");
            }
        }
        else if (strcmp(cmd, "DISCHARGE_TRIGGER_STATUS") == 0) {
            print_discharge_trigger_status();
        }
        
        // PIO trigger commands
        else if (strncmp(cmd, "PIO_DEBUG", 9) == 0) {
            int debug_enable;
            if (sscanf(cmd + 9, "%d", &debug_enable) == 1 && (debug_enable == 0 || debug_enable == 1)) {
                set_pio_debug_mode(debug_enable == 1);
            } else {
                printf("Invalid PIO_DEBUG command. Usage: PIO_DEBUG 0|1\n");
            }
        }
        else if (strncmp(cmd, "PIO_TRIGGER", 11) == 0) {
            int trigger_state;
            if (sscanf(cmd + 11, "%d", &trigger_state) == 1 && (trigger_state == 0 || trigger_state == 1)) {
                set_manual_pio_trigger(trigger_state == 1);
            } else {
                printf("Invalid PIO_TRIGGER command. Usage: PIO_TRIGGER 0|1\n");
            }
        }
        else if (strcmp(cmd, "PIO_TRIGGER_STATUS") == 0) {
            print_pio_trigger_status();
        }
        else {
            printf("Unrecognized command: %s\n", cmd);
            printf("Type HELP for a list of commands.\n");
        }
        
        
        chars = 0;
        cmd[0] = '\0';
    }
    
    return updated;
}