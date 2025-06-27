#pragma once
#include "pico/stdlib.h"

// Structure for logging thermocouple data
typedef struct {
    uint32_t timestamp_ms;
    float temps[4]; // Adjust size as needed for your number of thermocouples
} TCLogEntry;

// SPI port used for thermocouples (define in your main or CMake)
#ifndef SPI_PORT
#define SPI_PORT spi1
#endif

uint32_t max31855k_read(uint cs_pin);
float max31855k_temp_c(uint32_t value);
void max31855k_init_cs_pins(const uint *cs_pins, int num);

void log_thermocouples(TCLogEntry *tc_log, int *log_head, int log_size, const uint *cs_pins, int num_thermocouples);
void print_tc_log_csv(TCLogEntry *tc_log, int log_head, int log_size, int num_thermocouples);