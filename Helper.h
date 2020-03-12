/*
 * Helper functions 
 *
 * Karim Hraibi - 2018
 */

#ifndef __HELPER_H
#define __HELPER_H

#include <Arduino.h>

void eepromWrite (uint16_t addr, uint8_t *buf, uint16_t bufSize); 

void eepromRead (uint16_t addr, uint8_t *buf, uint16_t bufSize);

void eepromWrite32 (uint16_t addr, uint32_t val);

uint32_t eepromRead32 (uint16_t addr);

uint8_t dec2bcdLow (uint8_t value);

uint8_t dec2bcdHigh (uint8_t value);


#endif // __HELPER_H

