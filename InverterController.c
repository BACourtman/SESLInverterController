#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "hardware/adc.h"
#include "string.h"

// SPI Defines
// We are going to use SPI 1, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
// These SPI pins are used for the MAX31855K thermocouple interface
#define SPI_PORT spi1
#define PIN_MISO 12
#define PIN_CS   13
#define PIN_SCK  10
#define PIN_MOSI 11

// PIO, 4 Phase Waveform Generator
// #include "phase_pwm.pio.h"

#define NUM_THERMOCOUPLES 4
const uint CS_PINS[NUM_THERMOCOUPLES] = {9, 13, 14, 15}; // Update as needed

void max31855k_init_cs_pins() {
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
        gpio_set_function(CS_PINS[i], GPIO_FUNC_SIO);
        gpio_set_dir(CS_PINS[i], GPIO_OUT);
        gpio_put(CS_PINS[i], 1); // Deselect
    }
}

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

// Logging thermocouple data
// This will log the last 60 seconds of thermocouple data at 10 Hz
// Each entry will contain a timestamp and the temperatures of all thermocouples
#define LOG_SIZE 600 // 10 Hz * 60 seconds
#define LOG_INTERVAL_MS 100 // 10 Hz = 100 ms interval
#define PRINT_INTERVAL_MS 1000 // Print every second

typedef struct {
    uint32_t timestamp_ms;
    float temps[NUM_THERMOCOUPLES];
} TCLogEntry;

TCLogEntry tc_log[LOG_SIZE];
int log_head = 0;

void log_thermocouples() {
    TCLogEntry *entry = &tc_log[log_head];
    entry->timestamp_ms = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
        uint32_t raw = max31855k_read(CS_PINS[i]);
        entry->temps[i] = max31855k_temp_c(raw);
    }
    log_head = (log_head + 1) % LOG_SIZE;
}

void print_tc_log_csv() {
    printf("timestamp_ms");
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

// Convert raw ADC value to current (adjustable V/A and offset)
#define ADC_DISCONNECT_THRESHOLD 10
static inline float adc_raw_to_current(uint16_t raw, float v_per_a, float offset_v) {
    if (raw < ADC_DISCONNECT_THRESHOLD) return 0.0f; // Considered disconnected if below threshold
    float voltage = (raw * 3.3f) / 4095.0f; // 12-bit ADC, 3.3V ref
    return (voltage - offset_v) / v_per_a;
}

// Calibration values for each channel 
const float v_per_a[3] = {0.1f, 0.1f, 0.1f};     // V/A for each channel
const float offset_v[3] = {0.0f, 0.0f, 0.0f};    // Offset voltage for each channel

// OCP Current Limits
#define MAX_DC_CURRENT 20.0f // Max DC current in Amps
#define MAX_RMF_CURRENT 400.0f // Max RMF current in Amps

// OTP Temperature Limit
#define OTP_LIMIT 90.0f // Overtemperature limit in Celsius

// Shutdown sequence
#define SHUTDOWN_RELAY_PIN 22 // Change as needed

void shutdown() {
    char cmd[16];
    printf("SYSTEM SHUTDOWN INIATED\n");
    // 1. Set all PWM output pins low (replace with your actual output pins)
    const uint PWM_PINS[4] = {2, 3, 4, 5}; // Example pins, update as needed
    for (int i = 0; i < 4; ++i) {
        gpio_init(PWM_PINS[i]);
        gpio_set_dir(PWM_PINS[i], GPIO_OUT);
        gpio_put(PWM_PINS[i], 0);
    }

    // 2. Stop all PIO state machines (assuming 4 SMs)
    // for (int sm = 0; sm < 4; ++sm) {
    //    pio_sm_set_enabled(pio0, sm, false);
    //}

    // 3. Trigger external relay if used
    gpio_init(SHUTDOWN_RELAY_PIN);
    gpio_set_dir(SHUTDOWN_RELAY_PIN, GPIO_OUT);
    gpio_put(SHUTDOWN_RELAY_PIN, 1); // or 0, depending on your relay logic

    printf("!!! SYSTEM SHUTDOWN: Overcurrent or Overtemperature detected !!!\n");
    printf("To reboot, power cycle the system\n");
    printf("For TC Log send CSV command\n");

    // 4. Enter an infinite loop to halt the system and only respond to log request
    while (1) {
        if (scanf("%7s", cmd) == 1 && strcmp(cmd, "TC_CSV") == 0) {
            print_tc_log_csv();
        }
        sleep_ms(100); // Sleep indefinitely
    }
}
// Print help message
void print_help() {
    printf("Available commands:\n");
    printf("  FREQ <frequency> <duty_cycle>   - Set frequency (0 < f < 1e6 Hz) and duty (0.0 < d < 0.5)\n");
    printf("  TC_ON 0|1                       - Toggle automatic thermocouple printout OFF (0) or ON (1)\n");
    printf("  TC_CSV                          - Print thermocouple log as CSV\n");
    printf("  HELP                            - Show this help message\n");
}

int main()
{
    stdio_init_all();

    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    printf("USB connected!\n");

    // Debugging
    for (int i = 0; i < 5; ++i){
        printf("Inverter Controller Starting...\n");
        sleep_ms(1000);
    }

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
    // printf("MAX31855K Thermocouple Interface Initialized\n");

    // Enable ADC 
    adc_init();
    adc_gpio_init(26); // ADC0
    adc_gpio_init(27); // ADC1
    adc_gpio_init(28); // ADC2
    // printf("ADC Initialized\n");
    // Enable PIO Program
    // PIO pio = pio0;
    // uint offset = pio_add_program(pio, &phase_pwm_program);
    // printf("Loaded program at %d\n", offset);
    // printf("pio0 initialized\n");
    // Main loop
    absolute_time_t last_log = get_absolute_time();
    absolute_time_t last_print = get_absolute_time();
    // printf("Time initialized\n");
    char cmd[32]; // Command buffer for serial input
    int chars = 0;
    // printf("Inverter Controller Ready\n");
    // Auto TC print flag
    int auto_tc_print = 0; // 1 = ON by default

    while (true) {
        // 1. Parse serial input
        // 1.1 Read frequency and duty cycle from serial input (to be implemented)
        float new_freq, new_duty;

        // Read command from serial input
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT && chars < sizeof(cmd) - 1) {
            if (c == '\n' || c == '\r') break;
            cmd[chars++] = (char)c;
            c = getchar_timeout_us(0);
        }
        cmd[chars] = '\0'; // Null-terminate the command string
        // Process command
        if (chars > 0) {
            if (strncmp(cmd, "FREQ", 4) == 0) { // Frequency command
                // Parse frequency and duty cycle from command
                if (sscanf(cmd + 4, "%f %f", &new_freq, &new_duty) == 2) {
                    // Range checks here
                    if (new_freq <= 0 || new_freq >= 1e6 || new_duty < 0 || new_duty > 0.5) {
                        printf("Invalid frequency or duty cycle values.\n");
                    } else {
                        frequency = new_freq;
                        duty_cycle = new_duty;
                        printf("Updated: Frequency = %.2f Hz, Duty Cycle = %.2f\n", frequency, duty_cycle);
                    }
                } else {
                    printf("Invalid FREQ command. Usage: FREQ <frequency> <duty_cycle>\n");
                }
            } else if (strncmp(cmd, "TC_ON", 5) == 0) { // Thermocouple command
                // Parse TC_ON command
                int tcon_val;
                if (sscanf(cmd + 5, "%d", &tcon_val) == 1 && (tcon_val == 0 || tcon_val == 1)) {
                    auto_tc_print = tcon_val;
                    printf("Thermocouple auto print %s\n", auto_tc_print ? "ON" : "OFF");
                } else {
                    printf("Invalid TC_ON command. Usage: TC_ON 0|1\n");
                }
            } else if (strcmp(cmd, "TC_CSV") == 0) { // CSV command to print thermocouple log
                printf("TC_CSV command received. Printing thermocouple log...\n");
                print_tc_log_csv();
            } else if (strcmp(cmd, "HELP") == 0) { // Help command
                print_help();
            } else {
                printf("Unrecognized command: %s\n", cmd);
                printf("Type HELP for a list of commands.\n");
            }
        }
        // Reset command buffer
        chars = 0;
        cmd[0] = '\0';
        // 1.2 Push frequency and duty cycle to PIO state machines if idle (to be implemented)

        // 2. Read thermocouples
        // 2.1 Fast overtemperature protection (read every loop)
        float temps_now[NUM_THERMOCOUPLES];
        for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
            uint32_t raw = max31855k_read(CS_PINS[i]);
            temps_now[i] = max31855k_temp_c(raw); // Convert raw data to Celsius
            // Check for overtemperature
            if (temps_now[i] > OTP_LIMIT) { 
                printf("Overtemperature detected on TC%d: %.2f C\n", i, temps_now[i]);
                shutdown();
            }
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
        // 3. Read ADCs 
        uint16_t adc_raw[3];
        for (int ch = 0; ch < 3; ++ch) {
            adc_select_input(ch);
            adc_raw[ch] = adc_read();
        }
        // 3.1 Convert ADC raw values to current measurements (to be implemented)
        float currents[3];
        for (int ch = 0; ch < 3; ++ch) {
            currents[ch] = adc_raw_to_current(adc_raw[ch], v_per_a[ch], offset_v[ch]);
        }
        // 3.2 Compare to max current (DC - 20 A, RMF - 400 A)(to be implemented)
        // DC 0
        if (currents[0] > MAX_DC_CURRENT){
           printf("Overcurrent detected on DC channel 1: %.2f A\n", currents[0]);
            // Trigger shutdown sequence 
           shutdown();
        }
        // DC 1
        if (currents[1] > MAX_DC_CURRENT){
            printf("Overcurrent detected on DC channel 2: %.2f A\n", currents[1]);
            // Trigger shutdown sequence 
           shutdown();
        }
        // RMF Inverter
        if (currents[2] > MAX_RMF_CURRENT){
            printf("Overcurrent detected on RMF Inverter: %.2f A\n", currents[2]);
            // Trigger shutdown sequence 
           shutdown();
        }
        sleep_ms(5); // Adjust as needed
    }
}
