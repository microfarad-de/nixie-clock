/* 
 * Nixie tube display driver 
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

#ifndef __NIXIE_H
#define __NIXIE_H

#include <Arduino.h>

/*
 * Number of Nixie tubes
 */
#define NIXIE_NUM_TUBES 6


/*
 * Nixie tube digit structure
 */
struct NixieDigit_s {
  uint8_t value = 0;      /* BCD value */
  bool    blank = false;  /* blank digit */
  bool    comma = false;  /* decimal point state */
  bool    blink = false;  /* enable blinking */
};


/*
 * Main class
 */
class NixieClass {

  public:

    /*
     * Initialize the hardware
     * Parameters:
     *   anodePin0..5 : anode control pins
     *   bcdPin0..5   : pins connected to the BCD to decimal anode driver decoder chip (74141, K155ID1)
     *   commaPin     : pin connected to the decimal point symbol
     *   digits       : pointer to the Nixie digits array
     *   numDigits    : number of Nixie digits
     *   brightness   : default brightness value (0.255)
     */
    void initialize (
            uint8_t anodePin0,
            uint8_t anodePin1,
            uint8_t anodePin2,
            uint8_t anodePin3,
            uint8_t anodePin4,
            uint8_t anodePin5,
            uint8_t bcdPin0,
            uint8_t bcdPin1,
            uint8_t bcdPin2,
            uint8_t bcdPin3,
            uint8_t commaPin,
            NixieDigit_s *digits,
            uint8_t numDigits,
            uint8_t brightness = 255
            );

    /* 
     * Set the pointer to the Nixie digits structure
     * Parameters:
     *   digits    : array of Nixie tube display digits
     *   numDigits : number of Nixie digits
     */
    void setDigits (NixieDigit_s *digits, uint8_t numDigits);
    
    /* 
     * Refresh the display
     * This function must be called from within a very fast loop
     */
    void refresh (void);

    /*
     * Set display brightness
     * Parameters:
     *   brightness : 0..99
     */
    void setBrightness (uint8_t brightness);
    
    /*
     * Force blink all digits disregarding the individual digit selecten
     * Parameters:
     *   enable : enable/disable blinking
     */
    void blinkAll (bool enable);
    
    /*
     * Blink all digits once
     */
    void blinkOnce (void);

    /*
     * Resets the phase of digit blinking
     */
    void resetBlinking (void);
    
    /*
     * Trigger the "Slot Machine" effect
     */
    void slotMachine (void);

    /*
     * Exectue cathode poisoniong prevention sequence
     */
    void cathodePoisonPrevent (void);

    /*
     * Scroll once through the digit buffer
     */
    void scroll (void);

    /*
     * Cancel any ongoing scrolling activity
     */
    void cancelScroll (void);

    /*
     * Convert a decimal value to Nixie digits BCD format
     * Parameters:
     *   value      : value to be converted
     *   output     : pointer to the Nixie digits array
     *   outputSize : size of the Nixie digits array
     *   numDigits  : number of BCD digits to be converted
     */
    void dec2bcd (uint32_t value, NixieDigit_s *output, uint8_t outputSize, uint8_t numDigits);

    /*
     * Reset a Nixie Digit array
     * Parameters:
     *   output     : pointer to the Nixie digits array
     *   outputSize : size of the Nixie digits array
     */
    void resetDigits (NixieDigit_s *output, uint8_t outputSize);

    /*
     * Temporarily Blank the display by turning-off all the Anodes
     * will re-activate the next time refresh() is called
     */
    void blank (void);

    /*
     * Permanently Enable/disable display output
     * Parameters:
     *   enbale : enable/disable display
     */
    void enable (bool enable);

    /*
     * Tells if display is enabled or not
     */
    volatile bool enabled = true;

    /*
     * Pointer to the Nixie digits array
     */
    NixieDigit_s *digits;
    
    /*
     * Number of Nixie digits
     */
    uint8_t numDigits;

    /*
     * Activate decimal point of a specific digit 
     * disregarding the scroll effect
     */
    bool comma[NIXIE_NUM_TUBES] = { false };

    /*
     * Status of the CPP feature
     */
    bool cppEnabled = false;

  private:
    uint8_t anodePin [NIXIE_NUM_TUBES];
    uint8_t bcdPin [4];
    uint8_t commaPin;
    uint32_t digitOnDuration;
    uint32_t lastTs = 0;
    uint32_t dimFactor = 0;
    uint8_t digit = 0;
    bool blinkAllEnabled = false;
    uint32_t blinkTs = 0;
    bool blinkFlag = false;
    int8_t blinkCount = 0;
    bool slotMachineEnabled[NIXIE_NUM_TUBES] = { false };
    uint32_t slotMachineTs[NIXIE_NUM_TUBES] = { 0 };
    uint8_t slotMachineCnt[NIXIE_NUM_TUBES] = { 0 };
    uint32_t slotMachineDelay[NIXIE_NUM_TUBES] = { 0 };
    uint8_t slotMachineCntStart[NIXIE_NUM_TUBES] = {  0, 11,  5, 13,  9, 15 };
    uint8_t slotMachineCntMax[NIXIE_NUM_TUBES]   = { 20, 50, 30, 60, 40, 70 };
    uint32_t cppTs = 0;
    uint8_t cppCnt = 0;
    uint32_t scrollTs = 0;
    int8_t scrollOffset = 0;
};





/*
 * Nixie display class as singleton
 */
extern NixieClass Nixie;


#endif // __NIXIE_H
