// thermocouple.c
// This file contains the thermocouple reading and logging functions.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "thermocouple.h"

// Reads 32 bits from a MAX31855K on the given CS pin
uint32_t max31855k_read(uint cs_pin) {
    uint8_t rx_buf[4] = {0};
    gpio_put(cs_pin, 0); // Select
    spi_read_blocking(SPI_PORT, 0x00, rx_buf, 4);
    gpio_put(cs_pin, 1); // Deselect

    uint32_t value = (rx_buf[0] << 24) | (rx_buf[1] << 16) | (rx_buf[2] << 8) | rx_buf[3];
    return value;
}

// Converts raw data to Celsius (returns float)
float max31855k_temp_c(uint32_t value) {
    int16_t temp = (value >> 18) & 0x3FFF;
    if (temp & 0x2000) temp |= 0xC000; // Sign extend negative
    return temp * 0.25f;
}

// Initializes all thermocouple CS pins
void max31855k_init_cs_pins(const uint *cs_pins, int num) {
    for (int i = 0; i < num; ++i) {
        gpio_set_function(cs_pins[i], GPIO_FUNC_SIO);
        gpio_set_dir(cs_pins[i], GPIO_OUT);
        gpio_put(cs_pins[i], 1); // Deselect
    }
}

// Log structure and functions
void log_thermocouples(TCLogEntry *tc_log, int *log_head, int log_size, const uint *cs_pins, int num_thermocouples) {
    TCLogEntry *entry = &tc_log[*log_head];
    entry->timestamp_ms = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < num_thermocouples; ++i) {
        uint32_t raw = max31855k_read(cs_pins[i]);
        entry->temps[i] = max31855k_temp_c(raw);
    }
    *log_head = (*log_head + 1) % log_size;
}

void print_tc_log_csv(TCLogEntry *tc_log, int log_head, int log_size, int num_thermocouples) {
    printf("timestamp_ms");
    for (int i = 0; i < num_thermocouples; ++i) printf(",TC%d", i);
    printf("\n");
    for (int i = 0; i < log_size; ++i) {
        int idx = (log_head + i) % log_size;
        printf("%lu", tc_log[idx].timestamp_ms);
        for (int j = 0; j < num_thermocouples; ++j)
            printf(",%.2f", tc_log[idx].temps[j]);
        printf("\n");
    }
}