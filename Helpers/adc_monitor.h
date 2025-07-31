#ifndef ADC_MONITOR_H
#define ADC_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#define ADC_DISCONNECT_THRESHOLD 10
#define MAX_DC_CURRENT 50.0f
#define MAX_RMF_CURRENT 400.0f

void adc_monitor_init(void);
float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v);
void read_all_currents(float currents[3]);
bool check_overcurrent(float currents[3]);

#endif