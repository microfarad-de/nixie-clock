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

#include "Helper.h"
#include <EEPROM.h>



void eepromWrite (uint16_t addr, uint8_t *buf, uint16_t bufSize) {
  uint8_t i, v;

  for (i = 0; i < bufSize && i < EEPROM.length (); i++) {
    v = EEPROM.read (addr + i);
    if ( buf[i] != v) EEPROM.write (addr + i, buf[i]);
  }
}


void eepromRead (uint16_t addr, uint8_t *buf, uint16_t bufSize) {
  uint8_t i;

  for (i = 0; i < bufSize; i++) {
    buf[i] = EEPROM.read (addr + i);
  }
}


void eepromWrite32 (uint16_t addr, uint32_t val) {
  uint8_t i, v;
  uint16_t bytes[4];
  bytes[0] = (uint8_t)( val        & 0xFF);
  bytes[1] = (uint8_t)((val >>  8) & 0xFF);
  bytes[2] = (uint8_t)((val >> 16) & 0xFF);
  bytes[3] = (uint8_t)((val >> 24) & 0xFF);

  for (i = 0; i < 4; i++){
    v = EEPROM.read (addr +1);
    if (bytes[i] != v) EEPROM.write (addr + i, bytes[i]);
  }  
}


uint32_t eepromRead32 (uint16_t addr) {
  uint8_t i;
  uint16_t bytes[4];
  uint32_t val;
  for (i = 0; i < 4; i++){
    bytes[i] = EEPROM.read (addr + i);
  }
  val  = (uint32_t)bytes[0];
  val |= (uint32_t)bytes[1] <<  8;
  val |= (uint32_t)bytes[2] << 16;
  val |= (uint32_t)bytes[3] << 24;
  return val;
}


uint8_t dec2bcdLow (uint8_t value) {
  while ( value >= 10 ) value -= 10;
  return value; 
}


uint8_t dec2bcdHigh (uint8_t value) {
  uint8_t rv = 0;
  while ( value >= 100) value -= 100;
  while ( value >= 10 ) {
    value -= 10;
    rv++;
  }
  return rv; 
}
