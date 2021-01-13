/* 
 * Implementation of various clock features (complications) 
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

#ifndef __FEATURES_H
#define __FEATURES_H

#include <Arduino.h>
#include "Nixie.h"

/*
 * Buzzer control class
 */
class BuzzerClass {
  public:
    void initialize (uint8_t buzzerPin);
    void loopHandler (void);
    void playMelody1 (void);
    void playMelody2 (void);
    void stop (void);

    bool active = false;
    
  private:
    bool initialized = false;
    uint8_t buzzerPin;
    uint8_t index = 0;
    uint32_t melodyTs = 0;
    int32_t *melody = melody1;
    //int32_t melody1[9] = { 500, 50, 100, 50, 100, 50, 100, 50, -1 };
    int32_t melody1[9] = { 1450, 50, 200, 50, 200, 50, -1 };
    int32_t melody2[3] = { 950, 50, -1 };  
};

/*
 * Buzzer object as a singleton
 */
extern BuzzerClass Buzzer;

/*
 * Class for performing simple timekeeping operations
 * can be used for a chronometer or countdown timer
 */
class ChronoClass {
  public:
    void increment10th (void);
    void increment10sec (void);
    bool decrement10sec (void);
    void incrementMin (void);
    bool decrementMin (void);
    void incrementSec (void);
    bool decrementSec (void);
    void reset (void);
    void copy (ChronoClass *);
    void roundup (void);

    int8_t tenth = 0;
    int8_t second = 0;
    int8_t minute = 0;
    int8_t hour = 0;
    
  private:
};


/*
 * Countdown timer implementation class
 */
class CdTimerClass {
  public:
    void initialize (void (*callback)(bool start));
    void loopHandler (void);
    void tick (void);
    void secondIncrease (void);
    void secondDecrease (void);
    void minuteIncrease (void);
    void minuteDecrease (void);
    void displayRefresh (void);
    void start (void);
    void stop (void);
    void resetAlarm (void);
    void reset (void);
    
    bool active = false;
    bool running = false;
    bool alarm = false;
    NixieDigits_s digits; 
  
  private:
    ChronoClass defaultTm;
    ChronoClass tm;
    volatile bool tickFlag = false; 
    void (*callback)(bool) = NULL;
};


/*
 * Stopwatch implementation class
 */
class StopwatchClass {
  public:
    void initialize (void (*callback)(bool start));
    void loopHandler (void);
    void tick (void);
    void start (void);
    void stop (void);
    void pause (bool enable);
    void displayRefresh (void);
    void reset (void);

    bool active = false;
    bool running = false;
    bool paused = false;
    NixieDigits_s digits;
    
  private:
    ChronoClass tm;
    volatile bool tickFlag = false;
    void (*callback)(bool) = NULL;
};


/*
 * Alarm modes
 */
enum AlarmMode_e {
  ALARM_OFF = 0,
  ALARM_WEEKENDS = 2,
  ALARM_WEEKDAYS = 5,
  ALARM_DAILY = 7
};

/*
 * Alarm settings to be stored in EEPROM
 */
struct AlarmEeprom_s {
  int8_t hour;
  int8_t minute;
  AlarmMode_e mode;
  AlarmMode_e lastMode;
};

/*
 * Alarm clock implementation class
 */
class AlarmClass {
  public:
    void initialize (AlarmEeprom_s * settings);
    void loopHandler (int8_t hour, int8_t minute, int8_t wday, bool active);
    void startAlarm (void);
    void snooze (void);
    void resetAlarm (void);
    void modeIncrease (void); 
    void modeDecrease (void);
    void modeToggle (void);
    void minuteIncrease (void);
    void minuteDecrease (void);
    void hourIncrease (void);
    void hourDecrease (void);
    void displayRefresh (void);

    bool alarm = false;
    bool snoozing = false;
    NixieDigits_s digits;
    AlarmEeprom_s *settings = NULL;

  private:
    uint32_t snoozeTs = 0; 
    uint32_t alarmTs = 0;    
    int8_t lastMinute = 0;
    uint32_t blinkTs = 0;
    bool alarmCondition = false;
};




#endif // __FEATURES_H
