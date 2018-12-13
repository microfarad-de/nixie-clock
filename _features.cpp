/*
 * Implementation of various clock features (complications) 
 *
 * Karim Hraibi - 2018
 */

#include "Features.h"

#define TIMER_ALARM_DURATION      (5 * 60000)
#define ALARM_ALARM_DURATION      (30 * 60000)
#define ALARM_SNOOZE_DURATION     (8 * 60000)
#define BUTTON_LONG_PRESS_TIMEOUT (1000)



/*#######################################################################################*/

BuzzerClass Buzzer;

void BuzzerClass::initialize (uint8_t buzzerPin) {
  this->buzzerPin = buzzerPin;
  pinMode (buzzerPin, OUTPUT);
  digitalWrite (buzzerPin, LOW);
  active = false;
  index = 0;
  initialized = true;
}

void BuzzerClass::loopHandler (void) {
  uint32_t ts;
  
  if (!initialized || !active ) return;

  ts = millis ();
  
  if (ts - melodyTs > melody[index]) {
    digitalWrite (buzzerPin, !digitalRead (buzzerPin));
    melodyTs = ts;
    index++;
    if (melody[index] < 0) index = 0;
  }
  
}

void BuzzerClass::playMelody1 (void) {
  if (!initialized) return;
  active = true;
  melody = melody1;
  melodyTs = millis () - 5000;
  index = 0;
}

void BuzzerClass::playMelody2 (void) {
  if (!initialized) return;
  active = true;
  melody = melody2;
  melodyTs = millis () - 5000;
  index = 0;
}

void BuzzerClass::stop (void) {
  active = false;
  index = 0;
  digitalWrite (buzzerPin, LOW);
}


/*#######################################################################################*/

void ChronoClass::increment10th (void) volatile {
  tenth++;
  if (tenth > 9) tenth = 0, second++;
  if (second > 59) second = 0, minute++;
  if (minute > 59) minute = 0, hour++;
}

void ChronoClass::increment10sec (void) volatile {
  second += 10;
  if (second > 59) second = 0, minute++;
  if (minute > 59) minute = 0, hour++;
}

bool ChronoClass::decrement10sec (void) volatile {
  second -= 10;
  if (second < 0) second = 59, minute--;
  if (minute < 0) minute = 59, hour--;
  if (hour < 0) hour = 0, minute = 0, second = 0;
  if (hour == 0 && minute == 0 && second == 0) return true;
  return false;
}

void ChronoClass::incrementMin (void) volatile {
  minute++;
  if (minute > 59) minute = 0, hour++;
}

bool ChronoClass::decrementMin (void) volatile {
  minute--;
  if (minute < 0) minute = 59, hour--;
  if (hour < 0) hour = 0, minute = 0, second = 0;
  if (hour == 0 && minute == 0 && second == 0) return true;
  return false;
}

bool ChronoClass::decrementSec (void) volatile {
  second--;
  if (second < 0) second = 59, minute--;
  if (minute < 0) minute = 59, hour--;
  if (hour < 0) hour = 0, minute = 0, second = 0;
  if (hour == 0 && minute == 0 && second == 0) return true;
  return false;
}

void ChronoClass::reset (void) volatile {
  tenth = 0;
  second = 0;
  minute = 0;
  hour = 0;
}

void ChronoClass::copy (volatile ChronoClass *tm) volatile {
  tenth = tm->tenth;
  second = tm->second;
  minute = tm->minute;
  hour = tm->hour;
}

void ChronoClass::roundup (void) volatile {
  tenth = 0;
  if (second !=0) second = 0, minute++;
  if (minute > 59) minute = 0, hour++;
}

/*#######################################################################################*/

void CdTimerClass::initialize (void (*callback)(bool)) {
  this->callback = callback;
  defaultTm.tenth = 0;
  defaultTm.second = 0;
  defaultTm.minute = 5;
  defaultTm.hour = 0;
  reset ();
}

void CdTimerClass::loopHandler (void) {
  uint32_t ts = millis ();
  
  if (tickFlag) {
    displayRefresh ();
    if (alarm) {
      stop ();
      Nixie.resetBlinking ();
      Nixie.blinkAll (true);
      Buzzer.playMelody2 ();
      tm.copy (&defaultTm);
      displayRefresh ();
      alarmTs = ts;
    }
    tickFlag = false;
  }
  
  if (alarm && ts - alarmTs > TIMER_ALARM_DURATION) resetAlarm ();
}

void CdTimerClass::tick (void) {
  if (running) {
    alarm = tm.decrementSec (); 
    tickFlag = true;
  }
}

void CdTimerClass::secondIncrease (void) {
  stop ();
  tm.increment10sec ();
  displayRefresh ();
  start ();
}

void CdTimerClass::secondDecrease (void) {
  bool rv;
  stop ();
  rv = tm.decrement10sec ();
  displayRefresh ();
  if (rv) running = false, callback (false);
  else start ();
}

void CdTimerClass::minuteIncrease (void) {
  stop ();
  tm.incrementMin ();
  displayRefresh ();
  start ();
}

void CdTimerClass::minuteDecrease (void) {
  bool rv;
  stop ();
  rv = tm.decrementMin ();
  displayRefresh ();
  if (rv) running = false, callback (false);
  else start ();
}

void CdTimerClass::displayRefresh (void) {
  digits.value[0] = dec2bcdLow  (tm.second);
  digits.value[1] = dec2bcdHigh (tm.second);
  digits.value[2] = dec2bcdLow  (tm.minute);
  digits.value[3] = dec2bcdHigh (tm.minute);
  digits.value[4] = dec2bcdLow  (tm.hour);
  digits.value[5] = dec2bcdHigh (tm.hour); 
}

void CdTimerClass::start (void) {
  defaultTm.copy (&tm);
  active = true;
  running = true;
  callback (true);  
}

void CdTimerClass::stop (void) {
  running = false;
  callback (false);
}

void CdTimerClass::resetAlarm (void) {
  if (alarm) {
    alarm = false;
    Nixie.blinkAll (false);
    Buzzer.stop ();
  }
}

void CdTimerClass::reset (void) {
  resetAlarm ();
  active = false;
  running = false;
  Nixie.resetDigits (&digits);
  callback (false);
  defaultTm.roundup ();
  tm.copy (&defaultTm);
  displayRefresh ();
}

/*#######################################################################################*/

void StopwatchClass::initialize (void (*callback)(bool reset)) {
  this->callback = callback;
  reset ();
}

void StopwatchClass::loopHandler (void) {
  if (tickFlag) {
    if (!paused) displayRefresh ();
    if (tm.hour > 1) {
      tm.hour = 1; tm.minute = 59; tm.second = 59; tm.tenth = 9;
      stop ();
    }
    tickFlag = false;
  }
}

void StopwatchClass::tick (void) {
  if (running) {
    tm.increment10th ();
    tickFlag = true;
  }
}

void StopwatchClass::start (void) {
  active = true;
  running = true;
  callback (true);  
}

void StopwatchClass::stop (void) {
  running = false;
  pause (false);
  callback (false);
}

void StopwatchClass::pause (bool enable) {
  uint8_t i;
  if (enable && running) {
    paused = true;
    Nixie.resetBlinking ();
    for (i = 0; i < 6; i++) digits.blnk[i] = true;  
  }
  else {
    paused = false;
    displayRefresh ();
    for (i = 0; i < 6; i++) digits.blnk[i] = false;
  }
  
}

void StopwatchClass::displayRefresh (void) {
  digits.value[0] = 0;
  digits.value[1] = dec2bcdLow  (tm.tenth);
  digits.value[2] = dec2bcdLow  (tm.second);
  digits.value[3] = dec2bcdHigh (tm.second);
  digits.value[4] = dec2bcdLow  (tm.minute);
  digits.value[5] = dec2bcdHigh (tm.minute); 
  if (tm.hour > 0) digits.comma[4] = true;
}

void StopwatchClass::reset (void) {
  uint8_t i;
  active = false;
  running = false;
  paused = false;
  Nixie.resetDigits (&digits);
  for (i = 0; i < 6; i++) digits.blnk[i] = false;
  tm.reset ();
  displayRefresh ();
  callback (false);
}

/*#######################################################################################*/

void AlarmClass::initialize (AlarmEeprom_s *settings) {
  this->settings = settings;
  alarm = false;
  snoozing = false;
  Nixie.blinkAll (false);
  if (settings->minute < 0 || settings->minute > 59) settings->minute = 0;
  if (settings->hour < 0 || settings->hour > 23) settings->hour = 0;
  if (settings->mode != ALARM_OFF && settings->mode != ALARM_WEEKENDS && 
      settings->mode != ALARM_WEEKDAYS && settings->mode != ALARM_DAILY) settings->mode = ALARM_OFF;
  displayRefresh ();
}

void AlarmClass::loopHandler (int8_t hour, int8_t minute, int8_t wday, bool active) {
  uint32_t ts = millis ();

  if (active && !snoozing && settings->mode != ALARM_OFF && !alarmCondition &&
      minute == settings->minute && hour == settings->hour && 
      (settings->mode != ALARM_WEEKDAYS || (wday >= 1 && wday <= 5)) && 
      (settings->mode != ALARM_WEEKENDS || wday == 0 || wday == 6)) {
        startAlarm (); 
        alarmCondition = true;
      }

  if (snoozing && ts - snoozeTs > ALARM_SNOOZE_DURATION) startAlarm ();

  if (snoozing && ts - blinkTs > 500) {
    Nixie.comma[0] = !Nixie.comma[0];
    blinkTs = ts;
  }

  if (alarm && ts - alarmTs > ALARM_ALARM_DURATION) resetAlarm ();

  if (minute != lastMinute) alarmCondition = false;
  lastMinute = minute;
}

void AlarmClass::startAlarm (void) {
  alarm = true;
  Nixie.resetBlinking ();
  Nixie.blinkAll (true);
  Buzzer.playMelody1 ();
  alarmTs = millis ();
  snoozing = false;
  displayRefresh ();
}

void AlarmClass::snooze (void) {
  if (alarm && !snoozing) {
    alarm = false;
    Nixie.blinkAll (false);
    Buzzer.stop ();
    snoozing = true;
    snoozeTs = millis ();
    displayRefresh ();
  }
}

void AlarmClass::resetAlarm (void) {
  if (alarm || snoozing) {
    alarm = false;
    Nixie.blinkAll (false);
    Buzzer.stop ();
    snoozing = false;
    displayRefresh ();
  }
}

void AlarmClass::modeIncrease (void) {
  if      (settings->mode == ALARM_OFF)      settings->mode = ALARM_WEEKENDS;
  else if (settings->mode == ALARM_WEEKENDS) settings->mode = ALARM_WEEKDAYS;
  else if (settings->mode == ALARM_WEEKDAYS) settings->mode = ALARM_DAILY;
  else if (settings->mode == ALARM_DAILY)    settings->mode = ALARM_OFF, lastMode = ALARM_DAILY;
  displayRefresh ();
}

void AlarmClass::modeDecrease (void) {
  if      (settings->mode == ALARM_OFF)      settings->mode = ALARM_DAILY;
  else if (settings->mode == ALARM_DAILY)    settings->mode = ALARM_WEEKDAYS;
  else if (settings->mode == ALARM_WEEKDAYS) settings->mode = ALARM_WEEKENDS;
  else if (settings->mode == ALARM_WEEKENDS) settings->mode = ALARM_OFF, lastMode = ALARM_DAILY;
  displayRefresh ();
}

void AlarmClass::modeToggle (void) {
  if (settings->mode == ALARM_OFF) {
    settings->mode = lastMode;
  }
  else {
    lastMode = settings->mode;
    settings->mode = ALARM_OFF;
  }
  displayRefresh ();
}

void AlarmClass::minuteIncrease (void) {
  settings->minute++;
  if (settings->minute > 59) settings->minute = 0;
  displayRefresh ();
}

void AlarmClass::minuteDecrease (void) {
  settings->minute--;
  if (settings->minute < 0) settings->minute = 59;
  displayRefresh ();
}

void AlarmClass::hourIncrease (void) {
  settings->hour++;
  if (settings->hour > 23) settings->hour = 0;
  displayRefresh ();
}

void AlarmClass::hourDecrease (void) {
  settings->hour--;
  if (settings->hour < 0) settings->hour = 23;
  displayRefresh ();
}

void AlarmClass::displayRefresh (void) {
  for (uint8_t i = 0; i < 6; i++) digits.blank[i] = false;
  digits.value[0] = (uint8_t)settings->mode;
  Nixie.comma[0] = (bool)settings->mode;
  digits.blank[1] = true;
  digits.value[2] = dec2bcdLow  (settings->minute);
  digits.value[3] = dec2bcdHigh (settings->minute);
  digits.value[4] = dec2bcdLow  (settings->hour);
  digits.value[5] = dec2bcdHigh (settings->hour);   
}



/*#######################################################################################*/


void ButtonClass::press (void) {
  wasPressed = pressed;
  pressed = true;
}

void ButtonClass::release (void) {
  wasPressed = pressed;
  pressed = false;
}

bool ButtonClass::rising (void) {
  bool rv = false;
  if (pressed && !wasPressed) {
    wasPressed = pressed;
    longPressTs = millis ();
    longPressed = true;
    wasLongPressed = false;
    rv = true;
  }
  return rv;
}

bool ButtonClass::falling (void) {
  bool rv = false;
  if (!pressed && wasPressed && !wasLongPressed) {
    wasPressed = pressed;
    rv = true;
  }
  return rv;  
}

bool ButtonClass::fallingLongPress (void) {
  bool rv = false;
  if (!pressed && wasPressed && wasLongPressed) {
    wasPressed = pressed;
    wasLongPressed = false;
    rv = true;
  }
  return rv;  
}

bool ButtonClass::fallingContinuous (void) {
  return !pressed && wasPressed; 
}

bool ButtonClass::longPress (void) {
  bool rv = false;
  if (pressed && longPressed && millis () - longPressTs > BUTTON_LONG_PRESS_TIMEOUT) {
    longPressed = false;
    wasLongPressed = true;
    rv = true;
  }
  return rv;
}

bool ButtonClass::longPressContinuous (void) {
  bool rv = false;
  if (pressed && millis () - longPressTs > BUTTON_LONG_PRESS_TIMEOUT) {
    longPressed = false;
    wasLongPressed = true;
    rv = true;
  }
  return rv;
}


/*#######################################################################################*/
