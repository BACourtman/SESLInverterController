// thermocouple.c
// This file contains the thermocouple reading and logging functions.

#include "thermocouple.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>

#define SPI_PORT spi1

const uint CS_PINS[NUM_THERMOCOUPLES] = {9, 13, 14, 15};
TCLogEntry tc_log[LOG_SIZE];
int log_head = 0;

// Add this static variable for consecutive OTP tracking
static int otp_consecutive_count[NUM_THERMOCOUPLES] = {0};

void max31855k_init_cs_pins(void) {
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
        gpio_set_function(CS_PINS[i], GPIO_FUNC_SIO);
        gpio_set_dir(CS_PINS[i], GPIO_OUT);
        gpio_put(CS_PINS[i], 1); // Deselect
    }
    printf("[INFO] MAX31855K CS pins initialized\n");
}

uint32_t max31855k_read(uint cs_pin) {
    uint8_t rx_buf[4] = {0};
    gpio_put(cs_pin, 0); // Select
    spi_read_blocking(SPI_PORT, 0x00, rx_buf, 4);
    gpio_put(cs_pin, 1); // Deselect

    uint32_t value = (rx_buf[0] << 24) | (rx_buf[1] << 16) | (rx_buf[2] << 8) | rx_buf[3];
    return value;
}

float max31855k_temp_c(uint32_t value) {
    int16_t temp = (value >> 18) & 0x3FFF;
    if (temp & 0x2000) temp |= 0xC000; // Sign extend negative
    return temp * 0.25f;
}

void log_thermocouples(void) {
    TCLogEntry *entry = &tc_log[log_head];
    entry->timestamp_ms = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
        uint32_t raw = max31855k_read(CS_PINS[i]);
        entry->temps[i] = max31855k_temp_c(raw);
    }
    log_head = (log_head + 1) % LOG_SIZE;
}

void print_tc_log_csv(void) {
    printf("[DATA] timestamp_ms");
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) printf(",TC%d", i);
    printf("\n");
    for (int i = 0; i < LOG_SIZE; ++i) {
        int idx = (log_head + i) % LOG_SIZE;
        printf("%lu", tc_log[idx].timestamp_ms);
        for (int j = 0; j < NUM_THERMOCOUPLES; ++j)
            printf(",%.2f", tc_log[idx].temps[j]);
        printf("\n");
    }
}

// Modified function with consecutive checking
bool check_overtemperature(float temps[NUM_THERMOCOUPLES]) {
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
        if (temps[i] > OTP_LIMIT) {
            otp_consecutive_count[i]++; // Increment consecutive count
            if (otp_consecutive_count[i] >= OTP_CONSECUTIVE_THRESHOLD) {
                printf("[ALERT] CRITICAL: TC%d overtemperature for %d consecutive readings: %.2f C\n", 
                       i, otp_consecutive_count[i], temps[i]);
                return true; // Trigger shutdown
            } else {
                printf("[ALERT] WARNING: TC%d overtemperature reading %d/%d: %.2f C\n", 
                       i, otp_consecutive_count[i], OTP_CONSECUTIVE_THRESHOLD, temps[i]);
            }
        } else {
            otp_consecutive_count[i] = 0; // Reset count if temperature is normal
        }
    }
    return false;
}

// Function to print current temperatures with tags
void print_current_temperatures(void) {
    printf("[DATA] Current thermocouple readings:\n");
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
        uint32_t raw = max31855k_read(CS_PINS[i]);
        float temp = max31855k_temp_c(raw);
        printf("[DATA] TC%d: %.2f C\n", i, temp);
    }
}

