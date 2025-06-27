// adc_monitor.c
// This file contains the ADC monitoring functions.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "adc_monitor.h"

// Initialize ADC channels (GPIO 26, 27, 28 for ADC0, ADC1, ADC2)
void adc_monitor_init(const uint *adc_pins, int num_channels) {
    adc_init();
    for (int i = 0; i < num_channels; ++i) {
        adc_gpio_init(adc_pins[i]);
    }
}

// Read all ADC channels into adc_raw array
void adc_monitor_read_all(uint16_t *adc_raw, int num_channels) {
    for (int ch = 0; ch < num_channels; ++ch) {
        adc_select_input(ch);
        adc_raw[ch] = adc_read();
    }
}

// Convert raw ADC value to current (adjustable V/A and offset)
float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v) {
    const float ADC_DISCONNECT_THRESHOLD = 10.0f;
    if (raw < ADC_DISCONNECT_THRESHOLD) return 0.0f; // Considered disconnected if below threshold
    float voltage = (raw * 3.3f) / 4095.0f; // 12-bit ADC, 3.3V ref
    return (voltage - offset_v) / v_per_a;
}