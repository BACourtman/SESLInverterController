#pragma once
#include "pico/stdlib.h"

// Initialize ADC channels (pass array of GPIO pins and number of channels)
void adc_monitor_init(const uint *adc_pins, int num_channels);

// Read all ADC channels into adc_raw array (must be at least num_channels in size)
void adc_monitor_read_all(uint16_t *adc_raw, int num_channels);

// Convert raw ADC value to current (V/A and offset are calibration parameters)
float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v);