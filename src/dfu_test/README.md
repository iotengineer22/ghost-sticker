# Bluetooth Peripheral UART (with DFU support for nRF54L15-DK)

This project is a Bluetooth Low Energy (BLE) Peripheral UART sample for the **nRF54L15-DK**, based on the nRF Connect SDK (NCS).

Modifications have been made to the standard sample to support the **MCUboot bootloader** and **OTA DFU (Device Firmware Update) over BLE**.

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

The files inside the `boards/` directory customize the application for the custom board modified from nRF54L15-DK:

### 1. DeviceTree Overlay (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`)
* **UART Configuration**:
  * Chosen UART for NUS (Nordic UART Service): **UART 20** (`&uart20`).
* **GPIO & LED Mappings**:
  * **LED 0**: `GPIO 2 Pin 0` (Active High)
  * **LED 1**: `GPIO 2 Pin 1` (Active High)

### 2. Kconfig Settings (`boards/nrf54l15dk_nrf54l15_cpuapp.conf`)
Contains additional configurations specifically needed for the application image when built with DFU:
* Enables flash page layout configurations.
* Configures MCUmgr SMP transport parameters for BLE OTA.

---

## Key Configurations

### 1. Application Configuration (`boards/nrf54l15dk_nrf54l15_cpuapp.conf`)
Ensures that the application is built to be chain-loaded by MCUboot and enables the MCUmgr SMP protocol server for receiving firmware packages over BLE.
```kconfig
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y
```

### 2. Sysbuild Configuration (`sysbuild.conf`)
Instructs the sysbuild meta-build system to automatically build the MCUboot bootloader image alongside the application.
```kconfig
SB_CONFIG_BOOTLOADER_MCUBOOT=y
```

---

## How to Build

Run the following `west` command in a terminal initialized with the nRF Connect SDK toolchain environment:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp --sysbuild --pristine
```

---

## Generated Output Files

After a successful build, the following key binary files are generated in the `build/` directory:

1. **`build/merged.hex`**
   * A merged hex file containing the MCUboot bootloader and the signed initial application image.
   * Use this file for the initial flashing of your device (or for device recovery).
2. **`build/dfu_application.zip`**
   * The signed firmware update package used for OTA DFU updates over BLE.
   * Transfer this zip file to mobile applications like **nRF Connect for Mobile** or **nRF Device Firmware Update (nRF DFU)** to perform a wireless update.
