# Bluetooth Peripheral UART with I2C Sensor (Edge AI Inference Mode)

This project is a Bluetooth Low Energy (BLE) Peripheral UART sample for the **nRF54L15-DK** that integrates an **LSM6DSO** 6-axis accelerometer/gyroscope sensor via I2C, running real-time Edge AI inference.

Modifications have been made to support the **MCUboot bootloader**, **OTA DFU (Device Firmware Update) over BLE**, and **real-time Edge AI inference** using raw accelerometer data.

## 🧠 Accelerometer-based Edge AI Inference

This application performs real-time gesture classification using raw 3-axis accelerometer data from the **LSM6DSO** sensor.

* **Sampling Rate**: Acceleration values are sampled at **104Hz** and scaled to **g** units.
* **Output**: The classification results (gesture prediction probabilities and timing details) are printed to the serial console and streamed live via the BLE **Nordic UART Service (NUS)** in a clean, single-line format:
  ```text
  Predictions (DSP: X ms, Class: Y ms): idle: 0.050, wave: 0.950
  ```

### ⚙️ Edge Impulse Model Requirements
You must train and export your own model from Edge Impulse to compile this project:
1. Create a project on [Edge Impulse Studio](https://www.edgeimpulse.com/).
2. Collect data, configure your impulse, and train a classification model.
3. Go to the **Deployment** tab and select **C++ library**.
4. Download the library zip and extract its folders (`edge-impulse-sdk`, `model-parameters`, and `tflite-model`) directly into the root directory of this repository.

## Repository Structure

```text
├── .gitignore
├── CMakeLists.txt
├── Kconfig
├── Kconfig.sysbuild
├── sysbuild.conf                # Sysbuild configuration to enable MCUboot
├── prj.conf                      # Main application base configuration
├── src/
│   └── main.c                   # Application source code
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

Run the following `west` command in a terminal initialized with the nRF Connect SDK toolchain environment:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp --sysbuild --pristine
```

## How to Flash

Connect the board via J-Link and flash the merged binary:

```bash
west flash
```

## Generated Output Files

After a successful build, the following key binary files are generated in the `build/` directory:

1. **`build/merged.hex`**
   * A merged hex file containing the MCUboot bootloader and the signed initial application image.
   * Use this file for the initial flashing of your device (or for device recovery).
2. **`build/dfu_application.zip`**
   * The signed firmware update package used for OTA DFU updates over BLE.
   * Transfer this zip file to mobile applications like **nRF Connect for Mobile** or **nRF Device Firmware Update (nRF DFU)** to perform a wireless update.
