// serial_cmd.c
// This file contains the serial command handling functions

#include "serial_cmd.h"
#include "thermocouple.h"
#include "GPIO_control_V2.h"
#include "pwm_control.h"  // Add this include
#include "pico/stdlib.h"
#include "shutdown.h"
#include <stdio.h>
#include <string.h>

void print_help(void) {
    printf("[COMMAND] \n");
    printf("Available commands:\n");
    printf("  FREQ <frequency> <duty_cycle1> <duty_cycle2> - Set frequency and duty cycles\n");
    printf("  TC_ON 0|1                       - Toggle thermocouple auto print\n");
    printf("  TC_CSV                          - Print thermocouple log as CSV\n");
    printf("  TC_NOW                          - Print current thermocouple data\n");
    printf("  TC_ONBOARD                      - Print onboard temperature\n");
    printf("  DC_STEP <duration> CH1 <duties> CH2 <duties> - Quick discharge setup\n");
    printf("  DC_CSV <step_duration>          - Start CSV discharge input mode\n");
    printf("  DC_CSV_END                      - End CSV input and commit sequence\n");
    printf("  DC_STATUS                       - Show current DC discharge sequence\n");
    printf("  DC_DEBUG 0|1                    - Enable/disable manual DC discharge trigger\n");
    printf("  DC_TRIGGER 0|1                  - Set manual DC discharge trigger (debug mode)\n");
    printf("  DC_TRIGGER_STATUS               - Show DC discharge trigger status\n");
    printf("  DC_INVERT 0|1                   - Toggle DC discharge output inversion\n");
    printf("  DC_VERBOSE 0|1                - Toggle step-by-step output messages\n");
    printf("  PIO_DEBUG 0|1                   - Enable/disable manual PIO trigger\n");
    printf("  PIO_TRIGGER 0|1                 - Set manual PIO trigger (debug mode)\n");
    printf("  PIO_TRIGGER_STATUS              - Show PIO trigger status\n");
    printf("  RELAY 0|1                       - Toggle relay state\n");
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
        printf("[ERROR] Warning: Command too long, truncated\n");
    }
    
    if (chars > 0) {
        cmd[chars] = '\0';
        
        // Check if we're in CSV input mode first
        if (is_csv_mode_active()) {
            if (process_discharge_command(cmd)) {
                chars = 0;
                return false; // Command handled, no parameter changes
            }
        }
        
        // Handle discharge help
        if (strcmp(cmd, "DC_HELP") == 0) {
            print_discharge_help();
            chars = 0;
            return false;
        }
        
        if (chars > 0) {
            // Check if we're in CSV input mode first
            if (is_csv_mode_active()) {
            // In CSV mode, all commands except DC_CSV_END are treated as CSV data
                if (process_discharge_command(cmd)) {
                    return false; // Command was handled by discharge system
                }
            } else if (strncmp(cmd, "FREQ", 4) == 0 || strncmp(cmd, "FREQUENCY", 9) == 0) {
                float new_freq, new_duty1, new_duty2;
                int parsed = sscanf(cmd + 4, "%f %f %f", &new_freq, &new_duty1, &new_duty2);
                
                if (parsed == 3) {
                    // All three parameters provided
                    if (new_freq <= 0 || new_freq >= 1e6 || new_duty1 < 0 || new_duty1 > 1.0 || new_duty2 < 0 || new_duty2 > 1.0) {
                        printf("[ERROR] Invalid parameters.\n");
                        printf("[ERROR] Usage: FREQ <frequency> <duty_pair1> <duty_pair2>\n");
                    } else {
                        *frequency = new_freq;
                        *duty_cycle = new_duty1;  // Store first duty cycle for compatibility
                        update_pwm_parameters(*frequency, new_duty1, new_duty2);
                        printf("[COMMAND] Updated: Frequency = %.2f Hz, Pair1 = %.2f, Pair2 = %.2f\n", 
                               *frequency, new_duty1, new_duty2);
                        updated = true;
                    }
                } else if (parsed == 2) {
                    // Only frequency and one duty cycle provided - use same for both pairs
                    if (new_freq <= 0 || new_freq >= 1e6 || new_duty1 < 0 || new_duty1 > 1.0) {
                        printf("[ERROR] Invalid parameters.\n");
                    } else {
                        *frequency = new_freq;
                        *duty_cycle = new_duty1;
                        update_pwm_parameters(*frequency, new_duty1, new_duty1);  // Same duty for both pairs
                        printf("[COMMAND] Updated: Frequency = %.2f Hz, Both pairs = %.2f\n", *frequency, new_duty1);
                        updated = true;
                    }
                } else {
                    printf("[ERROR] Invalid FREQ command.\n");
                    printf("[ERROR] Usage: FREQ <frequency> <duty_pair1> <duty_pair2>\n");
                    printf("[ERROR] Usage: FREQ <frequency> <duty_both_pairs>\n");
                    printf("[ERROR] Example: FREQ 100000 0.5 0.3\n");
                    printf("[ERROR] Example: FREQ 100000 0.5\n");
                }
            } else if (strncmp(cmd, "TC_ON", 5) == 0) {
                int tcon_val;
                if (sscanf(cmd + 5, "%d", &tcon_val) == 1 && (tcon_val == 0 || tcon_val == 1)) {
                    *auto_tc_print = tcon_val;
                    printf("[COMMAND] Thermocouple auto print %s\n", *auto_tc_print ? "ON" : "OFF");
                } else {
                    printf("[ERROR] Invalid TC_ON command. Usage: TC_ON 0|1\n");
                }
            } else if (strcmp(cmd, "TC_CSV") == 0) {
                printf("[COMMAND] TC_CSV command received. Printing thermocouple log...\n");
                print_tc_log_csv();
                
            } else if (strcmp(cmd, "TC_NOW") == 0) {
                printf("[COMMAND] TC_NOW command received. Printing current thermocouple data...\n");
                print_current_temperatures();
            
            } else if (strcmp(cmd, "HELP") == 0) {
                print_help();
                print_discharge_help();

            // DC trigger commands
            } else if (strncmp(cmd, "DC_", 3) == 0) {
                if (process_discharge_command(cmd)) {
                } else {
                    printf("[ERROR] Unknown discharge command. Type DC_HELP for help.\n");
                }
            // PIO trigger commands
            } else if (strncmp(cmd, "PIO_DEBUG", 9) == 0) {
                int debug_enable;
                if (sscanf(cmd + 9, "%d", &debug_enable) == 1 && (debug_enable == 0 || debug_enable == 1)) {
                    set_pio_debug_mode(debug_enable == 1);
                } else {
                    printf("[ERROR] Invalid PIO_DEBUG command. Usage: PIO_DEBUG 0|1\n");
                }
            }
            else if (strncmp(cmd, "PIO_TRIGGER", 11) == 0) {
                int trigger_state;
                if (sscanf(cmd + 11, "%d", &trigger_state) == 1 && (trigger_state == 0 || trigger_state == 1)) {
                    set_manual_pio_trigger(trigger_state == 1);
                } else {
                    printf("[ERROR] Invalid PIO_TRIGGER command. Usage: PIO_TRIGGER 0|1\n");
                }
            }
            else if (strcmp(cmd, "PIO_TRIGGER_STATUS") == 0) {
                print_pio_trigger_status();
            }
            else if (strncmp(cmd, "RELAY", 5) == 0) {
                int relay_state;
                if (sscanf(cmd + 6, "%d", &relay_state) == 1 && (relay_state == 0 || relay_state == 1)) {
                    set_relay(relay_state);
                } else {
                    printf("[ERROR] Invalid RELAY command. Usage: RELAY 0|1\n");
                }
            } else if (strcmp(cmd, "TC_ONBOARD") == 0) {
                print_onboard_temperature();
            } else {
                printf("[ERROR] Unrecognized command: %s\n", cmd);
                printf("Type HELP for a list of commands.\n");
            }
            
            chars = 0;
            cmd[0] = '\0';
        }
    }
    
    return updated;
}