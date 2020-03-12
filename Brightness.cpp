/*
 * Dynamic display brightness adjustment algorithm 
 *
 * Karim Hraibi - 2018
 */

#include "Brightness.h"
#include <EEPROM.h>
#include <assert.h>

// maximum difference between consecutive auto-adjust steps
#define AUTOADJUST_MAX_STEP 4


BrightnessClass Brightness;


void BrightnessClass::initialize (uint16_t eepromAddr) {
  uint8_t i;  
  assert (eepromAddr + BRIGHTNESS_LUT_SIZE < EEPROM.length());
  this->eepromAddr = eepromAddr;

  for (i = 0; i < BRIGHTNESS_LUT_SIZE; i++) {
    lut[i] = EEPROM.read (eepromAddr + i);
    //Serial.print ("lut[");
    //Serial.print (i, DEC);
    //Serial.print ("] = ");
    //Serial.println (lut[i], DEC);
  }  
}



uint8_t BrightnessClass::lightSensorUpdate (int16_t value) {
  lutIdx = map (value, 0, 1023, 0, BRIGHTNESS_LUT_SIZE - 1);
  //Serial.print ("lutIdx = ");
  //Serial.println (lutIdx, DEC);
  return lut[lutIdx];
}


uint8_t BrightnessClass::increase (uint8_t steps) {
  int16_t temp = (int16_t)lut[lutIdx];
  temp += steps;
  if (temp > 255) temp = 255; 
  lut[lutIdx] = (uint8_t)temp;
  interpolate ();
  return lut[lutIdx];
}


uint8_t BrightnessClass::decrease (uint8_t steps) {
  int16_t temp = (int16_t)lut[lutIdx];
  temp -= steps;
  if (temp < 0) temp = 0;
  lut[lutIdx] = (uint8_t)temp;
  interpolate ();
  return lut[lutIdx];
}


void BrightnessClass::interpolate (void) {
  int16_t i;
  for (i = lutIdx - 1; i >= 0 ; i--) {
    if (lut[i] < lut[i+1]) lut[i] = lut[i+1];
    //while (lut[i] - lut[i+1] > AUTOADJUST_MAX_STEP) lut[i]--;
    if (lut[i] - lut[i+1] > AUTOADJUST_MAX_STEP) lut[i] = lut[i+1] + AUTOADJUST_MAX_STEP;
  }
  for (i = lutIdx + 1; i < BRIGHTNESS_LUT_SIZE; i++) {
    if (lut[i-1] < lut[i]) lut[i] = lut[i-1];
    //while (lut[i-1] - lut[i] > AUTOADJUST_MAX_STEP) lut[i]++;
    if (lut[i-1] - lut[i] > AUTOADJUST_MAX_STEP) lut[i] = lut[i-1] - AUTOADJUST_MAX_STEP;
  }
}



void BrightnessClass::eepromWrite (void) {
  uint8_t i, lutVal, eepromVal;

  for (i = 0; i < BRIGHTNESS_LUT_SIZE; i++) {
    lutVal = lut[i];
    eepromVal = EEPROM.read (eepromAddr + i);
    if (lutVal != eepromVal) {
      EEPROM.write (eepromAddr + i, lutVal);
    }
    //Serial.print ("lut[");
    //Serial.print (i, DEC);
    //Serial.print ("] = ");
    //Serial.println (lutVal, DEC);
  }
}

