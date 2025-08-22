#ifndef ADC_MONITOR_H
#define ADC_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#define ADC_DISCONNECT_THRESHOLD 150
#define MAX_DC_CURRENT 100.0f
#define MAX_RMF_CURRENT 600.0f // Maximum current in amperes
#define OCP_CONSECUTIVE_THRESHOLD 5  // Number of consecutive readings needed to trigger OCP

void adc_monitor_init(void);
float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v);
void read_all_currents(float currents[3]);
bool check_overcurrent(float currents[3]);
void print_adc_readings(void);

#endif