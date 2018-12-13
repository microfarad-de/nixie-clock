/*
 * Helper functions 
 *
 * Karim Hraibi - 2018
 */

#ifndef __HELPER_H
#define __HELPER_H

#include <Arduino.h>

/*
 * Write an array to EEPROM
 */
void eepromWrite (uint16_t addr, uint8_t *buf, uint16_t bufSize); 

/*
 * Read an array from EEPROM
 */
void eepromRead (uint16_t addr, uint8_t *buf, uint16_t bufSize);

/*
 * Save a 32-bit word to EEPROM
 */
void eepromWrite32 (uint16_t addr, uint32_t val);

/*
 * Read a 32-bit word from EEPROM
 */
uint32_t eepromRead32 (uint16_t addr);

/*
 * Caluclate least significant BCD digit 
 * out of a 2-digit decimal value
 */
uint8_t dec2bcdLow (uint8_t value);

/*
 * Caluclate most significant BCD digit 
 * out of a 2-digit decimal value
 */
uint8_t dec2bcdHigh (uint8_t value);


#endif // __HELPER_H
