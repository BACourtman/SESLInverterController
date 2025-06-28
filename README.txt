# SESL Inverter Controller Firmware

This program is the firmware for a Raspberry Pi Pico 2 W used to control an inverter and DC-DC switching supply. It provides precise control over PWM signals, ADC monitoring, thermocouple readings, and GPIO-based discharge sequences. The firmware is designed to run on a dual-core RP2350 microcontroller, with Core 0 handling main control tasks and Core 1 dedicated to time-critical discharge PWM sequences.

---

## Features

### Core 0 (Main Control)
- **Serial Command Interface**: Allows real-time control and debugging via USB.
- **Thermocouple Monitoring**: Reads temperatures from MAX31855K thermocouple sensors.
- **ADC Monitoring**: Monitors currents and voltages for overcurrent protection.
- **PIO PWM Control**: Updates frequency and duty cycle for inverter PWM signals.
- **Safety Shutdown**: Detects overtemperature and overcurrent conditions and shuts down the system.

### Core 1 (Discharge PWM Control)
- **Dual-Channel PWM Discharge**: Controls synchronized PWM signals for DC-DC converter discharge.
- **High-Precision Timing**: Supports step-based sequences with microsecond resolution.
- **Manual Debug Mode**: Allows manual control of discharge triggers for testing.

---

## Communication with RP2350 Microcontroller

### Requirements
- **Serial USB Connection**: Connect to the Raspberry Pi Pico 2 W via USB for communication.
- **Terminal Software**: Use a terminal emulator (e.g., PuTTY, Tera Term) to send commands and view output.

### Serial Commands

#### Main PWM Control
- `FREQ <frequency> <duty_cycle>`: Set the frequency and duty cycle for the PIO state machines.
  - Example: `FREQ 50000 0.3` (50 kHz, 30% duty cycle)

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
- `DISCHARGE_STATUS`: Show the current discharge sequence.

#### Debug/Testing Commands
- `DISCHARGE_DEBUG <0|1>`: Enable or disable manual discharge trigger control.
- `DISCHARGE_TRIGGER <0|1>`: Manually activate or deactivate the discharge trigger.
- `DISCHARGE_TRIGGER_STATUS`: Show the current discharge trigger status.
- `PIO_DEBUG <0|1>`: Enable or disable manual PIO trigger control.
- `PIO_TRIGGER <0|1>`: Manually activate or deactivate the PIO trigger.
- `PIO_TRIGGER_STATUS`: Show the current PIO trigger status.

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
#### GPIO PWM Discharge
- **GPIO 16**: CH1 PWM output (10 kHz)
- **GPIO 17**: CH2 PWM output (10 kHz)
- **GPIO 18**: Discharge trigger input (active LOW, internal pull-up)

#### PIO PWM Control
- **GPIO 2-5**: PIO PWM outputs for inverter control
  - GPIO 2: PWM Channel 1
  - GPIO 3: PWM Channel 2
  - GPIO 4: PWM Channel 3
  - GPIO 5: PWM Channel 4
- **GPIO 6**: PIO trigger input (active HIGH)

---

## Safety Features
- **Overtemperature Protection**: Monitors thermocouple readings and shuts down the system if temperatures exceed safe limits.
- **Overcurrent Protection**: Monitors ADC readings and shuts down the system if currents exceed safe limits.

---

## Example Usage

### Normal Operation
1. Connect the Raspberry Pi Pico 2 W via USB.
2. Open a terminal emulator and connect to the serial port.
3. Program the PWM discharge sequence:
   ```
   DISCHARGE_STEP 100 CH1 0.5,0.8 CH2 0.3,0.9
   ```
4. Set the PIO frequency and duty cycle:
   ```
   FREQ 50000 0.3
   ```
5. Monitor thermocouple and ADC readings for safety.

### Debug Mode
1. Enable manual discharge control:
   ```
   DISCHARGE_DEBUG 1
   ```
2. Start the discharge sequence manually:
   ```
   DISCHARGE_TRIGGER 1
   ```
3. Stop the discharge sequence manually:
   ```
   DISCHARGE_TRIGGER 0
   ```

---

## Notes
- Ensure proper hardware connections before powering the system.
- Use the `HELP` command to view all available commands.
- For debugging, use manual trigger commands to test individual components.

---

This firmware is designed for flexibility, precision, and safety in controlling inverter and DC-DC switching systems. For further assistance, refer to the source code or contact the developer.