# Bluetooth: Peripheral LBS (LED Button Service)

This sample demonstrates how to implement the Nordic LED Button Service (LBS) on a BLE peripheral device using the nRF Connect SDK (NCS).

## Overview

The Peripheral LBS sample allows you to:
1. Transmit the button state (pressed or released) from your development kit to a connected central device (e.g., smartphone).
2. Control the state of an LED on the development kit remotely from the connected device.

### User Interface Behavior
* **LED 1 (or LED 0 on nRF54)**: Blinks (2-second period, 50% duty cycle) when the device is advertising.
* **LED 2 (or LED 1 on nRF54)**: Lit when the device is connected.
* **LED 3 (or LED 2 on nRF54)**: Lit when the LED is controlled remotely from the app.
* **Button 1 (or Button 0 on nRF54)**: Sends notifications indicating "pressed" or "released".

## Requirements

* A supported development board (such as `nrf54l15dk/nrf54l15/cpuapp`).
* A smartphone or tablet running a compatible app (e.g., **nRF Connect for Mobile** or **nRF Blinky**).

## How to Build

Build the sample for your target board using:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp --pristine
```

### Minimal Build Option
To demonstrate reduced code size and RAM usage, you can build with a minimal configuration:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp -- -DFILE_SUFFIX=minimal
```

## How to Flash

Connect the board via J-Link and flash it:

```bash
west flash
```

## Testing with nRF Connect for Mobile

1. Install and launch the **nRF Connect for Mobile** app on your phone.
2. Connect to the advertising device (advertised name: `Nordic_LBS`).
3. Under the **Nordic LED Button Service** (LBS), enable notifications for the Button characteristic.
4. Press/release the button on your kit and observe the received notifications.
5. Write `ON` or `OFF` values to the LED characteristic to control the LED on the board.
