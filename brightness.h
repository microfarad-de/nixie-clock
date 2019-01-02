/* 
 * Dynamic display brightness adjustment algorithm 
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

#ifndef __BRIGHTNESS_H
#define __BRIGHTNESS_H

#include <Arduino.h>

/*
 * Size in bytes of the brightness lookup table (LUT)
 */
#define BRIGHTNESS_LUT_SIZE 64                   


/*
 * Brightness adjustment class
 */
class BrightnessClass {

  public:
    /*
     * Initial initialization
     * Parameters:
     *   eepromAddr   : lookup table address within EEPROM 
     *                  range = 0..(EEPROM.length() - BRIGHTNESS_LUT_SIZE)
     *   boostPin     : digital pin connected to the brightness boost circuitry
     *   boostEnabled : enables the brightness boost feature
     */
    void initialize (uint16_t eepromAddr, uint8_t boostPin = 0);

    /*
     * Enables/disables the auto brightness feature
     * Parameters:
     *   enable : true/false
     */
    void autoEnable (bool enable); 

    /*
     * Enables/disables the boost feature
     * Parameters:
     *   enable : true/false
     */
    void boostEnable (bool enable); 

    /*
     * Turn-off brightness boost circuitry
     */
    void boostDeactivate (void);

    /*
     * Apply new value from light sensor 
     * Parameters: 
     *   value : light sensor value [0..1023] (0 = brightest, 1023 = darkest)
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t lightSensorUpdate (int16_t value);

    /*
     * Increase brightness by 1 increment
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t increase (void);

    /*
     * Decrease brightness by 1 increment
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t decrease (void);

    /*
     * Set the brightneess temporarily to the maximum value
     * does not update the LUT
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t maximum (void);

    /*
     * Write-back the lookup table into EEPROM
     */
    void eepromWrite (void);
    
    
  private:
    void interpolate (void);                    // Interpolate the entries of the LUT
    uint8_t boost (uint8_t value);              // controls the brightness boost circuitry      
    uint16_t eepromAddr;                        // LUT within EEPROM
    uint8_t boostPin;                           // digital pin for controlling the boost circuitry
    bool boostEnabled;                          // enables the boost feature
    bool autoEnabled;                           // enables the auto brightness feature
    uint8_t lutIdx;                             // index of the brightness LUT element
    uint8_t lut[BRIGHTNESS_LUT_SIZE] = { 0 };   // brightness lookup table    
  
};

/*
 * Brightness control object as a singleton
 */
extern BrightnessClass Brightness;

#endif // __BRIGHTNESS_H
