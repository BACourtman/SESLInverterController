#ifndef THERMOCOUPLE_H
#define THERMOCOUPLE_H

#include <stdint.h>
#include <stdbool.h>  // Add this line
#include "pico/stdlib.h"

#define NUM_THERMOCOUPLES 4
#define LOG_SIZE 600
#define LOG_INTERVAL_MS 100
#define PRINT_INTERVAL_MS 1000
#define OTP_LIMIT 90.0f
#define OTP_CONSECUTIVE_THRESHOLD 2 // Number of consecutive readings required to trigger shutdown

typedef struct {
    uint32_t timestamp_ms;
    float temps[NUM_THERMOCOUPLES];
} TCLogEntry;

extern const uint CS_PINS[NUM_THERMOCOUPLES];
extern TCLogEntry tc_log[LOG_SIZE];
extern int log_head;

void max31855k_init_cs_pins(void);
uint32_t max31855k_read(uint cs_pin);
float max31855k_temp_c(uint32_t value);
void log_thermocouples(void);
void print_tc_log_csv(void);
bool check_overtemperature(float temps[NUM_THERMOCOUPLES]);
void print_current_temperatures(void);

#endif