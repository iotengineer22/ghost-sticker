# Blinky (Dual LED Alternating Toggle)

This project is a modified version of the standard Zephyr **Blinky** sample. It alternately toggles two LEDs (LED0 and LED1) at a 1-second interval and prints the status to the serial console.

## Overview

The application demonstrates how to:
1. Retrieve GPIO pin specifications for `led0` and `led1` from the Devicetree.
2. Configure the GPIO pins as active-high/low outputs.
3. Toggle both LEDs in an infinite loop.
4. Output the current LED status to the console.

## Requirements

Your board's Devicetree must define:
* An LED configured with the `led0` alias.
* An LED configured with the `led1` alias.

## How to Build

Use the `west` command-line tool to build the application for your target board (e.g., `nrf54l15dk/nrf54l15/cpuapp`):

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp --pristine
```

## How to Flash

Connect your board and flash the compiled binary using:

```bash
west flash
```

## Expected Output

Once flashed, the LEDs on your board will blink alternately, and the console will output:

```text
LED0 state: ON, LED1 state: OFF
LED0 state: OFF, LED1 state: ON
LED0 state: ON, LED1 state: OFF
```
