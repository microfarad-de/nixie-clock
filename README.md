# Nixie Clock

This is the C++ firmware source code of the DCF77 radio-controlled Nixie tube clock. The full description of this project can be found on www.microfarad.de/nixie-clock.

The contents of this repository are organized as an Aduino project, meant to be compiled using the Arduino IDE. 

This project uses Git submodules. In order to get its full source code, please clone this Git repository to your local workspace, then execute the follwoing command from within the repository's root directory: `git submodule update --init`.

In order to build this project, please clone or download the contents of this repository to your local workspace. Please ensure that the name of the parent directory "nixie-clock" matches the base name of `nixie-clock.ino`. Open `nixie-clock.ino` in the Arduino IDE then hit the "Verify" or "Upload" buttons.

Debugging via the Serial port can be enabled by uncommenting the `#define SERIAL_DEBUG` macro inside `nixie-clock.ino`.

This firmware has been verified using an Arduino Pro Mini compatible board based on the ATmega328P microcontroller.

Unless stated otherwise within the source file headers, please feel free to use or distribute the code or parts of it under the *GNU General Public License v3.0*.

## Prerequisites

* ATmega328P based Arduino Pro Mini, Arduino Nano or similar model
* Custom bootloader from: https://github.com/microfarad-de/bootloader

## Circuit Diagram

The circuit diagram for this device can be found under the */doc* folder or can be downloaded using the follwoing link:
https://github.com/microfarad-de/nixie-clock/raw/master/doc/nixie-clock-schematic.png
