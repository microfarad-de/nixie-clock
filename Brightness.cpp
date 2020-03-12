/*
 * Dynamic display brightness adjustment algorithm 
 *
 * Karim Hraibi - 2018
 */

#include "Brightness.h"
#include <EEPROM.h>
#include <assert.h>
#include "Helper.h"


#define NUM_STEPS 100
#define BOOST_STEPS 35
#define MAX_VAL (NUM_STEPS + BOOST_STEPS)
#define AUTOADJUST_MIN_STEP 3
#define AUTOADJUST_MAX_STEP 8


BrightnessClass Brightness;


void BrightnessClass::initialize (uint16_t eepromAddr, uint8_t boostPin = 0) {
  uint8_t i;  
  assert (eepromAddr + sizeof(lut) < EEPROM.length()); 
  this->eepromAddr = eepromAddr;
  this->boostPin = boostPin;
  this->boostEnabled = false;

  ::eepromRead (eepromAddr, (uint8_t *)lut, sizeof(lut));
}



uint8_t BrightnessClass::lightSensorUpdate (int16_t value) {
  lutIdx = map (value, 0, 1023, 0, BRIGHTNESS_LUT_SIZE - 1);
  //Serial.print ("lutIdx = ");
  //Serial.println (lutIdx, DEC);
  return boost (lut[lutIdx]);
}


uint8_t BrightnessClass::increase (uint8_t steps) {
  int16_t val = (int16_t)lut[lutIdx];
  val += steps;
  if (val >= MAX_VAL) val = MAX_VAL - 1; 
  lut[lutIdx] = (uint8_t)val;
  interpolate ();
  return boost (lut[lutIdx]);
}


uint8_t BrightnessClass::decrease (uint8_t steps) {
  int16_t val = (int16_t)lut[lutIdx];
  val -= steps;
  if (val < 0) val = 0;
  lut[lutIdx] = (uint8_t)val;
  interpolate ();
  return boost (lut[lutIdx]);
}


void BrightnessClass::interpolate (void) {
  int16_t i, val, prevVal;
  for (i = lutIdx - 1; i >= 0 ; i--) {
    val = (int16_t)lut[i];
    prevVal = (int16_t)lut[i+1];
    if (val < prevVal + AUTOADJUST_MIN_STEP) val = prevVal + AUTOADJUST_MIN_STEP;
    if (val > prevVal + AUTOADJUST_MAX_STEP) val = prevVal + AUTOADJUST_MAX_STEP;
    if (val >= MAX_VAL) val = MAX_VAL - 1;
    lut[i] = (uint8_t)val;
  }
  for (i = lutIdx + 1; i < BRIGHTNESS_LUT_SIZE; i++) {
    val = (int16_t)lut[i];
    prevVal = (int16_t)lut[i-1];
    if (val > prevVal - AUTOADJUST_MIN_STEP) val = prevVal - AUTOADJUST_MIN_STEP;
    if (val < prevVal - AUTOADJUST_MAX_STEP) val = prevVal - AUTOADJUST_MAX_STEP;
    if (val < 0) val = 0;
    lut[i] = (uint8_t)val;
  }
}

uint8_t BrightnessClass::boost (uint8_t value) {
  if (boostEnabled) {
    if (value >= NUM_STEPS) {
      digitalWrite (boostPin, HIGH);
      return (uint8_t)(value - BOOST_STEPS);
    }
    else {
      digitalWrite (boostPin, LOW);
      return (uint8_t)value;
    }   
  }
  else {
    if (value >= NUM_STEPS) return (uint8_t)(NUM_STEPS - 1);
    else                    return (uint8_t)value;  
  } 
}


void BrightnessClass::eepromWrite (void) {
  ::eepromWrite (eepromAddr, (uint8_t *)lut, sizeof(lut));
}


void BrightnessClass::boostEnable (bool enable) {
  pinMode (boostPin, OUTPUT);
  if (enable) {
    boostEnabled = true;  
  }
  else {
    boostEnabled = false;
    digitalWrite (boostPin, LOW);
  }
}

void BrightnessClass::boostDeactivate (void) {
  if (boostEnabled) digitalWrite (boostPin, LOW);
}

