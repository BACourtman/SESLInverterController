// adc_monitor.c
// This file contains the ADC monitoring functions

#include "adc_monitor.h"
#include "hardware/adc.h"
#include <math.h>
#include <stdio.h>

// Calibration values for each channel
static const float R1 = 2800 ; // Resistor 1 value in ohms
static const float R2 = 1500 ; // Resistor 2 value in ohms
static const float gain = 5.0/0.512 ; // Gain factor for current sensor
static const float scalefactor = R1/(R1 + R2); // Voltage divider scaling
static const float v_per_a[3] = {gain*2.5e-3*scalefactor, gain*2.5e-3*scalefactor, gain*1.25e-4*scalefactor};     // V/A for each channel
static const float offset_v[3] = {2.5*scalefactor, 2.5*scalefactor, 2.5*scalefactor};    // Offset voltage for each channel
static uint8_t overcurrent_counters[3] = {0, 0, 0};  // Track consecutive overcurrent events for each channel

void adc_monitor_init(void) {
    adc_init();
    adc_gpio_init(26); // ADC0
    adc_gpio_init(27); // ADC1
    adc_gpio_init(28); // ADC2
}

float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v) {
    if (raw < ADC_DISCONNECT_THRESHOLD) return 0.0f; // Considered disconnected if below threshold
    float voltage = (raw * 3.3f) / 4095.0f; // 12-bit ADC, 3.3V ref
    return fabs((voltage - offset_v) / v_per_a);
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
    bool ocp_triggered = false;

    // DC 0
    if (currents[0] > MAX_DC_CURRENT) {
        overcurrent_counters[0]++;
        if (overcurrent_counters[0] >= OCP_CONSECUTIVE_THRESHOLD) {
            printf("[ALERT] Overcurrent detected on DC channel 1: %.2f A\n", currents[0]);
            ocp_triggered = true;
        }
    } else {
        overcurrent_counters[0] = 0;
    }

    // DC 1
    if (currents[1] > MAX_DC_CURRENT) {
        overcurrent_counters[1]++;
        if (overcurrent_counters[1] >= OCP_CONSECUTIVE_THRESHOLD) {
            printf("[ALERT] Overcurrent detected on DC channel 2: %.2f A\n", currents[1]);
            ocp_triggered = true;
        }
    } else {
        overcurrent_counters[1] = 0;
    }

    // RMF Inverter
    if (currents[2] > MAX_RMF_CURRENT) {
        overcurrent_counters[2]++;
        if (overcurrent_counters[2] >= OCP_CONSECUTIVE_THRESHOLD) {
            printf("[ALERT] Overcurrent detected on RMF Inverter: %.2f A\n", currents[2]);
            ocp_triggered = true;
        }
    } else {
        overcurrent_counters[2] = 0;
    }

    return ocp_triggered;
}

void print_adc_readings(void) {
    float currents[3];
    uint16_t adc_raw[3];
    float voltages[3];
    
    for (int ch = 0; ch < 3; ++ch) {
        adc_select_input(ch);
        adc_raw[ch] = adc_read();
        voltages[ch] = (adc_raw[ch] * 3.3f) / 4095.0f;
        currents[ch] = adc_raw_to_current(adc_raw[ch], v_per_a[ch], offset_v[ch]);
    }
    
    printf("\n=== ADC Readings ===\n");
    printf("Channel | Voltage (V) | Current (A)\n");
    printf("--------------------------------\n");
    printf("DC0     | %7.3f    | %7.3f\n", voltages[0], currents[0]);
    printf("DC1     | %7.3f    | %7.3f\n", voltages[1], currents[1]);
    printf("RMF     | %7.3f    | %7.3f\n", voltages[2], currents[2]);
    printf("================================\n\n");
}


