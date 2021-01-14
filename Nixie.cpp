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

#include <avr/wdt.h>
#include "Nixie.h"


#define DIGIT_PERIOD        3000
#define MAX_ON_DURATION     2680
#define BLINK_PERIOD        500000
#define SCROLL_PERIOD_1     1000000
#define SCROLL_PERIOD_2     300000
#define SLOT_MACHINE_PERIOD 40000
#define CPP_PERIOD          200000
#define NUM_BCD_PINS        4


NixieClass Nixie;


void NixieClass::initialize ( uint8_t anodePin0, uint8_t anodePin1, uint8_t anodePin2, uint8_t anodePin3, 
        uint8_t anodePin4, uint8_t anodePin5, uint8_t bcdPin0, uint8_t bcdPin1, uint8_t bcdPin2, 
        uint8_t bcdPin3, uint8_t commaPin, NixieDigit_s *digits, uint8_t numDigits, uint8_t brightness) {

  uint8_t i;
  this->anodePin[0] = anodePin0;
  this->anodePin[1] = anodePin1;
  this->anodePin[2] = anodePin2;
  this->anodePin[3] = anodePin3;
  this->anodePin[4] = anodePin4;
  this->anodePin[5] = anodePin5;
  this->bcdPin[0] = bcdPin0;
  this->bcdPin[1] = bcdPin1;
  this->bcdPin[2] = bcdPin2;
  this->bcdPin[3] = bcdPin3;
  this->commaPin = commaPin;
  this->digits = digits;
  this->numDigits = numDigits;
  this->digitOnDuration = map (brightness, 0, 255, 0, MAX_ON_DURATION);

  // initialize output pins
  for (i = 0; i < NIXIE_NUM_TUBES; i++) {
    pinMode (anodePin[i], OUTPUT);
    digitalWrite (anodePin[i], LOW);
  }
  for (i = 0; i < NUM_BCD_PINS; i++) {
    pinMode (bcdPin[i], OUTPUT);
    digitalWrite (bcdPin[i], LOW);
  }
  pinMode (commaPin, OUTPUT);
  digitalWrite (commaPin, LOW); 
}


void NixieClass::setDigits (NixieDigit_s *digits, uint8_t numDigits) {
  this->digits = digits;
  this->numDigits = numDigits;
}

void NixieClass::refresh (void) {

  uint8_t bcdVal;
  bool commaVal, anodeVal;
  uint32_t ts = micros ();

  // display disabled
  if (!enabled) {
    wdt_reset ();
    // keep rotating digits to avoid delayed "Slot Machine" effect
    digit++;
    if (digit >= NIXIE_NUM_TUBES) digit = 0;   
  }
  
  // display multiplexing by switching digits
  else if (ts - lastTs >= DIGIT_PERIOD) {
    
    wdt_reset (); // reset the watchdog timer
    
    digitalWrite (anodePin[digit], LOW); 
    
    digit++;
    if (digit >= NIXIE_NUM_TUBES) digit = 0;
    
    bcdVal = digits[digit + scrollOffset].value;

    if (slotMachineEnabled[digit] || cppEnabled) {
      // produce "Slot Machine" or CPP effect 
      bcdVal += slotMachineCnt[digit] + cppCnt;
      while (bcdVal > 9) bcdVal -= 10; 
    }

    commaVal = digits[digit + scrollOffset].comma || comma[digit] || cppEnabled || slotMachineEnabled[digit];
    anodeVal = !(blinkFlag && (digits[digit + scrollOffset].blink || blinkAllEnabled || blinkCount)) && !digits[digit + scrollOffset].blank;

    // decimal point shall never be blanked
    // reduce brightness by dimFactor for decimal points without digits
    if (commaVal && !anodeVal) dimFactor = 2, anodeVal = true, bcdVal = 10;
    
    digitalWrite (bcdPin[0],  bcdVal       & 1);
    digitalWrite (bcdPin[1], (bcdVal >> 1) & 1);
    digitalWrite (bcdPin[2], (bcdVal >> 2) & 1);
    digitalWrite (bcdPin[3], (bcdVal >> 3) & 1);
    digitalWrite (commaPin, commaVal);
    digitalWrite (anodePin[digit], anodeVal);
     
    lastTs = ts;
  }
  
  // turn off all opto-coupler controlled pins ahead of time, this avoids ghost numbers
  // also control brightness by reducing anode on time
  // reduce on duration by dimFactor for decimal points without digits
  else if (ts - lastTs >= (digitOnDuration >> dimFactor)) {
    
    digitalWrite (anodePin[digit], LOW);  
    digitalWrite (commaPin, LOW); 
    dimFactor = 0;
  }

  // toggle blinking digits
  if (ts - blinkTs > BLINK_PERIOD / (1 + (blinkCount > 0))) {
    blinkFlag = !blinkFlag;
    if (blinkCount > 0) blinkCount--;
    blinkTs = ts;
  }

  // generate the digit "Slot Machine" effect
  if (slotMachineEnabled[digit]) { 
    if (ts - slotMachineTs[digit] > slotMachineDelay[digit]) {
      slotMachineCnt[digit]++;
      if (slotMachineCnt[digit] >= slotMachineCntMax[digit]) {
        slotMachineEnabled[digit] = false;
        slotMachineCnt[digit] = 0;
      }
      slotMachineDelay[digit] = SLOT_MACHINE_PERIOD + 10 * (uint32_t)slotMachineCnt[digit] * (uint32_t)slotMachineCnt[digit];
      slotMachineTs[digit] = ts;
    }
  }  

  // generate cathode poisoning prevention effect
  if (cppEnabled) {
    if (ts - cppTs > CPP_PERIOD) {
      cppCnt++;
      if (cppCnt >= 20) cppEnabled = false,  cppCnt = 0;
      cppTs = ts;
    }
  }

  // scroll through the digits buffer
  if (scrollOffset > 0) {
    if ( (ts - scrollTs > SCROLL_PERIOD_2 && scrollOffset < numDigits - NIXIE_NUM_TUBES) || 
         (ts - scrollTs > SCROLL_PERIOD_1) ) {
      scrollOffset--;
      scrollTs = ts;
    }
  }
}


void NixieClass::setBrightness (uint8_t brightness) {
  this->digitOnDuration = map (brightness, 0, 99, 0, MAX_ON_DURATION);
}

void NixieClass::blinkAll (bool enable) {
  blinkAllEnabled = enable;
}

void NixieClass::blinkOnce (void) {
  //resetBlinking ();
  blinkCount = 2;
}

void NixieClass::resetBlinking (void) {
  blinkTs = micros ();
  blinkFlag = false;
}

void NixieClass::slotMachine (void) {
  uint8_t i;
  for (i = 0; i < NIXIE_NUM_TUBES; i++) {
    slotMachineEnabled[i] = true;
    slotMachineCnt[i] = slotMachineCntStart[i];
    slotMachineDelay[i] = 0;
  }
}

void NixieClass::cathodePoisonPrevent (void) {
  cppEnabled = true;
  cppCnt = 0;
  cppTs = micros () - CPP_PERIOD;
}

void NixieClass::scroll (void) {
  if (numDigits > NIXIE_NUM_TUBES) scrollOffset = numDigits - NIXIE_NUM_TUBES;
  scrollTs = micros ();
}

void NixieClass::cancelScroll (void) {
  scrollOffset = 0;
}

void NixieClass::blank (void) {
  uint8_t i;
  for (i = 0; i < NIXIE_NUM_TUBES; i++) digitalWrite (anodePin[i], LOW); 
  for (i = 0; i < NUM_BCD_PINS;    i++) digitalWrite (bcdPin[i],   LOW);
  digitalWrite (commaPin, LOW);
}

void NixieClass::enable (bool enable) {
  enabled = enable;
  if (!enabled) blank ();
}

void NixieClass::dec2bcd (uint32_t value, NixieDigit_s* output, uint8_t outputSize, uint8_t numDigits) {
  uint8_t i, n;
  uint64_t p = 1;
  //bool leadingZero = true; 
  
  if (numDigits > outputSize) numDigits = outputSize;
  n = numDigits;

  for (i = 0; i < n; i++) output[i].value = 0;
  for (i = 0; i < n; i++) p = p * 10;    

  while (value >= p) value -= p;

  for (i = 0; i < n; i++) {
    p = p / 10;
    while (value >= p) {
      value -= p;
      output[n - i - 1].value++;
    }
    //if (output->value[i] > 0) leadingZero = false;
    //if (leadingZero) output->blank[i] = true;
    //else output->blank[i] = false;
  }
}


void NixieClass::resetDigits (NixieDigit_s *output, uint8_t outputSize) {
  int8_t i;

  for (i = 0; i < outputSize; i++) {
    output[i].value = 0;
    output[i].blank = false;
    output[i].comma = false;
    output[i].blink = false;
  }
}
