/* 
 * Class for decoding the DCF77 signal 
 *
 * Implemented using the "Pollin DCF1" module with 
 * with inverted output signal (bit starts on falling edge).
 *
 * This source file is part of the Nixie Clock Arduino firmware
 * found under http://www.github.com/microfarad-de/nixie-clock
 * 
 * Please visit:
 *   http://www.microfarad.de
 *   http://www.github.com/microfarad-de
 *   
 * Copyright (C) 2019 Karim Hraibi (khraibi at gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DCF_H
#define __DCF_H

#include <Arduino.h>
#include <time.h>

/*
 * Total number of bits in a DCF77 word
 */
#define DCF_BIT_COUNT 60

/*
 * DCF bit values
 */
enum DcfBit_e {
  DCF_BIT_LOW = 0,
  DCF_BIT_HIGH = 1,
  DCF_BIT_SYNC = 2,
  DCF_BIT_NONE = 3
};


/*
 * Main class
 */
class DcfClass {
  public:
    /*
     * Initialize the DCF receiver
     * Parameters:
     *   dcfPin     : interrupt-enabled input pin connected to the DCF module (pin 2 or 3 on ATmega328p)
     *   bitStart   : whether DCF77 bits start with a rising or falling edge, set to RISING or FALLING.
     *   dcfPinMode : whether to use the internal pullup resitor, set to INPUT or INPUT_PULLUP
     */
    void initialize (uint8_t dcfPin, uint8_t bitStart=FALLING, uint8_t dcfPinMode=INPUT);

    /*
     * Pause DCF reception by disabling the DCF77 signal interrupt
     */
    void pauseReception (void);

    /*
     * Resume DCF reception by re-enabling the DCF77 signal  interrupt
     */
    void resumeReception (void);

    
    /*
     * Read the DCF time
     * This method must be called in a fast loop until it returns 0 for success.
     * Upon success, the current time is stored in the DCF.currentTm variable.
     * Parameters:
     *   void
     * Return value:
     *    0     : success, DCF.currentTm updated with a new time value
     *    1..13 : decoded values out of range, DCF.currentTm contains invalid data!
     *   21..23 : parity check failed, DCF.currentTm contains invalid data!
     *   31     : too many bits detected
     *   32     : too few bits detected
     *   33     : a new bit has been detected, value can be found in DCF.lastBit
     *   41     : detection in progress
     */
    uint8_t getTime (void);

    /*
     * Current time is stored in this variable once getTime succeeds
     */
    struct tm currentTm;

    /*
     * Stores the value of the last received DCF bit
     * This variable can be safely reset from outside this class
     * Example usage: to control the led blinking from outside this class
     */
    DcfBit_e lastBit = DCF_BIT_NONE;

    /*
     * Stores the event that triggered the last interrupt
     * This variable can be safely reset from outside this class
     * Possible values: HIGH, LOW
     */
    volatile uint8_t lastIrqTrigger;

    /*
     * Index of the last received DCF bit
     * This variable can be safely reset from outside this class
     * Used for debug purposes
     */
    uint32_t lastIdx = 0;
    
    /*
     * Private variables used by the main ISR, thus need to be declared as public.
     */
    volatile uint8_t dcfPin;
    volatile uint32_t startEdgeTs = 0;
    volatile DcfBit_e dcfBit = DCF_BIT_NONE; 
    volatile uint8_t rxFlag = 0;
    volatile uint8_t startEdge;
    
  private:
    /*
     * Private variables
     */
    bool isConfigured = false;
    uint8_t interrupt;
    uint8_t verify (void);
    uint8_t ledPin;
    uint8_t idx = 0;
    uint8_t bits[DCF_BIT_COUNT];
};

/*
 * DCF class is instantiated as a singleton
 */
extern DcfClass Dcf;

#endif // __DCF_H
