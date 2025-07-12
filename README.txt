# SESL Inverter Controller Firmware

This program is the firmware for a Raspberry Pi Pico 2 W used to control an inverter and DC-DC switching supply. It provides precise control over PWM signals, ADC monitoring, thermocouple readings, and GPIO-based discharge sequences. The firmware is designed to run on a dual-core RP2350 microcontroller, with Core 0 handling main control tasks and Core 1 dedicated to time-critical discharge PWM sequences.

---

## Features

### Core 0 (Main Control)
- **Serial Command Interface**: Allows real-time control and debugging via USB.
- **Thermocouple Monitoring**: Reads temperatures from MAX31855K thermocouple sensors via SPI.
- **ADC Monitoring**: Monitors currents and voltages for overcurrent protection (supports voltage dividers for >3.3V signals).
- **PIO PWM Control**: Updates frequency and duty cycle for inverter PWM signals with independent dual-pair control.
- **Relay Control**: GPIO-based relay switching for safety shutdown.
- **Safety Shutdown**: Detects overtemperature and overcurrent conditions and shuts down the system.

### Core 1 (Discharge PWM Control)
- **Dual-Channel PWM Discharge**: Controls synchronized PWM signals for DC-DC converter discharge at 50kHz.
- **Inverting Circuit Support**: Configurable output inversion for inverting circuit topologies.
- **High-Precision Timing**: Supports step-based sequences with microsecond resolution.
- **Manual Debug Mode**: Allows manual control of discharge triggers for testing.

---

## Communication with RP2350 Microcontroller

### Requirements
- **Serial USB Connection**: Connect to the Raspberry Pi Pico 2 W via USB for communication.
- **Terminal Software**: Use a terminal emulator (e.g., PuTTY, Tera Term) to send commands and view output.
- **Power Supply**: External power via VSYS pin (1.8V-5.5V) or 3V3 pin (exactly 3.3V).

### Serial Commands

#### Main PWM Control
- `FREQ <frequency> <duty_pair1> <duty_pair2>`: Set frequency and independent duty cycles for two phase pairs.
  - Example: `FREQ 50000 0.3 0.5` (50 kHz, Pair 1: 30%, Pair 2: 50%)
  - Example: `FREQ 100000 0.4` (100 kHz, both pairs: 40%)
  - **Note**: Frequency compensation applied automatically for PIO timing accuracy.

#### Discharge PWM Control
- `DISCHARGE_STEP <duration_ms> CH1 <d1,d2,...> CH2 <d1,d2,...>`: Program step-based discharge sequences.
  - Example: `DISCHARGE_STEP 100 CH1 0.5,0.7,0.3 CH2 0.2,0.9,0.1`
- `DISCHARGE_CSV <step_duration_ms>`: Start CSV input mode for large datasets.
  - Example:
    ```
    DISCHARGE_CSV 50
    0.5,0.2
    0.7,0.9
    0.3,0.1
    DISCHARGE_CSV_END
    ```
- `DISCHARGE_INVERT <0|1>`: Toggle output inversion for inverting circuits (default: enabled).
  - Example: `DISCHARGE_INVERT 1` (inverted mode - input 0.8 outputs 20% PWM for 80% effective)
- `DISCHARGE_STATUS`: Show the current discharge sequence and configuration.
- `DISCHARGE_VERBOSE <0|1>`: Toggle detailed step-by-step debug output.

#### Debug/Testing Commands
- `DISCHARGE_DEBUG <0|1>`: Enable or disable manual discharge trigger control.
- `DISCHARGE_TRIGGER <0|1>`: Manually activate or deactivate the discharge trigger.
- `DISCHARGE_TRIGGER_STATUS`: Show the current discharge trigger status.
- `PIO_DEBUG <0|1>`: Enable or disable manual PIO trigger control.
- `PIO_TRIGGER <0|1>`: Manually activate or deactivate the PIO trigger.
- `PIO_TRIGGER_STATUS`: Show the current PIO trigger status.

#### System Control
- `RELAY <0|1>`: Control safety relay state.
  - Example: `RELAY 1` (turn relay ON), `RELAY 0` (turn relay OFF)
- `RELAY_STATUS`: Show current relay state.

#### Thermocouple Commands
- `TC_ON <0|1>`: Enable or disable automatic thermocouple data printing.
- `TC_CSV`: Print thermocouple log as CSV.

#### Help
- `HELP`: Show a list of available commands.

---

## System Architecture

### Core Allocation
- **Core 0**: Handles thermocouple monitoring, ADC monitoring, serial commands, and PIO updates.
- **Core 1**: Dedicated to GPIO PWM discharge sequences for precise timing.

### Hardware Connections

#### Power Supply
- **VSYS (Pin 39)**: External power input (1.8V - 5.5V) - **Recommended**
- **3V3 (Pin 36)**: 3.3V power input (must be precise ±0.1V) - Use with caution
- **VBUS (Pin 40)**: 5V USB power input

#### GPIO PWM Discharge
- **GPIO 16**: CH1 PWM output (50 kHz, configurable inversion)
- **GPIO 17**: CH2 PWM output (50 kHz, configurable inversion)
- **GPIO 18**: Discharge trigger input (active LOW, internal pull-up)

#### PIO PWM Control (4-Phase with Independent Pairs)
- **GPIO 2**: PWM Phase 0 (Pair 1) - 0° phase shift
- **GPIO 3**: PWM Phase 1 (Pair 2) - 90° phase shift
- **GPIO 4**: PWM Phase 2 (Pair 1) - 180° phase shift
- **GPIO 5**: PWM Phase 3 (Pair 2) - 270° phase shift
- **GPIO 6**: PIO trigger input (active HIGH)

#### SPI Thermocouple Interface
- **GPIO 8**: SPI1 RX (MISO) - Data from MAX31855K boards
- **GPIO 10**: SPI1 SCK - Clock to MAX31855K boards
- **GPIO 9, 13, 14, 15**: Chip select pins for up to 4 thermocouple boards

#### ADC Monitoring (with Voltage Dividers)
- **ADC 0, 1, 2**: Current monitoring inputs (0-3.3V with voltage dividers for higher voltages)
- **ADC 3**: VSYS voltage monitoring (automatic 3:1 divider)

#### Safety Control
- **GPIO 22**: Relay control output (requires 5V level shifting for relay activation)

### Voltage Considerations
- **ADC Inputs**: Maximum 3.3V (use voltage dividers for higher input voltages)
- **Digital Inputs**: 3.3V logic levels (use level shifters for 5V signals)
- **GPIO Outputs**: 3.3V logic (use transistors/MOSFETs for 5V relay control)

---

## Clock Frequency Compensation

The firmware automatically detects the actual system clock frequency using `clock_get_hz(clk_sys)` instead of assuming 125MHz. This ensures:
- **Accurate PWM frequencies** regardless of clock variations
- **Correct phase relationships** (90° = 250μs at 1kHz, 2.5μs at 100kHz)
- **Precise duty cycle calculations** for both PIO and GPIO PWM systems

---

## Safety Features
- **Overtemperature Protection**: Monitors thermocouple readings and shuts down the system if temperatures exceed safe limits.
- **Overcurrent Protection**: Monitors ADC readings and shuts down the system if currents exceed safe limits.
- **Relay Safety Shutdown**: GPIO-controlled relay for emergency system isolation.
- **Voltage Monitoring**: VSYS voltage monitoring for power supply health.

---

## Example Usage

### Normal Operation
1. Connect the Raspberry Pi Pico 2 W via USB and external power via VSYS.
2. Open a terminal emulator and connect to the serial port.
3. Program the PWM discharge sequence with inversion:
   ```
   DISCHARGE_INVERT 1
   DISCHARGE_STEP 100 CH1 0.5,0.8 CH2 0.3,0.9
   ```
4. Set the PIO frequency with independent duty cycles:
   ```
   FREQ 50000 0.3 0.5
   ```
5. Control relay and monitor system:
   ```
   RELAY 1
   RELAY_STATUS
   DISCHARGE_STATUS
   ```

### Debug Mode
1. Enable manual discharge control:
   ```
   DISCHARGE_DEBUG 1
   DISCHARGE_VERBOSE 1
   ```
2. Start the discharge sequence manually:
   ```
   DISCHARGE_TRIGGER 1
   ```
3. Monitor debug output and stop manually:
   ```
   DISCHARGE_TRIGGER 0
   ```

### Inverting Circuit Configuration
For circuits that invert the PWM signal:
```
DISCHARGE_INVERT 1          # Enable inversion (default)
DISCHARGE_STEP 1000 CH1 0.8 # Input 80% → Output 20% PWM → 80% effective circuit output
```

For non-inverting circuits:
```
DISCHARGE_INVERT 0          # Disable inversion
DISCHARGE_STEP 1000 CH1 0.8 # Input 80% → Output 80% PWM → 80% effective circuit output
```

---

## Hardware Setup Tips

### External Power
- Use 5V supply connected to VSYS pin for best performance
- Ensure proper grounding between Pico and external circuits

### Signal Level Conversion
- Use voltage dividers for ADC inputs >3.3V
- Use transistors/MOSFETs for 5V relay control
- Use level shifters for 5V digital trigger signals

### Thermocouple Connections
- MAX31855K boards: SO→GPIO8, SCK→GPIO10, CS→GPIO9/13/14/15
- Power MAX31855K boards with 3.3V from Pico

---

## Notes
- Ensure proper hardware connections and voltage levels before powering the system.
- Use the `HELP` command to view all available commands.
- For debugging, use manual trigger commands and verbose mode to test individual components.
- The firmware compensates for PIO timing automatically - frequencies are accurate regardless of system clock variations.
- Default configuration assumes inverting discharge circuits - use `DISCHARGE_INVERT 0` for non-inverting circuits.

---
