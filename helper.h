/* 
 * Helper functions 
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
