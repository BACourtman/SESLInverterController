#ifndef GPIO_PWM_DISCHARGE_H
#define GPIO_PWM_DISCHARGE_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"

#define MAX_DISCHARGE_STEPS 1000
#define PWM_FREQ_HZ 10000
#define PWM_PIN_1 16
#define PWM_PIN_2 17
#define TRIGGER_PIN_DISCHARGE 18
#define NUM_PWM_CHANNELS 2

typedef struct {
    float duty_cycles[MAX_DISCHARGE_STEPS];  // Array of duty cycles
    int num_steps;
    uint32_t total_duration_ms;              // Total sequence duration
} ChannelSequence;

typedef struct {
    ChannelSequence channels[NUM_PWM_CHANNELS];
    uint32_t step_duration_ms;               // Global step duration
    uint32_t max_cycle_duration_ms;          // Longest cycle duration
    bool enabled;
    bool csv_mode;                           // Flag for CSV input mode
    int csv_step_count;                      // Current step in CSV input
    bool debug_mode;                         // Manual trigger control mode
    bool manual_trigger_state;               // Manual trigger override
} DischargeSequence;

// Global variables
extern DischargeSequence discharge_seq;
extern volatile bool trigger_active;

// Function declarations
void pwm_discharge_init(void);
void core1_entry(void);
void set_discharge_sequence_step(const char* command);

// CSV input functions
bool is_in_csv_mode(void);
void start_csv_input(uint32_t step_duration);
void process_csv_line(const char* line);
void end_csv_input(void);

void handle_discharge_trigger(void);

// Debug/Manual control functions
void set_discharge_debug_mode(bool enable);
void set_manual_discharge_trigger(bool state);
bool get_effective_discharge_trigger_state(void);
void print_discharge_trigger_status(void);
void set_discharge_verbose_mode(bool enable);

#endif