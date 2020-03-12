/*
 * Implementation of various clock features (complications) 
 *
 * Karim Hraibi - 2018
 */

#ifndef __FEATURES_H
#define __FEATURES_H

#include <Arduino.h>
#include "Helper.h"
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
    bool decrementSec (void);
    void reset (void);
    void copy (ChronoClass *tm);
    void roundup (void);

    volatile int8_t tenth = 0;
    volatile int8_t second = 0;
    volatile int8_t minute = 0;
    volatile int8_t hour = 0;
    
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
    
    volatile bool active = false;
    volatile bool running = false;
    volatile bool alarm = false;
    volatile NixieDigits_s digits; 
  
  private:
    uint32_t alarmTs = 0;
    ChronoClass defaultTm;
    volatile ChronoClass tm;
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

    volatile bool active = false;
    volatile bool running = false;
    volatile bool paused = false;
    volatile NixieDigits_s digits;
    
  private:
    volatile ChronoClass tm;
    volatile bool tickFlag = false;
    void (*callback)(bool) = NULL;
};


/*
 * Alarm settings to be stored in EEPROM
 */
struct AlarmEeprom_s {
  int8_t hour = 0;
  int8_t minute = 0;
  bool active = false;    
  bool weekdays = false;  
};

/*
 * Alarm clock implementation class
 */
class AlarmClass {
  public:
    void initialize (AlarmEeprom_s * settings);
    void loopHandler (int8_t hour, int8_t minute, int8_t wday, bool active);
    void toggleActive (void); 
    void toggleWeekdays (void);
    void startAlarm (void);
    void snooze (void);
    void resetAlarm (void);
    void minuteIncrease (void);
    void minuteDecrease (void);
    void hourIncrease (void);
    void hourDecrease (void);
    void displayRefresh (void);
    void displayRefreshWeekdays (void);

    bool alarm = false;
    bool snoozing = false;
    volatile NixieDigits_s digits;
    AlarmEeprom_s *settings = NULL;

  private:
    uint32_t snoozeTs = 0; 
    uint32_t alarmTs = 0;    
};


/*
 * Push button implementation class
 */
class ButtonClass {
  public:
    void press (void);
    void release (void);
    bool rising (void);
    bool falling (void);
    bool fallingLongPress (void);
    bool fallingContinuous (void);
    bool longPress (void); 
    bool longPressContinuous (void); 

    bool pressed = false;
    
  private:
    
    bool wasPressed = false;
    bool longPressed = false;
    bool wasLongPressed = false;
    uint32_t longPressTs = 0;
};


#endif // __FEATURES_H

