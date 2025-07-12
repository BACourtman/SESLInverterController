// shutdown.c
// This file contains the shutdown function for the system

#include "shutdown.h"
#include "pwm_control.h"
#include "thermocouple.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include <stdio.h>
#include <string.h>

void init_relay(void){
    gpio_init(SHUTDOWN_RELAY_PIN);
    gpio_set_dir(SHUTDOWN_RELAY_PIN, GPIO_OUT);
    gpio_put(SHUTDOWN_RELAY_PIN, 1);
}

void set_relay(int hilo) {
    gpio_put(SHUTDOWN_RELAY_PIN, hilo);
    printf("[INFO] Relay set to %s\n", hilo ? "ON" : "OFF");
}

void shutdown(void) {
    char cmd[16];
    printf("[ALERT] SYSTEM SHUTDOWN INITIATED\n");
    
    // 1. Set all PWM output pins low
    for (int i = 0; i < 4; ++i) {
        gpio_init(PWM_PINS[i]);
        gpio_set_dir(PWM_PINS[i], GPIO_OUT);
        gpio_put(PWM_PINS[i], 0);
    }

    // 2. Stop all PIO state machines (assuming 4 SMs)
    for (int sm = 0; sm < 4; ++sm) {
        pio_sm_set_enabled(pio0, sm, false);
    }

    // 3. Trigger external relay if used
    gpio_init(SHUTDOWN_RELAY_PIN);
    gpio_set_dir(SHUTDOWN_RELAY_PIN, GPIO_OUT);
    gpio_put(SHUTDOWN_RELAY_PIN, 0); // or 0, depending on your relay logic

    printf("!!! SYSTEM SHUTDOWN: Overcurrent or Overtemperature detected !!!\n");
    printf("To reboot, power cycle the system\n");
    printf("For TC Log send TC_CSV command\n");

    // 4. Enter an infinite loop to halt the system and only respond to log request
    while (1) {
        if (scanf("%7s", cmd) == 1 && strcmp(cmd, "TC_CSV") == 0) {
            print_tc_log_csv();
        }
        sleep_ms(100); // Sleep indefinitely
    }
}