/*
 * Dynamic display brightness adjustment algorithm 
 *
 * Karim Hraibi - 2018
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
     *   eepromAddr : lookup table address within EEPROM 
     *                range = 0..(EEPROM.length() - BRIGHTNESS_LUT_SIZE)
     */
    void initialize (uint16_t eepromAddr);

    /*
     * Apply new value from light sensor 
     * range = 0..1023 (0 = brightest, 1023 = darkest)
     */
    uint8_t lightSensorUpdate (int16_t value);

    /*
     * Increase brightness by 1 or more increments
     * Parameters:
     *   steps : number of increments to increase [1..255]
     */
    uint8_t increase (uint8_t steps = 1);

    /*
     * Decrease brightness by 1 or more increments
     * Parameters:
     *   steps : number of increments to decrease [1..255]
     */
    uint8_t decrease (uint8_t steps = 1);

    /*
     * Write-back the lookup table into EEPROM
     */
    void eepromWrite (void);
    
    
  private:
    void interpolate (void);                    // Interpolate the entries of the LUT
    uint16_t eepromAddr;                        // LUT within EEPROM
    uint8_t lutIdx = 0;                         // index of the brightness LUT element
    uint8_t lut[BRIGHTNESS_LUT_SIZE] = { 0 };   // brightness lookup table    
  
};

/*
 * Brightness control object as a singleton
 */
extern BrightnessClass Brightness;

#endif // __BRIGHTNESS_H

