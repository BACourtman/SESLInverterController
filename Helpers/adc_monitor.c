// adc_monitor.c
// This file contains the ADC monitoring functions

#include "adc_monitor.h"
#include "hardware/adc.h"
#include <stdio.h>

// Calibration values for each channel
static const float R1 = 2800 ; // Resistor 1 value in ohms
static const float R2 = 5100 ; // Resistor 2 value in ohms
static const float v_per_a[3] = {2.5e-3, 2.5e-3, 1.25e-4};     // V/A for each channel
static const float offset_v[3] = {2.5*R1/(R1+R2), 2.5*R1/(R1+R2), 2.5*R1/(R1+R2)};    // Offset voltage for each channel

void adc_monitor_init(void) {
    adc_init();
    adc_gpio_init(26); // ADC0
    adc_gpio_init(27); // ADC1
    adc_gpio_init(28); // ADC2
}

float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v) {
    if (raw < ADC_DISCONNECT_THRESHOLD) return 0.0f; // Considered disconnected if below threshold
    float voltage = (raw * 3.3f) / 4095.0f; // 12-bit ADC, 3.3V ref
    return (voltage - offset_v) / v_per_a;
}

void read_all_currents(float currents[3]) {
    uint16_t adc_raw[3];
    for (int ch = 0; ch < 3; ++ch) {
        adc_select_input(ch);
        adc_raw[ch] = adc_read();
        currents[ch] = adc_raw_to_current(adc_raw[ch], v_per_a[ch], offset_v[ch]);
    }
}

bool check_overcurrent(float currents[3]) {
    // DC 0
    if (currents[0] > MAX_DC_CURRENT) {
        printf("[ALERT] Overcurrent detected on DC channel 1: %.2f A\n", currents[0]);
        return true;
    }
    // DC 1
    if (currents[1] > MAX_DC_CURRENT) {
        printf("[ALERT] Overcurrent detected on DC channel 2: %.2f A\n", currents[1]);
        return true;
    }
    // RMF Inverter
    if (currents[2] > MAX_RMF_CURRENT) {
        printf("[ALERT] Overcurrent detected on RMF Inverter: %.2f A\n", currents[2]);
        return true;
    }
    return false;
}
