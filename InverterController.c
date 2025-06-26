#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/pio.h"

// SPI Defines
// We are going to use SPI 1, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi1
#define PIN_MISO 12
#define PIN_CS   13
#define PIN_SCK  10
#define PIN_MOSI 11

#include "blink.pio.h"

void blink_pin_forever(PIO pio, uint sm, uint offset, uint pin, uint freq) {
    blink_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);

    printf("Blinking pin %d at %d Hz\n", pin, freq);

    // PIO counter program takes 3 more cycles in total than we pass as
    // input (wait for n + 1; mov; jmp)
    pio->txf[sm] = (125000000 / (2 * freq)) - 3;
}

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

int main()
{
    stdio_init_all();

    // SPI initialisation. This example will use SPI at 1MHz.
    spi_init(SPI_PORT, 1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    max31855k_init_cs_pins();

    // PIO Blinking example
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &blink_program);
    printf("Loaded program at %d\n", offset);

    #ifdef PICO_DEFAULT_LED_PIN
    blink_pin_forever(pio, 0, offset, PICO_DEFAULT_LED_PIN, 3);
    #else
    blink_pin_forever(pio, 0, offset, 6, 3);
    #endif

    while (true) {
        for (int i = 0; i < NUM_THERMOCOUPLES; ++i) {
            uint32_t raw = max31855k_read(CS_PINS[i]);
            float temp = max31855k_temp_c(raw);
            printf("Thermocouple %d: %.2f C\n", i, temp);
        }
        sleep_ms(1000);
    }
}
