# Bluetooth Peripheral UART with PDM Microphone (Audio Inference Mode)

> [!WARNING]
> **This program has implementation issues (pin conflicts) and the final operational confirmation on the actual hardware has not yet been completed.**

This project is a Bluetooth Low Energy (BLE) Peripheral UART sample for the **nRF54L15-DK** that integrates a **PDM microphone** (via DMIC API) and an **LSM6DSO** sensor, designed to run real-time audio classification using Edge Impulse.

## Features

* **Audio Inference**: Designed to sample audio from a PDM microphone at 16kHz and run an Edge Impulse audio classification model.
* **DFU & Bootloader Support**: Configured to build with the **MCUboot** bootloader and supports **OTA DFU (Device Firmware Update) over BLE**.

## Repository Structure

```text
├── .gitignore
├── CMakeLists.txt
├── Kconfig
├── Kconfig.sysbuild
├── sysbuild.conf                # Sysbuild configuration to enable MCUboot
├── prj.conf                      # Main application base configuration
├── project_inferencing.h        # Edge Impulse inferencing configuration
├── src/
│   └── main.cpp                 # Application source code (audio sampling and inference logic)
└── boards/
    ├── nrf54l15dk_nrf54l15_cpuapp.conf     # Additional Kconfig settings for nRF54L15-DK DFU
    └── nrf54l15dk_nrf54l15_cpuapp.overlay  # DeviceTree overlay for nRF54L15-DK
```

---

## 🔌 Board Configuration & Pin Mappings (`boards/`)

The files inside the `boards/` directory customize the Devicetree and Kconfig settings:

### 1. DeviceTree Overlay (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`)
* **UART Configuration**:
  * Chosen UART for NUS: **UART 20** (`&uart20`).
* **GPIO & LED Mappings**:
  * **LED 0**: `GPIO 2 Pin 0` (Active High)
  * **LED 1**: `GPIO 2 Pin 1` (Active High)
* **I2C 21 Interface & LSM6DSO**:
  * **SDA**: `GPIO 1 Pin 10`
  * **SCL**: `GPIO 1 Pin 11`
  * **LSM6DSO I2C Address**: `0x6a`
  * **Interrupt GPIO**: `GPIO 1 Pin 9`
* **PDM 20 Interface (`&pdm20` / `dmic_dev`)**:
  * Configures the Pulse Density Modulation interface for the microphone.
  * **CLK**: `GPIO 1 Pin 12`
  * **DIN**: `GPIO 1 Pin 13`
* **Conflicts**:
  * **Note**: Pin conflicts may exist on `GPIO 1` between the I2C, PDM, and other board peripherals. Button 0 (`&button0`) has been disabled to free up resources.

### 2. Kconfig Settings (`boards/nrf54l15dk_nrf54l15_cpuapp.conf`)
* Configures flash layout and SMP protocol buffers for DFU.

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
