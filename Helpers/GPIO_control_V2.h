#ifndef GPIO_CONTROL_V2_H
#define GPIO_CONTROL_V2_H

#include <stdbool.h>
#include <stdint.h>

// Function declarations
void discharge_system_init(void);
bool process_discharge_command(const char* command);
void print_discharge_help(void);
bool is_csv_mode_active(void);
bool is_sequence_running(void);

// Internal functions (shouldn't be called directly)
void core1_discharge_loop(void);
void discharge_pwm_init(void);

#endif // GPIO_CONTROL_V2_H