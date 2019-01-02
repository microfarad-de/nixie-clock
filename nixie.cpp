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
#include "nixie.h"


#define DIGIT_PERIOD        3000
#define MAX_ON_DURATION     2680
#define BLINK_PERIOD        500000
#define SCROLL_PERIOD_1     1000000
#define SCROLL_PERIOD_2     300000
#define SLOT_MACHINE_PERIOD 40000
#define CPP_PERIOD          200000


NixieClass Nixie;


void NixieClass::initialize (NixieNumTubes_e numTubes,
        uint8_t anodePin0, uint8_t anodePin1, uint8_t anodePin2, uint8_t anodePin3, uint8_t anodePin4, uint8_t anodePin5,
        uint8_t bcdPin0, uint8_t bcdPin1, uint8_t bcdPin2, uint8_t bcdPin3, uint8_t commaPin, NixieDigits_s *digits, uint8_t brightness) {

  uint8_t i;
  this->numTubes = numTubes;
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
  this->digitOnDuration = map (brightness, 0, 255, 0, MAX_ON_DURATION);

  // initialize output pins
  for (i = 0; i < numTubes; i++) {
    pinMode (anodePin[i], OUTPUT);
    digitalWrite (anodePin[i], LOW);
  }
  for (i = 0; i < 4; i++) {
    pinMode (bcdPin[i], OUTPUT);
    digitalWrite (bcdPin[i], LOW);
  }
  pinMode (commaPin, OUTPUT);
  digitalWrite (commaPin, LOW); 
}


void NixieClass::setDigits (NixieDigits_s *digits) {
  this->digits = digits;
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
    if (digit >= numTubes) digit = 0;   
  }
  
  // display multiplexing by switching digits
  else if (ts - lastTs >= DIGIT_PERIOD) {
    
    wdt_reset (); // reset the watchdog timer
    
    digitalWrite (anodePin[digit], LOW); 
    
    digit++;
    if (digit >= numTubes) digit = 0;
    
    bcdVal = digits->value[digit + scrollOffset];

    if (slotMachineEnabled[digit] || cppEnabled) {
      // produce "Slot Machine" or CPP effect 
      bcdVal += slotMachineCnt[digit] + cppCnt;
      while (bcdVal > 9) bcdVal -= 10; 
    }

    commaVal = digits->comma[digit + scrollOffset] || comma[digit] || cppEnabled || slotMachineEnabled[digit];
    anodeVal = !(blinkFlag && (digits->blnk[digit + scrollOffset] || blinkAllEnabled)) && !digits->blank[digit + scrollOffset];

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
  if (ts - blinkTs > BLINK_PERIOD) {
    blinkFlag = !blinkFlag;
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
    if ( (ts - scrollTs > SCROLL_PERIOD_2 && scrollOffset < digits->numDigits - numTubes) || 
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

void NixieClass::resetBlinking (void) {
  blinkTs = micros ();
  blinkFlag = false;
}

void NixieClass::slotMachine (void) {
  uint8_t i;
  for (i = 0; i < numTubes; i++) {
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
  if (digits->numDigits > NIXIE_DIGIT_BUF_SIZE) digits->numDigits = NIXIE_DIGIT_BUF_SIZE;
  if (digits->numDigits > numTubes) scrollOffset = digits->numDigits - numTubes;
  scrollTs = micros ();
}

void NixieClass::cancelScroll (void) {
  scrollOffset = 0;
}

void NixieClass::blank (void) {
  uint8_t i;
  for (i = 0; i < numTubes; i++) digitalWrite (anodePin[i], LOW); 
  for (i = 0; i < 4;        i++) digitalWrite (bcdPin[i],   LOW);
  digitalWrite (commaPin, LOW);
}

void NixieClass::enable (bool enable) {
  enabled = enable;
  if (!enabled) blank ();
}

void NixieClass::dec2bcd (uint32_t value, NixieDigits_s* output, uint8_t numDigits) {
  int8_t i, n;
  uint64_t p = 1;
  //bool leadingZero = true; 
  
  if (numDigits > NIXIE_DIGIT_BUF_SIZE) numDigits = NIXIE_DIGIT_BUF_SIZE;
  n = (int8_t)numDigits;

  for (i = 0; i < n; i++) output->value[i] = 0;
  for (i = 0; i < n; i++) p = p * 10;    

  while (value >= p) value -= p;

  for (i = n - 1; i >= 0; i--) {
    p = p / 10;
    while (value >= p) {
      value -= p;
      output->value[i]++;
    }
    //if (output->value[i] > 0) leadingZero = false;
    //if (leadingZero) output->blank[i] = true;
    //else output->blank[i] = false;
  }
}


void NixieClass::resetDigits (NixieDigits_s *output) {
  int8_t i;

  for (i = 0; i < NIXIE_DIGIT_BUF_SIZE; i++) {
    output->value[i] = 0;
    output->blank[i] = false;
    output->comma[i] = false;
    output->blnk[i] = false;
  }
  output->numDigits = NIXIE_MAX_NUM_TUBES;
}
