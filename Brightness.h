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
     *   eepromAddr   : lookup table address within EEPROM 
     *                  range = 0..(EEPROM.length() - BRIGHTNESS_LUT_SIZE)
     *   boostPin     : digital pin connected to the brightness boost circuitry
     *   boostEnabled : enables the brightness boost feature
     */
    void initialize (uint16_t eepromAddr, uint8_t boostPin = 0);

    /*
     * Apply new value from light sensor 
     * Parameters: 
     *   value : light sensor value [0..1023] (0 = brightest, 1023 = darkest)
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t lightSensorUpdate (int16_t value);

    /*
     * Increase brightness by 1 or more increments
     * Parameters:
     *   steps : number of increments to increase [1..99]
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t increase (uint8_t steps = 1);

    /*
     * Decrease brightness by 1 or more increments
     * Parameters:
     *   steps : number of increments to decrease [1..99]
     * Returns:
     *   brightness value [0..99]
     */
    uint8_t decrease (uint8_t steps = 1);

    /*
     * Write-back the lookup table into EEPROM
     */
    void eepromWrite (void);

    /*
     * Turn-off brightness boost circuitry
     */
    void boostDeactivate (void);

    /*
     * Enables/disables the boost feature
     * Parameters:
     *   enable : true/false
     */
    void boostEnable (bool enable); 
    
  private:
    void interpolate (void);                    // Interpolate the entries of the LUT
    uint8_t boost (uint8_t value);              // controls the brightness boost circuitry      
    uint16_t eepromAddr;                        // LUT within EEPROM
    uint8_t boostPin;                           // digital pin for controlling the boost circuitry
    bool boostEnabled;                          // enables the boost feature
    uint8_t lutIdx = 0;                         // index of the brightness LUT element
    uint8_t lut[BRIGHTNESS_LUT_SIZE] = { 0 };   // brightness lookup table    
  
};

/*
 * Brightness control object as a singleton
 */
extern BrightnessClass Brightness;

#endif // __BRIGHTNESS_H

