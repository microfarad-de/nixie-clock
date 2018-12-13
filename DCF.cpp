/*
 * Class for decoding the DCF77 signal 
 *
 * Karim Hraibi - 2018
 */


#include "dcf.h"

/*
 * Uncomment for activating debug output over the serial port
 */
//#define SERIAL_DEBUG
//#define SERIAL_BAUD 115200
//#define VERBOSE

// use these macros for printing to serial port
#ifdef SERIAL_DEBUG  
  #define PRINT(...)   Serial.print   (__VA_ARGS__)
  #define PRINTLN(...) Serial.println (__VA_ARGS__)
#else
  #define PRINT(...)
  #define PRINTLN(...)
#endif


void dcfIsr (void);
DcfClass DCF;


/***********************************
 * Initialize the DCF receiver
 ***********************************/
void DcfClass::initialize (uint8_t dcfPin, uint8_t bitStart, uint8_t dcfPinMode) {
  uint8_t pinMd;
  this->dcfPin = dcfPin;
  this->ledPin = ledPin;
  this->startEdge = (bitStart == RISING ? HIGH : LOW);
  pinMd = ( dcfPinMode == INPUT_PULLUP ? INPUT_PULLUP : INPUT);
  pinMode (dcfPin, pinMd);
#ifdef SERIAL_DEBUG
  Serial.begin (SERIAL_BAUD);
#endif
  PRINTLN (" ");
  PRINTLN ("+ + + D C F + + +");
  PRINTLN (" ");

  this->interrupt = digitalPinToInterrupt (dcfPin);
  attachInterrupt (interrupt, dcfIsr, CHANGE); 
  this->isConfigured = true;
}
/*********/



/***********************************
 * Read the DCF time
 ***********************************/
uint8_t DcfClass::getTime ( void) {
  
  uint8_t rv = 41;
  
  // too many bits -> restart
  if (idx >= DCF_BIT_COUNT) { 
    PRINT   ("many bits... idx = ");
    PRINTLN (idx, DEC);
    idx = 0;
    rv = 31;
  }
  // synchronization bit detected
  else if (dcfBit == DCF_BIT_SYNC) { 

    // full word of 59 bits received -> verify
    if (idx == DCF_BIT_COUNT - 1) { 
      bits[idx] = DCF_BIT_LOW;
      dcfBit = DCF_BIT_NONE;
      lastBit = DCF_BIT_SYNC;
      rv = verify();
      PRINT   ("full word... idx = ");
      PRINTLN (idx, DEC);
      PRINT   ("verify = ");
      PRINTLN (rv, DEC);
      idx = 0;
    } 
    // too few bits -> restart
    else { 
      dcfBit = DCF_BIT_NONE;
      lastBit = DCF_BIT_SYNC;
      PRINT   ("few bits... idx = ");
      PRINTLN (idx, DEC);
      idx = 0;
      rv = 32;
    }
  }
  // received a new bit
  else if (dcfBit == DCF_BIT_HIGH || dcfBit == DCF_BIT_LOW) { 
    bits[idx] = dcfBit;
    lastBit = dcfBit;
    dcfBit = DCF_BIT_NONE; 
    idx++; 
    lastIdx = idx; 
    rv = 33;        
  #ifdef VERBOSE
    PRINT   (" - ");
    PRINTLN (idx, DEC);
  #endif    
  }

  return rv;
}
/*********/



/***********************************
 * Pause DCF reception
 ***********************************/
void DcfClass::pauseReception (void) {
  if (!isConfigured) return;  
  detachInterrupt (interrupt);
  lastIrqTrigger = !startEdge;
}
/*********/



/***********************************
 * Resume DCF reception
 ***********************************/
void DcfClass::resumeReception (void) {
  if (!isConfigured) return;  
  attachInterrupt (interrupt, dcfIsr, CHANGE); 
}
/*********/



#ifdef VERBOSE
  #define ISR_PRINT  PRINTLN (delta, DEC);
#else
  #define ISR_PRINT
#endif

/***********************************
 * ISR triggered on both falling and rising edges of the DCF signal
 ***********************************/
void dcfIsr () {
  uint32_t delta;
  uint32_t ts;
  uint8_t dcfPinValue; 
  
  ts = millis ();
  dcfPinValue = digitalRead (DCF.dcfPin);
  DCF.lastIrqTrigger = dcfPinValue;   // store the value of of input pin for later use
  
  if (dcfPinValue == DCF.startEdge) { 
    delta = ts - DCF.startEdgeTs;                                   // measure distance between consecutive falling edges
    if      (delta > 2050) { ISR_PRINT }                            // not valid but needed for avoiding deadlock
    else if (delta > 1950) { DCF.dcfBit = DCF_BIT_SYNC; ISR_PRINT } // no falling edge for 2s = sync
    else if (delta > 1050) { return; }
    else if (delta > 950)  { ISR_PRINT }                            // expected falling edge every 1s
    else                   { return; }
    DCF.startEdgeTs = ts;
    DCF.rxFlag = 0; 
  }
  else { 
    delta = ts - DCF.startEdgeTs;                                                 // measure pulse width
    if  (DCF.rxFlag == 1) { return; }                                             // reject any subsequent pulses until the next bit starts
    /* else if (delta > 260) { return; } */
    else if (delta > 175) { DCF.dcfBit = DCF_BIT_HIGH; DCF.rxFlag = 1; ISR_PRINT} // 200ms pulse width = 1
    /* else if (delta > 160) { return; } */
    else if (delta >  50) { DCF.dcfBit = DCF_BIT_LOW;  DCF.rxFlag = 1; ISR_PRINT} // 100ms pulse width = 0
    else                  { return; }
  } 
}
/*********/



/***********************************
 * Verifies the recieved DCF word for correctness
 ***********************************/
uint8_t DcfClass::verify (void) {
  
  uint8_t i, sum;
  uint8_t minutes = bits[21]*1 + bits[22]*2 + bits[23]*4 + bits[24]*8 + bits[25]*10 + bits[26]*20 + bits[27]*40;
  uint8_t hours   = bits[29]*1 + bits[30]*2 + bits[31]*4 + bits[32]*8 + bits[33]*10 + bits[34]*20;
  uint8_t dayM    = bits[36]*1 + bits[37]*2 + bits[38]*4 + bits[39]*8 + bits[40]*10 + bits[41]*20;
  uint8_t dayW    = bits[42]*1 + bits[43]*2 + bits[44]*4;
  uint8_t month   = bits[45]*1 + bits[46]*2 + bits[47]*4 + bits[48]*8 + bits[49]*10;
  uint8_t year    = bits[50]*1 + bits[51]*2 + bits[52]*4 + bits[53]*8 + bits[54]*10 + bits[55]*20 + bits[56]*40 + bits[57]*80;
  uint8_t cest    = bits[17];
  uint8_t cet     = bits[18];
  
#ifdef SERIAL_DEBUG 
  if (hours < 10) PRINT ("0"); // add leading zero
  PRINT (hours, DEC);
  PRINT (":");
  if (minutes < 10) PRINT ("0");
  PRINT (minutes, DEC);
  PRINT (" ");
  PRINT (dayW, DEC);
  PRINT (" ");
  if (dayM < 10) PRINT ("0");
  PRINT (dayM, DEC);
  PRINT (".");
  if (month < 10) PRINT ("0");
  PRINT (month, DEC);
  PRINT (".");
  if (year < 10) PRINT ("0");
  PRINT (year, DEC);
  PRINT (" ");
  PRINT (cest, DEC);
  PRINT (" ");
  PRINT (cet, DEC);
  PRINTLN (" ");
#endif
 
  // populate output structure
  currentTm.tm_sec = 0;           // seconds after the minute - [ 0 to 59 ]
  currentTm.tm_min = minutes;     // minutes after the hour - [ 0 to 59 ]
  currentTm.tm_hour = hours;      // hours since midnight - [ 0 to 23 ]
  currentTm.tm_mday = dayM;       // day of the month - [ 1 to 31 ]
  currentTm.tm_wday = dayW % 7;   // days since Sunday - [ 0 to 6 ]
  currentTm.tm_mon = month - 1;   // months since January - [ 0 to 11 ]
  currentTm.tm_year = year + 100; // years since 1900
  //currentTm.tm_yday;            // days since January 1 - [ 0 to 365 ]
  currentTm.tm_isdst = cest;      // Daylight Saving Time flag
  
  // sanity checks
  if (bits[0] != 0)  return 1;
  if (bits[20] != 1) return 2;
  if (bits[59] != 0) return 3;
  if (minutes > 59)  return 4;
  if (hours > 23)    return 5;
  if (dayM == 0)     return 6;
  if (dayM > 31)     return 7;
  if (dayW == 0)     return 8;
  if (dayW > 7)      return 9;
  if (month == 0)    return 10;
  if (month > 12)    return 11;
  if (year > 99)     return 12;
  if (cest == cet)   return 13;

  // parity checks
  sum = 0;
  for (i = 21; i <= 28; i++) sum += bits[i];
  if (sum % 2 != 0) return 21;
  
  sum = 0;
  for (i = 29; i <= 35; i++) sum += bits[i];
  if (sum % 2 != 0) return 22;

  sum = 0;
  for (i = 36; i <= 58; i++) sum += bits[i];
  if (sum % 2 != 0) return 23;

  return 0;
}
/*********/
