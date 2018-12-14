# Nixie Clock

This is the C++ firmware source code of the DCF77 radio-controlled Nixie Clock. The full description on of this project can be found on www.microfarad.de/nixie-clock.

The contents of this repository are organized as an Aduino project, meant to be compiled using the Arduino IDE.

In order to build this project, please clone or download the contents of this repository to your local workspace. Please ensure that the name of the parent directory "nixie-clock" matches the base name of "nixie-clock.ino". Open "nixie-clock.ino" in the Arduino IDE then hit the "Verify" or "Upload" buttons.

Debugging via the Serial port can be enabled by uncommenting the "#define SERIAL_DEBUG" macro inside "nixie-clock.ino".

This firmware has been verified using an Arduino Pro Mini compatible board based on the ATmega328P microcontroller.
