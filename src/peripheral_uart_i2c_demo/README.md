# Bluetooth Peripheral UART with I2C Sensor (LSM6DSO 1Hz Demo Mode)

This project is a Bluetooth Low Energy (BLE) Peripheral UART sample for the **nRF54L15-DK** that integrates an **LSM6DSO** 6-axis accelerometer/gyroscope sensor via I2C.

This version is configured in **1Hz Demo Mode** (`BLE_DEMO_MODE_1HZ` set to `1`). It prints raw accelerometer data locally at 104Hz, but transmits the latest sample over BLE **Nordic UART Service (NUS)** once per second (1Hz) to conserve bandwidth.

## Features

* **1Hz BLE Streaming**: Sends the latest sensor sample once every 1 second in a human-readable text format: `x,y,z=0.01G,-0.02G,0.99G`.
* **Local Console Output**: Prints raw data at 104Hz to the local serial console.
* **DFU & Bootloader Support**: Includes configuration for the **MCUboot** bootloader and supports **OTA DFU (Device Firmware Update) over BLE**.

## Repository Structure

```text
├── .gitignore
├── CMakeLists.txt
├── Kconfig
├── Kconfig.sysbuild
├── sysbuild.conf                # Sysbuild configuration to enable MCUboot
├── prj.conf                      # Main application base configuration
├── src/
│   └── main.c                   # Application source code (1Hz Demo logic)
└── boards/
    ├── nrf54l15dk_nrf54l15_cpuapp.conf     # Additional Kconfig settings for nRF54L15-DK DFU
    └── nrf54l15dk_nrf54l15_cpuapp.overlay  # DeviceTree overlay for nRF54L15-DK
```

---

## 🔌 Board Configuration & Pin Mappings (`boards/`)

The files inside the `boards/` directory customize the Devicetree and Kconfig settings for this project:

### 1. DeviceTree Overlay (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`)
* **UART Configuration**:
  * Chosen UART for NUS (Nordic UART Service): **UART 20** (`&uart20`).
* **GPIO & LED Mappings**:
  * **LED 0**: `GPIO 2 Pin 0` (Active High)
  * **LED 1**: `GPIO 2 Pin 1` (Active High)
* **I2C 21 Interface (`&i2c21`) & LSM6DSO Sensor**:
  * Configures the `i2c21` peripheral for communication with the sensor.
  * **SDA**: `GPIO 1 Pin 10`
  * **SCL**: `GPIO 1 Pin 11`
  * **Clock Speed**: Standard I2C speed.
  * **LSM6DSO Sensor Node (`lsm6dso@6a`)**:
    * **I2C Address**: `0x6a`
    * **Interrupt GPIO Pin**: `GPIO 1 Pin 9` (Active High)

### 2. Kconfig Settings (`boards/nrf54l15dk_nrf54l15_cpuapp.conf`)
* Activates configurations for MCUboot and OTA DFU support.
* Optimizes buffer configurations for the SMP protocol.

---

## How to Build

Run the following `west` command in your nRF Connect SDK environment:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp --sysbuild --pristine
```

## How to Flash

Connect the board via J-Link and flash the merged binary:

```bash
west flash
```

## Generated Output Files

1. **`build/merged.hex`**: Contains MCUboot bootloader and the application. Use this for the initial J-Link flashing.
2. **`build/dfu_application.zip`**: The signed firmware update package used for OTA DFU over BLE.
