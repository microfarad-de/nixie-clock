/*
 * Dynamic display brightness adjustment algorithm 
 *
 * Karim Hraibi - 2018
 */

#include "brightness.h"
#include <EEPROM.h>
#include <assert.h>
#include "helper.h"


#define PWM_STEPS 100
#define BOOST_STEPS 34
#define TOTAL_STEPS (PWM_STEPS + BOOST_STEPS)
#define AUTOADJUST_MIN_STEP 1
#define AUTOADJUST_MAX_STEP 4


BrightnessClass Brightness;


void BrightnessClass::initialize (uint16_t eepromAddr, uint8_t boostPin) {
  uint8_t i;  
  assert (eepromAddr + sizeof(lut) < EEPROM.length()); 
  this->eepromAddr = eepromAddr;
  this->boostPin = boostPin;
  this->boostEnabled = false;
  this->autoEnabled = false;
  this->lutIdx = 0;

  ::eepromRead (eepromAddr, (uint8_t *)lut, sizeof(lut));
}

void BrightnessClass::autoEnable (bool enable) {
  this->autoEnabled = enable;
  if (!enable) lutIdx = 0;
}

void BrightnessClass::boostEnable (bool enable) {
  pinMode (boostPin, OUTPUT);
  this->boostEnabled = enable;
  if (!enable) digitalWrite (boostPin, LOW);
}

void BrightnessClass::boostDeactivate (void) {
  if (boostEnabled) digitalWrite (boostPin, LOW);
}

uint8_t BrightnessClass::lightSensorUpdate (int16_t value) {
  if (autoEnabled) lutIdx = map (value, 0, 1023, 1, BRIGHTNESS_LUT_SIZE - 1); // index 0 is used for disabled auto brightness
  else             lutIdx = 0;
  //Serial.print ("lutIdx = ");
  //Serial.println (lutIdx, DEC);
  return boost (lut[lutIdx]);
}


uint8_t BrightnessClass::increase (void) {
  int16_t val = (int16_t)lut[lutIdx];
  val++;
  if (boostEnabled) {
    if (val >= TOTAL_STEPS) val = TOTAL_STEPS - 1;
  }
  else {
    if (val >= PWM_STEPS) val = PWM_STEPS - 1;
  }
  lut[lutIdx] = (uint8_t)val;
  if (autoEnabled) interpolate ();
  return boost (lut[lutIdx]);
}


uint8_t BrightnessClass::decrease (void) {
  int16_t val = (int16_t)lut[lutIdx];
  val--;
  if (val < 0) val = 0;
  lut[lutIdx] = (uint8_t)val;
  if (autoEnabled) interpolate ();
  return boost (lut[lutIdx]);
}


void BrightnessClass::interpolate (void) {
  int16_t i, val, prevVal;
  for (i = lutIdx - 1; i >= 1 ; i--) { // lutIdx 0 is us used for deactivated auto-brightness
    val = (int16_t)lut[i];
    prevVal = (int16_t)lut[i+1];
    if (val < prevVal + AUTOADJUST_MIN_STEP) val = prevVal + AUTOADJUST_MIN_STEP;
    if (val > prevVal + AUTOADJUST_MAX_STEP) val = prevVal + AUTOADJUST_MAX_STEP;
    if (val >= TOTAL_STEPS) val = TOTAL_STEPS - 1;
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
    if (value >= PWM_STEPS) {
      digitalWrite (boostPin, HIGH);
      return (uint8_t)(value - BOOST_STEPS);
    }
    else {
      digitalWrite (boostPin, LOW);
      return (uint8_t)value;
    }   
  }
  else {
    if (value >= PWM_STEPS) return (uint8_t)(PWM_STEPS - 1);
    else                    return (uint8_t)value;  
  } 
}

uint8_t BrightnessClass::maximum (void) {
  //return boost (TOTAL_STEPS - 1);
  return (PWM_STEPS - 1);
}


void BrightnessClass::eepromWrite (void) {
  ::eepromWrite (eepromAddr, (uint8_t *)lut, sizeof(lut));
}
