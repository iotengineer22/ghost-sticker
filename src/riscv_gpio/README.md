# NUS & IPC LED Monitor for Custom nRF54L15-DK Board

This project is a multi-core application designed for custom boards modified from the nRF54L15-DK.
The Host core (ARM Cortex-M33) and the Remote core (RISC-V FLPR) communicate using IPC (Inter-Processor Communication) to monitor LED statuses and notify a BLE (Bluetooth Low Energy) client.

---

## 1. System Architecture & Operation Overview

The application runs across two processing cores:

```mermaid
graph TD
    RV[Remote Core: RISC-V] -- IPC (icmsg) --> Host[Host Core: ARM]
    Host -- UART --> PC[PC Serial Console]
    Host -- BLE (NUS) --> App[Smartphone App (e.g. nRF Connect)]
```

### ① Remote Core (RISC-V / cpuflpr)
* **Role**: Controls local hardware (LEDs) and transmits status messages.
* **Operation**:
  * Toggles LED 0 and LED 1 alternately every 1 second.
  * Formats the current LED state into a string and sends it to the Host core via IPC (using the `icmsg` backend) at regular intervals.
* **BLE Packet Constraint Mitigation (Default 20-byte MTU limit)**:
  * To conform to the default BLE notification size limit of 20 bytes (excluding protocol headers), the message payload is optimized to be exactly 17 bytes (18 bytes with null terminator).
  * Message Format: `"RISC-V_LED0:1,1:0"` or `"RISC-V_LED0:0,1:1"` (where `1` means ON and `0` means OFF).

### ② Host Core (ARM Cortex-M33 / cpuapp)
* **Role**: Manages Bluetooth LE (Nordic UART Service: NUS), local UART logging, and forwards incoming remote messages.
* **Operation**:
  * Upon booting, it starts advertising BLE services under the device name `"Nordic_UART_Service"`.
  * Receives the LED status string from the Remote core via IPC, and prints it to the local UART terminal (PC serial console) as `[RISC-V] RISC-V_LED0:1,1:0\r\n`.
  * When a BLE client connects and enables notifications on the **TX Characteristic**, the Host core forwards the received messages directly to the client.

---

## 🔌 Board Configuration & Pin Mappings (`boards/`)

The Devicetree settings inside the `boards/` directory define the pins and memory mapping for multi-core communication:

### 1. DeviceTree Overlay (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`)
* **UART Configuration**:
  * Chosen UART for NUS: **UART 20** (`&uart20`).
* **IPC (Inter-Processor Communication) Configuration**:
  * Configures the **`icmsg`** (Inter-Core Messaging) backend.
  * **Shared Memory Regions**:
    * **`sram_rx`**: SRAM region at address `0x20018000` (size: `0x0800` bytes) for receiving data.
    * **`sram_tx`**: SRAM region at address `0x20020000` (size: `0x0800` bytes) for transmitting data.
  * **Mailbox (`mboxes`)**:
    * Uses the event interface (`cpuapp_vevif_rx` channel 20 and `cpuapp_vevif_tx` channel 21) to trigger interrupts between the ARM Host and RISC-V Remote cores.
* **Host Core GPIO & LED Mappings**:
  * **LED 2** (Run LED): `GPIO 2 Pin 7` (Active Low)
  * **LED 3** (Connection LED): `GPIO 1 Pin 14` (Active Low)
  * *Note: LED 0 and LED 1 are handled on the RISC-V Remote side.*

---

## 3. Build & Flash Instructions

Ensure your terminal is initialized with the nRF Connect SDK toolchain environment and you are inside the `riscv_gpio` directory.

### ① Build Command
Execute the following command to perform a pristine build:

```powershell
west build -b nrf54l15dk/nrf54l15/cpuapp --pristine -- -DCONFIG_DEBUG_THREAD_INFO=y -DSNIPPET=nordic-flpr
```

### ② Flash Command
Connect the board via J-Link and run this command to flash both the Host (ARM) and Remote (RISC-V) images:

```powershell
west flash
```

---

## 4. How to Verify Operation

1. Once flashed, open the Host serial port (COM port on Windows) using a serial terminal client (baud rate: 115200).
2. Verify that the console prints the following logs every 1 second:
   ```text
   [RISC-V] RISC-V_LED0:1,1:0
   [RISC-V] RISC-V_LED0:0,1:1
   ```
3. Open the **nRF Connect** mobile app (or another BLE terminal tool) and connect to the peripheral advertised as `Nordic_UART_Service`.
4. Locate the **Nordic UART Service** (UUID: `6e400001-...`) and tap the **triple down arrow** (Subscribe/Notify) icon on the **TX Characteristic** (UUID: `6e400003-...`) to enable notifications.
5. Verify that notification payloads like `RISC-V_LED0:1,1:0` are received and displayed in the app log periodically.
