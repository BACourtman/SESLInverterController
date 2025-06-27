#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "string.h"

#include "Helpers/pwm_control.h"
#include "Helpers/thermocouple.h"
#include "Helpers/adc_monitor.h"
#include "Helpers/shutdown.h"
#include "Helpers/serial_cmd.h"
#include "Helpers/GPIO_pwm_discharge.h"

// SPI Defines for MAX31855K Thermocouple Interface
#define SPI_PORT spi1
#define PIN_MISO 12
#define PIN_CS   13
#define PIN_SCK  10
#define PIN_MOSI 11

int main()
{
    // Initialize the stdio for USB
    stdio_init_all();

    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    printf("USB connected!\n");

    // Variables for frequency and duty cycle
    float frequency = 1.0e5f;   // Default 100 kHz
    float duty_cycle = 0.4f;    // Default 40%
    printf("Default Frequency: %.2f Hz, Duty Cycle: %.2f\n", frequency, duty_cycle);

    // SPI initialisation. This example will use SPI at 1MHz.
    spi_init(SPI_PORT, 1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    max31855k_init_cs_pins();
    printf("MAX31855K Thermocouple Interface Initialized\n");
    float temps_now[NUM_THERMOCOUPLES];
    
    // Initialize ADC
    adc_monitor_init();
    printf("ADC Initialized\n");
    
    // Initialize PWM Control (PIO state machines)
    pwm_control_init(frequency, duty_cycle);
    printf("PIO PWM Control Initialized\n");
    
    // Initialize GPIO PWM Discharge system
    pwm_discharge_init();
    printf("GPIO PWM Discharge System Initialized\n");
    
    // Launch Core 1 for discharge PWM control
    multicore_launch_core1(core1_entry);
    printf("Core 1 launched for discharge PWM control\n");
    
    absolute_time_t last_log = get_absolute_time();
    absolute_time_t last_print = get_absolute_time();

    // Auto TC print flag
    int auto_tc_print = 0; // 0 = OFF by default
    printf("Inverter controller ready, entering main loop\n");
    printf("Core 0: Main control loop (TC, ADC, Serial, PIO updates)\n");
    printf("Core 1: Discharge PWM sequences\n");
    printf("Type HELP for available commands\n");

    // Main loop (Core 0)
    while (true) {
        // 1. Parse serial input and update parameters if needed
        bool params_updated = process_serial_commands(&frequency, &duty_cycle, &auto_tc_print);
        
        // 1.2 Push frequency and duty cycle to PIO state machines if they are free
        process_pio_state_machines(pio0, frequency, duty_cycle);
        
        // 2. Read thermocouples
        // 2.1 Fast overtemperature protection (read every loop)
        for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
            uint32_t raw = max31855k_read(CS_PINS[i]);
            temps_now[i] = max31855k_temp_c(raw);
        }
        
        if (check_overtemperature(temps_now)) {
            printf("EMERGENCY: Overtemperature detected! Shutting down...\n");
            shutdown();
        }
        
        // 2.2 Log Thermocouple data
        if (absolute_time_diff_us(last_log, get_absolute_time()) > LOG_INTERVAL_MS * 1000) {
            last_log = get_absolute_time();
            log_thermocouples();
        }
        
        // 2.3 Print Thermocouple data every 1 second
        if (auto_tc_print && absolute_time_diff_us(last_print, get_absolute_time()) > PRINT_INTERVAL_MS * 1000) {
            last_print = get_absolute_time();
            TCLogEntry *latest = &tc_log[(log_head + LOG_SIZE - 1) % LOG_SIZE];
            printf("Latest: %lu \n", latest->timestamp_ms);
            for (int i = 0; i < NUM_THERMOCOUPLES; ++i)
                printf("TC %d: %.2f C\n", i, latest->temps[i]);
            printf("\n");
        }
        
        // 3. Read ADCs and check for overcurrent
        float currents[3];
        read_all_currents(currents);
        
        if (check_overcurrent(currents)) {
            printf("EMERGENCY: Overcurrent detected! Shutting down...\n");
            shutdown();
        }
        
        sleep_ms(5); // Adjust as needed for Core 0 loop timing
    }
}
