/*
 * A Nixie Clock Implementation
 * 
 * Features:
 * - 6 IN-8-2 Nixie featuring 0-9 digits and decimal points
 * - Multiplexed display, requires one single K155ID1 Nixie driver chip
 * - Synchronization with the DCF77 time signal
 * - Automatic crystal drift compensation using DCF77 time
 * - Power saving mode for running on a backup super-capacitor
 * - Dual timers: Timer1 used for timekeeping and Timer2 for countdown timer / stopwatch
 * - Automatic and manual display brightness adjustment
 * - Menu navigation using 3 push-buttons
 * - Alarm clock with the weekday and weekend options
 * - Countdown timer
 * - Stopwatch
 * - Service menu
 * - Cathode poisoning prevention and the "Slot Machine" effect
 * - Screen blanking with dual time intervals
 * - Settings are stored to EEPROM
 * - and more...
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
 * 
 * Version: 3.5.0
 * Date:    August 2019
 */
#define VERSION_MAJOR 3  // Major version
#define VERSION_MINOR 5  // Minor version
#define VERSION_MAINT 0  // Maintenance version



#include <time.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include "TimerOne.h"
#include "TimerTwo.h"
#include "Dcf.h"
#include "Nixie.h"
#include "Adc.h"
#include "Brightness.h"
#include "Helper.h"
#include "Features.h"
//#include "BuildDate.h"


#define VALUE_DEBUG         // activate the debug values within the service menu

//#define SERIAL_DEBUG      // activate debug printing over RS232
#define SERIAL_BAUD 115200  // serial baud rate

// use these macros for printing to serial port
#ifdef SERIAL_DEBUG  
  #define PRINT(...)   Serial.print   (__VA_ARGS__)
  #define PRINTLN(...) Serial.println (__VA_ARGS__)
#else
  #define PRINT(...)
  #define PRINTLN(...)
#endif


// reset the Nixie tube uptime to this value in seconds upon booting for the very first time
#define NIXIE_UPTIME_INIT_VAL  ((uint32_t)0*3600)
  
// change the following value to force an uptime reset
#define NIXIE_UPTIME_RESET_CODE 0xDEADBEEF    

// upon the absence of this string in EEPROM all the settings will be reset to default
#define SETTINGS_RESET_CODE 0xDEADBEEF


// anode control pins
#define ANODE0_PIN 12
#define ANODE1_PIN 11
#define ANODE2_PIN 10
#define ANODE3_PIN 7
#define ANODE4_PIN 4
#define ANODE5_PIN 2

// K155ID1 BCD decoder pins
#define BCD0_PIN 9
#define BCD1_PIN 6
#define BCD2_PIN 5
#define BCD3_PIN 8

// pin controlling the comma
#define COMMA_PIN 13 

// DCF77 signal pin with interrupt support (should be 2 or 3 on ATmega328p)
#define DCF_PIN 3   

// pin controlling the buzzer
#define BUZZER_PIN 1 

// pin for controlling the brightness boosting feature
#define BRIGHTNESS_PIN 0

// analog pins
#define NUM_APINS      5   // total number of hardware analog pins in use for non-blocking ADC 
#define BUTTON0_APIN   A2  // button 0 - "mode"
#define BUTTON1_APIN   A3  // button 1 - "increase"
#define BUTTON2_APIN   A1  // button 2 - "decrease"
#define LIGHTSENS_APIN A0  // light sensor
#define EXTPWR_APIN    A6  // measures external power voltage
#define VOLTAGE_APIN   A7  // measures the retention super capacitor voltage

// various constants
#define TIMER1_DEFUALT_PERIOD  1000000       // default period for Timer1 in us (1 second)
#define WDT_TIMEOUT            WDTO_4S       // watcchdog timer timeout setting
#define EEPROM_SETTINGS_ADDR   0             // EEPROM address of the settngs structure
#define EEPROM_BRIGHTNESS_ADDR (EEPROM_SETTINGS_ADDR + sizeof (Settings))      // EEPROM address of the display brightness lookup table
#define MENU_ORDER_LIST_SIZE   3             // size of the dynamic menu ordering list
#define SETTINGS_LUT_SIZE      15            // size of the settings lookup table
#define NUM_DEBUG_VALUES       7             // total number of debug values shown in the service menu
#define NUM_DEBUG_DIGITS       6             // number of digits for the debug values shown in the service menu
#define NUM_SERVICE_VALUES     (3 + NUM_DEBUG_VALUES)   // total number of values inside the service menu


// conversion macros
#define TIMER1_TO_SEC_PER_DAY(VAL) ((TIMER1_DEFUALT_PERIOD - (int32_t)VAL) * 86400 / 1000000) // extact the seconds-per-day clock drift from the Timer1 period
#define SEC_PER_DAY_TO_TIMER1(VAL) (uint32_t)(TIMER1_DEFUALT_PERIOD - VAL * 1000000 / 86400)  // calculate the Timer1 period from the seconds-per-day clock drift


/*
 * Enumerations for the states of the menu navigation state machine
 * the program relies on the exact order of the below definitions
 */
enum MenuState_e { SHOW_TIME_E, SHOW_DATE_E,    SHOW_WEEK_E, SHOW_ALARM_E, SHOW_TIMER_E, SHOW_STOPWATCH_E, SHOW_SERVICE_E, SHOW_BLANK_E, 
                   SET_ALARM_E, SET_SETTINGS_E, SET_HOUR_E,  SET_MIN_E,    SET_SEC_E,    SET_DAY_E,        SET_MONTH_E,    SET_YEAR_E, 
                   SHOW_TIME,   SHOW_DATE,      SHOW_WEEK,   SHOW_ALARM,   SHOW_TIMER,   SHOW_STOPWATCH,   SHOW_SERVICE,   SHOW_BLANK,   
                   SET_ALARM,   SET_SETTINGS,   SET_HOUR,    SET_MIN,      SET_SEC,      SET_DAY,          SET_MONTH,      SET_YEAR,   };


/*
 * Structure that holds the settings to be stored in EEPROM
 */
struct Settings_t {
  volatile uint32_t timer1Period = TIMER1_DEFUALT_PERIOD;  // stores the period of Timer1 in microseconds
  volatile uint32_t nixieUptime;  // stores the nixie tube uptime in seconds
  uint32_t nixieUptimeResetCode;  // uptime is reset to zero if this value is different than the value of NIXIE_UPTIME_RESET_CODE
  bool dcfSyncEnabled;            // enables DCF77 synchronization feature
  bool dcfSignalIndicator;        // enables the live DCF77 signal strength indicator (blinking decimal point on digit 1)
  uint8_t dcfSyncHour;            // hour of day when DCF77 sync shall start
  uint8_t blankScreenMode;        // turn-off display during a time interval in order to reduce tube wear (1 = every day, 2 = on weekdays, 3 = on weekends, 4 = permanent)
  uint8_t blankScreenStartHr;     // start hour for disabling the display
  uint8_t blankScreenFinishHr;    // finish hour for disabling the display
  uint8_t cathodePoisonPrevent;   // enables cathode poisoning prevention measure by cycling through all digits (1 = on preset time, 2 = "Slot Machine" every minute, 3 = "Slot Machine" every 10 min)
  uint8_t cppStartHr;             // start hour for the cathode poisoning prevention measure
  int8_t secPerDayCorrect;        // manual Timer1 drift correction in seconds per day
  uint8_t reserved0;              // reserved for future use
  bool brightnessAutoAdjust;      // enables the brightness auto-adjustment feature
  bool brightnessBoost;           // enables the brightness boosting feature
  uint8_t blankScreenMode2;       // turn-off display during a time interval in order to reduce tube wear (second profile) (1 = every day, 2 = on weekdays, 3 = on weekends)
  uint8_t blankScreenStartHr2;    // start hour for disabling the display (second profile)
  uint8_t blankScreenFinishHr2;   // finish hour for disabling the display (second profile)
  uint8_t reserved1[1];           // reserved for future use
  uint32_t settingsResetCode;     // all settings will be reset to default if this value is different than the value of SETTINGS_RESET_CODE
  AlarmEeprom_s alarm;            // alarm clock settings
  int8_t weekStartDay;            // the first day of a calendar week (1 = Monday, 7 = Sunday)
  uint8_t reserved2[3];           // reserved for future use
} Settings;


/*
 * Lookup table that maps the individual system settings 
 * to their ranges and IDs  
 */
struct {
  int8_t *value;     // pointer to the value variable
  uint8_t idDigit1;  // most significant digit to be displayed for the settings ID
  uint8_t idDigit0;  // least significant digit to be dispalyed for the settings ID
  int8_t minVal;     // minimum value
  int8_t maxVal;     // maximum value
  int8_t defaultVal; // default value
} SettingsLut[SETTINGS_LUT_SIZE] = 
{
  { (int8_t *)&Settings.blankScreenMode,        1, 1,     0,    4,     2 }, // screen blanking (0 = off, 1 = every day, 2 = on weekdays, 3 = on weekends, 4 = permanent)
  { (int8_t *)&Settings.blankScreenStartHr,     1, 2,     0,   23,     8 }, //  - start hour
  { (int8_t *)&Settings.blankScreenFinishHr,    1, 3,     0,   23,    17 }, //  - finish hour
  { (int8_t *)&Settings.blankScreenMode2,       2, 1,     0,    3,     1 }, // screen blanking (second profile) (0 = off, 1 = every day, 2 = on weekdays, 3 = on weekends)
  { (int8_t *)&Settings.blankScreenStartHr2,    2, 2,     0,   23,     2 }, //  - start hour
  { (int8_t *)&Settings.blankScreenFinishHr2,   2, 3,     0,   23,     5 }, //  - finish hour
  { (int8_t *)&Settings.cathodePoisonPrevent,   3, 1,     0,    3,     3 }, // cathode poisoning prevention (0 = off, 1 = on preset time, 2 = "Slot Machine" every minute, 3 = "Slot Machine" every 10 min)
  { (int8_t *)&Settings.cppStartHr,             3, 2,     0,   23,     4 }, //  - start hour
  { (int8_t *)&Settings.brightnessAutoAdjust,   4, 1, false, true,  true }, // brightness auto adjust
  { (int8_t *)&Settings.brightnessBoost,        4, 2, false, true, false }, //  - brighntess boost
  { (int8_t *)&Settings.dcfSyncEnabled,         5, 1, false, true,  true }, // DCF sync
  { (int8_t *)&Settings.dcfSignalIndicator,     5, 2, false, true,  true }, //  - signal indicator
  { (int8_t *)&Settings.dcfSyncHour,            5, 3,     0,   23,     3 }, //  - sync hour
  { (int8_t *)&Settings.secPerDayCorrect,       6, 1,   -99,   99,     0 }, // clock drift correction (seconds per day)
  { (int8_t *)&Settings.weekStartDay,           6, 2,     1,    7,     7 }  // start day of the week (1 = Monday, 7 = Sunday)
};

/*
 * Global variables
 */
struct {
  public:
    uint32_t dcfSyncInterval             = 0;      // DCF77 synchronization interval in minutes
    time_t lastDcfSyncTime               = 0;      // stores the time of last successful DCF77 synchronizaiton
    bool manuallyAdjusted                = true;   // prevent crystal drift compensation if clock was manually adjusted  
    bool dcfSyncActive                   = true;   // enable/disable DCF77 synchronization
    bool cppEffectEnabled                = false;  // Nixie digit cathod poison prevention effect is triggered every x seconds (avoids cathode poisoning) 
    volatile uint32_t secTickMsStamp     = 0;      // millis() at the last second tick, used for accurate crystal drift compensation
    volatile bool timer1TickFlag         = false;  // flag is set every second by the Timer1 ISR 
    volatile bool timer1PeriodUpdateFlag = false;  // flag is set whenever Timer1 period needs to be updated by the ISR
    volatile bool timer2PeriodUpdateFlag = false;  // flag is set whenever Timer2 period needs to be updated by the ISR
    volatile uint8_t timer2SecCounter    = 0;      // increments every time Timer2 ISR is called, used for converting 25ms into 1s ticks
    volatile uint8_t timer2TenthCounter  = 0;      // increments every time Timer2 ISR is called, used for converting 25ms into 1/10s ticks
  #ifdef SERIAL_DEBUG
    volatile uint8_t printTickCount      = 0;      // incremented by the Timer1 ISR every second
  #endif
    time_t systemTime                    = 0;      // current system time
    tm    *systemTm                      = NULL;   // pointer to the current system time structure
    NixieDigits_s timeDigits;                      // stores the Nixie display digit values of the current time
    NixieDigits_s dateDigits;                      // stores the Nixie display digit values of the current date 
    MenuState_e menuState = SHOW_TIME_E;           // state of the menu navigation state machine 

    // analog pins as an array
    uint8_t analogPin[NUM_APINS] = 
      { BUTTON0_APIN, BUTTON1_APIN, BUTTON2_APIN, LIGHTSENS_APIN, EXTPWR_APIN };       

    // dynamically defines the order of the menu items
    MenuState_e menuOrder[MENU_ORDER_LIST_SIZE] = 
       { SHOW_TIMER_E, SHOW_STOPWATCH_E, SHOW_SERVICE_E }; 

    int32_t debugValue[NUM_DEBUG_VALUES];          // values used for general purpose debugging
    
} G;

/*
 * Various objects
 */
ButtonClass Button[NUM_APINS]; // array of push button objects
AlarmClass Alarm;              // alarm clock object
CdTimerClass CdTimer;          // countdown timer object
StopwatchClass Stopwatch;      // stopwatch object





/***********************************
 * Arduino setup routine
 ***********************************/
void setup() {
  uint8_t i;
  time_t sysTime;
  MCUSR = 0;      // clear MCU status register
  wdt_disable (); // and disable watchdog

#ifdef SERIAL_DEBUG  
  // initialize serial port
  Serial.begin (SERIAL_BAUD);
#endif
  PRINTLN (" ");
  PRINTLN ("+ + +  N I X I E  C L O C K  + + +");
  PRINTLN (" ");

  // initialize the ADC
  Adc.initialize (ADC_PRESCALER_128, ADC_DEFAULT);

  // initialize the Nixie tube display
  Nixie.initialize ( NIXIE_MAX_NUM_TUBES,
          ANODE0_PIN, ANODE1_PIN, ANODE2_PIN, ANODE3_PIN, ANODE4_PIN, ANODE5_PIN,
          BCD0_PIN, BCD1_PIN, BCD2_PIN, BCD3_PIN, COMMA_PIN, &G.timeDigits);
  
  // initialize the brightness control algorithm
  Brightness.initialize (EEPROM_BRIGHTNESS_ADDR, BRIGHTNESS_PIN);

  // initilaize the DCF77 receiver
  Dcf.initialize (DCF_PIN, FALLING, INPUT);
  
  // reset system time
  set_system_time (0);
  sysTime = time (NULL);
  G.systemTm = localtime (&sysTime);

  // retrieve system settings period from EEOROM
  eepromRead (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
  PRINT   ("[setup] nixieUptime=");
  PRINTLN (Settings.nixieUptime, DEC);
  //PRINT   ("[setup] nixieUptimeResetCode=");
  //PRINTLN (Settings.nixieUptimeResetCode, HEX);

  // validate the Timer1 period loaded from EEPROM
  if (Settings.timer1Period < TIMER1_DEFUALT_PERIOD - 10000 ||
      Settings.timer1Period > TIMER1_DEFUALT_PERIOD + 10000) Settings.timer1Period = TIMER1_DEFUALT_PERIOD;

  // reset nixie tube uptime on first-time boot
  if (Settings.nixieUptimeResetCode != NIXIE_UPTIME_RESET_CODE) {
    Settings.nixieUptime = NIXIE_UPTIME_INIT_VAL;
    Settings.nixieUptimeResetCode = NIXIE_UPTIME_RESET_CODE;
    PRINTLN ("[setup] nixieUptime initialized");
  }

  // reset all settings on first-time boot
  if (Settings.settingsResetCode != SETTINGS_RESET_CODE) {
    Brightness.initializeLut ();
    Settings.alarm.minute = 0;
    Settings.alarm.hour = 0;
    Settings.alarm.mode = ALARM_OFF;
    Settings.alarm.lastMode = ALARM_OFF;
    for (i = 0; i < SETTINGS_LUT_SIZE; i++) {
      *SettingsLut[i].value = SettingsLut[i].defaultVal;
      Settings.settingsResetCode = SETTINGS_RESET_CODE;
      PRINTLN ("[setup] settings initialized");
    }
  }
  
  PRINTLN ("[setup] other:");
  // validate settings loaded from EEPROM
  for (i = 0; i < SETTINGS_LUT_SIZE; i++) {
    PRINT("  ");
    PRINT (SettingsLut[i].idDigit1, DEC); PRINT ("."); PRINT (SettingsLut[i].idDigit0, DEC); PRINT ("=");
    PRINTLN (*SettingsLut[i].value, DEC);
    if (*SettingsLut[i].value < SettingsLut[i].minVal || *SettingsLut[i].value > SettingsLut[i].maxVal) *SettingsLut[i].value = SettingsLut[i].defaultVal;
  }

  PRINT   ("[setup] timer1Period=");
  PRINTLN (Settings.timer1Period, DEC);
  
  // derive the seconds-per-day correction value from the current timer1Period
  Settings.secPerDayCorrect = TIMER1_TO_SEC_PER_DAY (Settings.timer1Period);
  //PRINT   ("[setup] secPerDayCorrect=");
  //PRINTLN (Settings.secPerDayCorrect, DEC);
  
  // initialize Timer 1 to trigger timer1ISR once per second
  Timer1.initialize (Settings.timer1Period);
  Timer1.attachInterrupt (timer1ISR);

  // initialize Timer2, set the period to 25ms (1s / 40)
  // Timer2 is used for Chronometer and Countdown Timer features
  // Timer2 has a maximum period of 32768us
  Timer2.initialize (Settings.timer1Period / 40); 
  Timer2.attachInterrupt (timer2ISR);
  cli ();
  Timer2.stop ();
  Timer2.restart ();
  sei ();
  G.timer2SecCounter = 0; 
  G.timer2TenthCounter = 0;
  
 
#ifndef SERIAL_DEBUG
  // initialize the Buzzer driver (requires serial communication pin)
  Buzzer.initialize (BUZZER_PIN);
  // enable/disable the brightness boost feature (requires serial communication pin)
  Brightness.boostEnable (Settings.brightnessBoost);
#endif

  // apply auto brightness setting
  Brightness.autoEnable (Settings.brightnessAutoAdjust);

  // initialize the alarm, countdown timer and stopwatch
  Alarm.initialize (&Settings.alarm);
  CdTimer.initialize (featureCallback);
  Stopwatch.initialize (featureCallback);

  #ifdef VALUE_DEBUG
  // initialize the debug values
  for (i = 0; i < NUM_DEBUG_VALUES; i++) {
    G.debugValue[i] = (int32_t)111111111 * (i + 1);
    if (i % 2 == 0) {
      G.debugValue[i] = -G.debugValue[i];
    }
  }
  #endif

  // enable the watchdog
  wdt_enable (WDT_TIMEOUT);
}
/*********/



/***********************************
 * Arduino main loop
 ***********************************/
void loop() {
  static bool cppCondition = false, blankCondition = false;
  static int8_t hour = 0, lastHour = 0, minute = 0, wday = 0;
  
  // actions to be executed once every second
  if (G.timer1TickFlag) {
    cli();
    G.systemTime     = time (NULL);    // get the current time
    sei();
    G.systemTm       = localtime (&G.systemTime);
    G.secTickMsStamp = millis ();
    updateDigits ();                      // update the Nixie display digits 
    G.timer1TickFlag = false;    
  }
                    
  lastHour = hour;
  hour     = G.systemTm->tm_hour;
  minute   = G.systemTm->tm_min;
  wday     = G.systemTm->tm_wday;

  // start DCF77 reception at the specified hour
  if (hour != lastHour && hour == Settings.dcfSyncHour) G.dcfSyncActive = true;

  Nixie.refresh (); // refresh the Nixie tube display
                    // refresh method is called many times across the code to ensure smooth display operation

  // enable cathode poisoning prevention effect at a preset hour
  if (Settings.cathodePoisonPrevent == 1 && hour == Settings.cppStartHr && G.menuState != SET_HOUR) {
    if (!cppCondition) {
      G.cppEffectEnabled = true;
      cppCondition = true;
    }  
  }
  else {
    G.cppEffectEnabled = false;  
    cppCondition = false;
  }

  Nixie.refresh ();
  
  // disable Nixie display at a preset hour interval in order to extend the Nixie tube lifetime
  // or permanently disable Nixie Display 
  if (Settings.blankScreenMode == 4 || 
       ( ( (
             (Settings.blankScreenMode >= 1 && Settings.blankScreenMode <= 3 ) && (
               (Settings.blankScreenStartHr <  Settings.blankScreenFinishHr &&  hour >= Settings.blankScreenStartHr && hour < Settings.blankScreenFinishHr  ) ||
               (Settings.blankScreenStartHr >= Settings.blankScreenFinishHr && (hour >= Settings.blankScreenStartHr || hour < Settings.blankScreenFinishHr) ) 
             ) &&
             (Settings.blankScreenMode != 2 || (wday >= 1 && wday <= 5) ) && (Settings.blankScreenMode != 3 || wday == 0 || wday == 6) 
           ) ||
           (
             (Settings.blankScreenMode2 >= 1 && Settings.blankScreenMode2 <= 3 ) && (
               (Settings.blankScreenStartHr2 <  Settings.blankScreenFinishHr2 &&  hour >= Settings.blankScreenStartHr2 && hour < Settings.blankScreenFinishHr2  ) ||
               (Settings.blankScreenStartHr2 >= Settings.blankScreenFinishHr2 && (hour >= Settings.blankScreenStartHr2 || hour < Settings.blankScreenFinishHr2) ) 
             ) &&
             (Settings.blankScreenMode2 != 2 || (wday >= 1 && wday <= 5) ) && (Settings.blankScreenMode2 != 3 || wday == 0 || wday == 6)
           )
         ) && !G.cppEffectEnabled
       )
     ) {
    if (!blankCondition && G.menuState == SHOW_TIME) {
      G.menuState = SHOW_BLANK_E;
      blankCondition = true;
    }
    // re-enable blanking after switching to the service display /*or at the change of an hour*/ 
    else if (G.menuState == SHOW_SERVICE /*|| ( G.menuState != SHOW_BLANK && G.menuState != SET_HOUR && hour != lastHour)*/ ) {
      blankCondition = false;
    }
  }
  // disable blanking if blanking blankScreenMode != 4, CPP is enabled or outside the preset time intervals
  else if (G.menuState == SHOW_BLANK) {
    G.menuState = SHOW_TIME_E;
    blankCondition = false;
  }
  // ensure that blankCondition is reset after blanking period elapses, even if not in blanking mode
  else if (blankCondition) {
    blankCondition = false;
  }

  Nixie.refresh ();

  // write-back system settings to EEPROM every night
  if (hour != lastHour && hour == 1 && G.menuState != SET_HOUR) {
      Nixie.blank ();
      eepromWrite (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
      Brightness.eepromWrite ();
      PRINTLN ("[loop] >EEPROM");   
  }

  Nixie.refresh ();

  // toggle the decimal point for the DCF signal indicator 
  if (Settings.dcfSyncEnabled) {  
    Nixie.comma[1] = G.dcfSyncActive && (Dcf.lastIrqTrigger || !Settings.dcfSignalIndicator);  // DCF77 sync status indicator
  }
  else {
    Nixie.comma[1] = false; 
  }

  Nixie.refresh ();

  adcRead ();               // process the ADC channels
  
  Nixie.refresh ();
 
  settingsMenu ();          // navigate the settings menu
  
  Nixie.refresh ();
  
  syncToDCF ();             // synchronize with DCF77 time

  Nixie.refresh ();
  
  CdTimer.loopHandler ();   // countdown timer loop handler 

  Nixie.refresh ();
  
  Stopwatch.loopHandler (); // stopwatch loop handler
 
  Nixie.refresh ();

  Alarm.loopHandler (hour, minute, wday, G.menuState != SET_MIN && G.menuState != SET_SEC);  // alarm clock loop handler

  Nixie.refresh ();
  
  Buzzer.loopHandler ();    // buzzer loop handler


#ifdef SERIAL_DEBUG  
  // print the current time
  if (G.printTickCount >= 15) {
    PRINT ("[loop] ");
    PRINTLN (asctime (localtime (&G.systemTime)));
    //PRINT ("[loop] nixieUptime=");
    //PRINTLN (Settings.nixieUptime, DEC);
    G.printTickCount = 0;
  }
#endif
}
/*********/





/***********************************
 * Timer ISR
 * Triggered once every second by Timer 1
 ***********************************/
void timer1ISR () {

  system_tick ();

  if (Nixie.enabled) Settings.nixieUptime++;

  // to avoid race conditions, Timer1 period must be updated from within the ISR
  if (G.timer1PeriodUpdateFlag) {
    Timer1.setPeriod (Settings.timer1Period);
    G.timer1PeriodUpdateFlag = false;
  }

  G.timer1TickFlag = true;

#ifdef SERIAL_DEBUG  
  G.printTickCount++;
#endif  
}
/*********/


/***********************************
 * Timer2 ISR
 * Triggered once every 25ms by Timer 2
 ***********************************/
void timer2ISR () {
    
  G.timer2SecCounter++;
  G.timer2TenthCounter++;
  
  // 1s period = 25ms * 40
  if (G.timer2SecCounter >= 40) {
    CdTimer.tick ();
    G.timer2SecCounter = 0;
  }

  // 1/10s period = 25ms * 4
  if (G.timer2TenthCounter >= 4) {
    Stopwatch.tick ();
    G.timer2TenthCounter = 0;
  }

  // to avoid race conditions, Timer2 period must be updated from within the ISR
  if (G.timer2PeriodUpdateFlag) {
    // initialize Timer2, set the period to 25ms (1s / 40)
    Timer2.setPeriod (Settings.timer1Period / 40);
    G.timer2PeriodUpdateFlag = false;
  }
}
/*********/


/***********************************
 * Watchdog expiry ISR
 * takes over system ticking during Deep Sleep
 ***********************************/
ISR (WDT_vect)  {
  system_tick ();
}
/*********/


/*********************************** 
 * Countdown timer and stopwatch callback function 
 ***********************************/
void featureCallback (bool start) {
  if (start) {
    Timer2.start ();
  }
  else {
    cli (); 
    Timer2.stop (); 
    Timer2.restart (); 
    sei (); 
    G.timer2SecCounter   = 0; 
    G.timer2TenthCounter = 0;   
  }
}
/*********/






/***********************************
 * Synchronize the system clock 
 * with the DCF77 time
 ***********************************/
void syncToDCF () {
  static bool coldStart = true;  // flag to indicate initial sync after power-up
  static bool enabledCondition = true;
  static int32_t lastDelta = 0;
  uint8_t rv;
  int32_t delta, deltaMs, ms;
  time_t timeSinceLastSync;
  time_t sysTime, dcfTime;

  // enable DCF77 pin interrupt
  if (Settings.dcfSyncEnabled && G.dcfSyncActive) {
    if (!enabledCondition) {
      Dcf.resumeReception ();
      PRINTLN ("[syncToDcf] resume");
      enabledCondition = true;
    }
  }
  else {
    if (enabledCondition) {
      Dcf.pauseReception ();
      PRINTLN ("[syncToDcf] pause");
      enabledCondition = false;
    }  
  }
  
  // read DCF77 time
  rv = Dcf.getTime ();

  Nixie.refresh ();  // refresh the Nixie tube display

  // DCF77 time has been successfully decoded
  if (G.dcfSyncActive && rv == 0) {
    ms      = millis () - G.secTickMsStamp;  // milliseconds elapsed since the last full second
    sysTime = G.systemTime;                  // get the current system time 
    dcfTime = mktime (&Dcf.currentTm);       // get the DCF77 timestamp                     
    
    delta   = (int32_t)(sysTime - dcfTime);           // time difference between the system time and DCF77 time in seconds     
    deltaMs = delta * 1000  + ms;                     // above time difference in milliseconds
    timeSinceLastSync = dcfTime - G.lastDcfSyncTime;  // time elapsed since the last successful DCF77 synchronization in seconds
    
    // if no big time deviation was detected or 
    // two consecutive DCF77 timestamps produce a similar time deviation
    // then update the system time 
    if (abs (delta) < 60 || abs (delta - lastDelta) < 60) {

      Timer1.stop ();
      Timer1.restart ();              // reset the beginning of a second 
      cli ();
      set_system_time (dcfTime - 1);  // apply the new system time, subtract 1s to compensate for initial tick
      sei ();
      Timer1.start ();
      G.lastDcfSyncTime = dcfTime;    // remember last sync time
      G.dcfSyncActive   = false;      // pause DCF77 reception
        

      // calibrate timer1 to compensate for crystal drift
      if (abs (delta) < 60 && timeSinceLastSync > 1800 && !G.manuallyAdjusted && !coldStart) {
        #ifdef VALUE_DEBUG
        // for debugging the calibration inaccuracy issue
        if (NUM_DEBUG_VALUES >= 6) {
          G.debugValue[0] = (int32_t)timeSinceLastSync;
          G.debugValue[1] = (int32_t)G.systemTm->tm_hour*10000 + (int32_t)G.systemTm->tm_min*100 + (int32_t)G.systemTm->tm_sec;
          G.debugValue[2] = (int32_t)Dcf.currentTm.tm_hour*10000 + (int32_t)Dcf.currentTm.tm_min*100 + (int32_t)Dcf.currentTm.tm_sec;
          G.debugValue[3] = delta;
          G.debugValue[4] = ms; 
          G.debugValue[5] = deltaMs;
        }
        #endif
        timerCalibrate (timeSinceLastSync, deltaMs);     
      }

      PRINTLN ("[syncToDCF] updated time");

      #ifdef SERIAL_DEBUG
      G.printTickCount   = 0;      // reset RS232 print period
      #endif

      G.manuallyAdjusted = false;  // clear the manual adjustment flag
      coldStart          = false;  // clear the initial startup flag
    }

    lastDelta = delta; // needed for validating consecutive DCF77 measurements against each other
  }

  #ifdef SERIAL_DEBUG  
  // debug printing
  // timestamp validation failed
  if (rv < 31) {
    PRINT   ("[syncToDCF] Dcf.getTime=");
    PRINTLN (rv, DEC);
    if (rv == 0) {
      PRINT   ("[syncToDCF] delta=");
      PRINTLN (delta, DEC);
    }
    PRINT ("[syncToDCF] ");
    PRINT   (asctime (&Dcf.currentTm));
    PRINTLN (" *");
  }
  // sync bit has been missed, bits buffer overflow
  else if (rv == 31) {
    PRINT   ("[syncToDCF] many bits=");
    PRINTLN (Dcf.lastIdx, DEC);
    Dcf.lastIdx = 0;
  }
  // missed bits or false sync bit detected
  else if (rv == 32) {
    PRINT   ("[syncToDCF] few bits=");
    PRINTLN (Dcf.lastIdx, DEC);
    Dcf.lastIdx = 0;
  }
  #endif

}
/*********/



/***********************************
 * Calibrate Timer1 frequency in order to compensate for the oscillator drift
 * Periodically back-up the Timer1 period to EEPROM
 ***********************************/
void timerCalibrate (time_t measDuration, int32_t timeOffsetMs) {
  int32_t errorPPM;

  errorPPM = (timeOffsetMs * 1000) / (int32_t)measDuration;

  #ifdef VALUE_DEBUG
  // for debugging the calibration inaccuracy issue
  if (NUM_DEBUG_VALUES >= 7) {
    G.debugValue[6] = errorPPM;
  }
  #endif
  
  cli ();
  Settings.timer1Period += errorPPM;
  sei ();

  // derive the seconds-per-day corretion value from the current timer1Period
  Settings.secPerDayCorrect = TIMER1_TO_SEC_PER_DAY (Settings.timer1Period);

  // to avoid race conditions, timer period is updated from within the ISRs
  G.timer1PeriodUpdateFlag = true;
  G.timer2PeriodUpdateFlag = true;

  PRINT   ("[timerCalibrate] measDuration=");
  PRINTLN (measDuration, DEC);
  PRINT   ("[timerCalibrate] timeOffsetMs=");
  PRINTLN (timeOffsetMs, DEC);
  PRINT   ("[timerCalibrate] errorPPM=");
  PRINTLN (errorPPM, DEC);
  PRINT   ("[timerCalibrate] timer1Period=");
  PRINTLN (Settings.timer1Period, DEC);
}
/*********/




/***********************************
 * Update the Nixie display digits
 ***********************************/
void updateDigits () {
  static int8_t lastMin = 0;
  
  // check whether current state requires time or date display
  G.timeDigits.value[0] = dec2bcdLow  (G.systemTm->tm_sec);
  G.timeDigits.value[1] = dec2bcdHigh (G.systemTm->tm_sec);
  G.timeDigits.value[2] = dec2bcdLow  (G.systemTm->tm_min);
  G.timeDigits.value[3] = dec2bcdHigh (G.systemTm->tm_min);
  G.timeDigits.value[4] = dec2bcdLow  (G.systemTm->tm_hour);
  G.timeDigits.value[5] = dec2bcdHigh (G.systemTm->tm_hour);
  G.dateDigits.value[0] = dec2bcdLow  (G.systemTm->tm_year);
  G.dateDigits.value[1] = dec2bcdHigh (G.systemTm->tm_year);
  G.dateDigits.value[2] = dec2bcdLow  (G.systemTm->tm_mon + 1);
  G.dateDigits.value[3] = dec2bcdHigh (G.systemTm->tm_mon + 1);
  G.dateDigits.value[4] = dec2bcdLow  (G.systemTm->tm_mday);
  G.dateDigits.value[5] = dec2bcdHigh (G.systemTm->tm_mday); 

  
  if (G.menuState == SHOW_TIME) {
    // trigger Nixie digit "Slot Machine" effect
    if (G.systemTm->tm_min != lastMin && (Settings.cathodePoisonPrevent == 2 || (Settings.cathodePoisonPrevent == 3 && G.timeDigits.value[2] == 0))) {
      Nixie.slotMachine();
    }
    // trigger the cathode poisoning prevention routine
    if (Settings.cathodePoisonPrevent == 1 && G.cppEffectEnabled && G.timeDigits.value[0] == 0) {
      Nixie.setBrightness (Brightness.maximum ());
      Nixie.cathodePoisonPrevent ();
    }
  }
  lastMin = G.systemTm->tm_min;
}
/*********/





/***********************************
 * Read ADC channels
 ***********************************/
void adcRead (void) {
  static uint8_t chanIdx = 0;
  static uint32_t lightsensTs = 0;
  static int32_t avgVal[NUM_APINS] = { 1023, 1023, 1023 };
  uint32_t ts;
  int16_t adcVal;
  uint8_t val;
  
  // start ADC for the current button (will be ignored if already started)
  Adc.start (G.analogPin[chanIdx]);

  // check if ADC finished detecting the value
  adcVal = Adc.readVal ();
 
  // ADC finished
  if (adcVal >= 0) {
    
    ts = millis ();
    
    // process light sensor value
    if (G.analogPin[chanIdx] == LIGHTSENS_APIN) {
     // reduce light sensor sampling frequency
     // disable auto-brightness when a button is pressed to avoid ADC channel cross-talk
     // disable auto-brightness during cathode poisoning prevention
     if (ts - lightsensTs > 150 && !Button[0].pressed && !Button[1].pressed && !Button[2].pressed && !Nixie.cppEnabled) { 
        avgVal[chanIdx] = (avgVal[chanIdx] * 31 + (int32_t)adcVal) >> 5;  // IIR low-pass filtering for smooth transitions
        val = Brightness.lightSensorUpdate ((int16_t)avgVal[chanIdx]);
        Nixie.setBrightness (val);
        lightsensTs = ts;
      }     
    }
    // check external power voltage
    else if (G.analogPin[chanIdx] == EXTPWR_APIN) {
      if (adcVal < 512) powerSave();  // enable power save mode if external power is lost
    }
    // process button values
    else {
      avgVal[chanIdx] = (avgVal[chanIdx] * 15 + (int32_t)adcVal) >> 4;      // IIR low-pass filtering for button debouncing
      if (avgVal[chanIdx] < 512) {
        if (Button[chanIdx].pressed == false) Nixie.resetBlinking();       // synchronize digit blinking with the rising edge of a button press
        Button[chanIdx].press (); 
      }
      else {
        Button[chanIdx].release ();
      }
    }
    chanIdx++;
    if (chanIdx >= NUM_APINS) chanIdx = 0;
  }
  
}
/*********/


/***********************************
 * Enter power save mode
 ***********************************/
void powerSave (void) {
  enum { LIGHT_SLEEP, DEEP_SLEEP } mode = LIGHT_SLEEP;
  // threshold power supply voltage for going into deep sleep [0..1023] 1023 ~= 4.5V
  const int16_t voltageThreshold = 800;  // 800 ~= 3.5V
  int16_t voltage = 0;
  uint8_t i;

  // write-back system settings to EEPROM
  eepromWrite (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
  Brightness.eepromWrite ();
  
  analogReference (INTERNAL);       // set ADC reference to internal 1.1V source (required for measuring power supply voltage)
  for (i = 0; i < 100 && voltage < voltageThreshold; i++) voltage = analogRead (VOLTAGE_APIN); // stabilize voltage reading
  Nixie.blank ();                   // turns-off all digital outputs
  Brightness.boostDeactivate ();    // make sure that BRIGHTNESS_PIN is LOW
  wdt_disable ();                   // disable watchdog timer  
  power_all_disable ();             // turn off peripherals
  power_timer1_enable ();           // power on Timer 1
  power_adc_enable ();              // power on the ADC
  set_sleep_mode (SLEEP_MODE_IDLE); // configure lowest sleep mode that keeps clk_IO for Timer 1
  
  // stay in this loop until external power is re-connected
  // DCF_PIN is pulled-up when external power is connected
  while (digitalRead (DCF_PIN) == LOW) {

    // Light Sleep: 
    // - turnoff all peripherals except for Timer1 and ADC
    // - accurate time-keeping using Timer1 interrupt
    // - periodically check supply voltage and go to Deep Sleep when below threshold
    // - measured current consuption approx. 2.4mA
    if (mode == LIGHT_SLEEP) {
      
      // enter sleep, wakeup will be triggered by the 
      // next Timer 1 interrupt (second tick)
      sleep_enable (); 
      sleep_cpu ();
      sleep_disable ();

      // read the super-capacitor voltage
      voltage = analogRead (VOLTAGE_APIN);

      // if below a certain voltage threshold, then switch to deep sleep
      if (voltage < voltageThreshold) {
        power_timer1_disable ();
        ADCSRA &= ~_BV(ADEN);  // Disable ADC, see ATmega328P datasheet Section 28.9.2
        power_adc_disable ();
        cli ();
        WDTCSR |= _BV(WDCE) | _BV(WDE);
        WDTCSR = _BV(WDIE) | _BV(WDP2) | _BV(WDP1); // Enable watchdog interrupt, set to 1s (see Datasheet Section 15.9.2)
        sei ();
        set_sleep_mode (SLEEP_MODE_PWR_DOWN);
        mode = DEEP_SLEEP;
        G.dcfSyncActive = true;        // time must be synced to DCF77 upon power re-connect
        G.manuallyAdjusted = true;     // inaccurate time-keeping should never be used for Timer1 calibration
      }  
    }
    // Deep Sleep:
    // - turn off all of the remaining peripherals
    // - inaccurate time-keeping using the Watchdog interrupt
    // - measured current consumption approx. 180uA
    else { // mode == DEEP_SLEEP
      
      // enter sleep, wakeup will be triggered by the 
      // next DCF_PIN interrupt (if external power is connected)
      // or by the watchdog timer interrupt (for incrementing seconds)
      cli ();
      sleep_enable (); 
      sleep_bod_disable();  // disable brown-out detection (not sure if actually works)
      sei ();
      sleep_cpu ();
      sleep_disable ();
    }
        
  } // while (digitalRead (DCF_PIN) == LOW)
  
  power_all_enable();       // turn on peripherals
  ADCSRA |= _BV(ADEN);      // Enable ADC, see ATmega328P datasheet Section 28.9.2
  wdt_enable (WDT_TIMEOUT); // enable watchdog timer
  
#ifdef SERIAL_DEBUG
  delay (500);
  PRINT   ("[powerSave] mode=");
  PRINTLN (mode, DEC);
#endif
}
/*********/




/***********************************
 * Performs dynamic reordering of menu items
 * the last used feature shall appear first in the
 * menu list
 ***********************************/
void reorderMenu (int8_t menuIndex) {
  static MenuState_e lastState = SHOW_TIME;
  MenuState_e temp; 

  // never call the function more than once per menu state
  // ensure that menuIndex is within range
  if (G.menuState != lastState && menuIndex > 0 && menuIndex < MENU_ORDER_LIST_SIZE) {
    
    temp = G.menuOrder[menuIndex-1];
    for (int8_t i = menuIndex-1; i > 0; i--) {
      G.menuOrder[i] = G.menuOrder[i-1];
    }
    G.menuOrder[0] = temp;
    
    lastState = G.menuState;
  }
}
/*********/


/***********************************
 * Code snippet for setting time/date value
 * called repeatedly from within settingsMenu()
 ***********************************/
#define SET_TIME_DATE( EXPR ) { \
  cli (); \
  t = G.systemTm; \
  EXPR; \
  sysTime = mktime (t); \
  set_system_time (sysTime); \
  updateDigits (); \
  sei (); \
  G.dcfSyncActive = true; \
  G.manuallyAdjusted = true; \
}
/*********/


/***********************************
 * Implements the settings menu 
 * navigation structure
 ***********************************/
void settingsMenu (void) {
  static MenuState_e nextState = SHOW_DATE_E;
  static MenuState_e returnState = SHOW_TIME_E; 
  static int8_t menuIndex = 0;
  static const uint32_t scrollDelayDefault = 300, menuTimeoutDefault = 30*1000, menuTimeoutExtended = 60*60000, menuTimeoutNextState = 5*1000;
  static uint32_t timeoutTs = 0, scrollTs = 0, brightnessTs = 0, accelTs = 0, bannerTs = 0;
  static uint32_t scrollDelay = scrollDelayDefault, menuTimeout = menuTimeoutDefault/*, scrollCount = 0*/;
  static NixieDigits_s valueDigits;
  static int8_t sIdx = 0, vIdx = 0;
  static bool brightnessEnable = false;
  uint8_t i;
  uint8_t valU8;
  int8_t val8;
  int16_t val16;
  time_t sysTime;
  tm *t;
  uint32_t ts = millis ();

  // button 0 - rising edge --> initiate a long press
  if (Button[0].rising ()) {
    timeoutTs = ts;  // reset the menu timeout
  }
  
  Nixie.refresh ();  // refresh the Nixie tube display

  // modes where buttons 1 and 2 are used for digit/setting adjustments 
  // using the progressive scrolling acceleration feature
  if ( G.menuState == SHOW_TIMER || (G.menuState >= SET_ALARM && G.menuState != SET_SEC) ) {
    // button 1 or 2 - rising edge --> initiate a long press
    if (Button[1].rising () || Button[2].rising ()){
      accelTs = ts;
      timeoutTs = ts; // reset the menu timeout
      scrollTs -= scrollDelay; // ensure digits are updated immediately after a button press
    }
    // button 1 or 2 - long press --> further accelerate scrolling digits
    else if ( (Button[1].pressed || Button[2].pressed) && ts - accelTs >= 8000) {
      scrollDelay = 20;
    }
    // button 1 or 2 - long press --> further accelerate scrolling digits
    else if ( (Button[1].pressed || Button[2].pressed) && ts - accelTs >= 5000) {
      scrollDelay = 75;
    }
    // button 1 or 2 - long press --> accelerate scrolling digits
    else if ( (Button[1].pressed || Button[2].pressed) && ts - accelTs >= 2000) {
      scrollDelay = 150;
    }
    // button 1 or 2 - falling edge --> reset scroll speed
    else if (Button[1].fallingContinuous () || Button[2].fallingContinuous ()) {
      scrollDelay = scrollDelayDefault; 
      /*scrollCount = 0;*/ 
    }
  }

  Nixie.refresh ();  // refresh the Nixie tube display

  // in timekeeping and alarm modes, buttons 1 and 2 are used switching to the date and alarm modes 
  // or for adjusting display brightness when long-pressed
  // or for snoozing or resetting the alarm
  if (G.menuState == SHOW_TIME || G.menuState == SHOW_DATE || G.menuState == SHOW_ALARM || G.menuState == SHOW_WEEK) {
    // button 1  or 2- rising edge --> initiate a long press
    if (Button[1].rising () || Button[2].rising ()) {
      timeoutTs = ts;
      brightnessEnable = true;
    }
    // button 1 - falling edge --> snooze alarm or change state: SHOW_DATE->SHOW_WEEK->SHOW_ALARM->SHOW_TIME 
    else if (Button[1].falling ()) {
      if (Alarm.alarm) Alarm.snooze ();
      else if (G.menuState == SHOW_TIME) G.menuState = SHOW_DATE_E;
      else if (G.menuState == SHOW_DATE) G.menuState = SHOW_WEEK_E;
      else if (G.menuState == SHOW_WEEK) G.menuState = SHOW_ALARM_E;
      else if (G.menuState == SHOW_ALARM) G.menuState = SHOW_TIME_E;
    }
    // button 2 - falling edge --> snooze alarm or change state: SHOW_ALARM->SHOW_WEEK->SHOW_DATE->SHOW_TIME
    else if (Button[2].falling ()) {
      if (Alarm.alarm) Alarm.snooze ();
      else if (G.menuState == SHOW_TIME) G.menuState = SHOW_ALARM_E;
      else if (G.menuState == SHOW_ALARM) G.menuState = SHOW_WEEK_E;
      else if (G.menuState == SHOW_WEEK) G.menuState = SHOW_DATE_E;
      else if (G.menuState == SHOW_DATE) G.menuState = SHOW_TIME_E;
    }
    else if (Alarm.alarm || Alarm.snoozing) {
      // button 1 or 2 - long press --> reset alarm
      if (Button[1].longPress () || Button[2].longPress ()) Alarm.resetAlarm ();
      brightnessEnable = false;
    }
    else if ( G.menuState == SHOW_ALARM) {
      // button 1 long press --> toggle alarm active
      if (Button[1].longPress ()) {
        Alarm.modeToggle ();
      }
    }
    else if ( G.menuState == SHOW_DATE || G.menuState == SHOW_WEEK) {
      // button 1 long press --> toggle DCF77 sync status
      if (Button[1].longPress ()) {
        G.dcfSyncActive = !G.dcfSyncActive;
      }
    }
    else if ( G.menuState == SHOW_TIME) {
      // button 1 or 2 - long press --> increase/decrease brightness
      if ((Button[1].longPressContinuous () || Button[2].longPressContinuous ()) && ts - brightnessTs >= 50 && brightnessEnable) {
        if (Button[1].pressed) valU8 = Brightness.increase ();
        else                   valU8 = Brightness.decrease ();
        Nixie.setBrightness (valU8);
        Nixie.refresh ();
        brightnessTs = ts;
      }    
    }
  }

  Nixie.refresh ();  // refresh the Nixie tube display

  // modes where short-pressing button 0 is used for switching to the next display mode
  // when alarm(s) are active short-pressing button 0 will cancel or snooze the alarm(s) instead
  if ( (G.menuState >= SHOW_TIME && G.menuState <= SHOW_SERVICE) || G.menuState >= SET_HOUR || CdTimer.alarm || Alarm.alarm) {
    // button 0 - falling edge --> reset alarms or change state: nextState
    if (Button[0].falling ()) {
      if (G.cppEffectEnabled)  G.cppEffectEnabled = false; // disable cathode poisoning prevention effect
      if (Alarm.alarm || CdTimer.alarm) {
        Alarm.snooze ();       // snooze the alarm clock
        CdTimer.resetAlarm (); // reset alarm of the countdown timer feature
      }
      else {
        menuTimeout = menuTimeoutDefault;
        G.menuState = nextState;
      }
    }
  }

  // in selected modes, long-pressing button 0 shall switch to the pre-defined setting mode or display mode
  // when the alarm clock is snoozed or active, long-pressing button 0 will cancel the alarm
  if (G.menuState == SHOW_TIME || G.menuState == SHOW_DATE || G.menuState == SHOW_ALARM || G.menuState == SHOW_WEEK ||
      G.menuState == SHOW_SERVICE || G.menuState >= SET_ALARM) {
    // button 0 - long press --> reset alarm or change state: returnState 
    if (Button[0].longPress ()) {
      if (Alarm.alarm || Alarm.snoozing) {
        Alarm.resetAlarm ();  // stop the alarm clock
      }
      else {
        G.menuState = returnState;
      }
    }
  }


  Nixie.refresh ();  // refresh the Nixie tube display

  // timeout --> change state: SHOW_TIME
  if (G.menuState != SHOW_BLANK_E && G.menuState != SHOW_BLANK && G.menuState != SHOW_TIME) {
    if (ts - timeoutTs > menuTimeout) {
      menuTimeout = menuTimeoutDefault;
      G.menuState = SHOW_TIME_E;
    }
  }

  // timeout --> return to time display upon pressing button 0
  if (G.menuState == SHOW_TIMER || G.menuState == SHOW_STOPWATCH) {
    if (ts - timeoutTs > menuTimeoutNextState) {
      nextState = SHOW_TIME_E;
    }
  }

  // if alarm is ringing the switch to the corresponding display mode
  if (Alarm.alarm && G.menuState != SHOW_TIME) G.menuState = SHOW_TIME_E;
  if (CdTimer.alarm && G.menuState != SHOW_TIMER) G.menuState = SHOW_TIMER_E;
  
  Nixie.refresh ();  // refresh the Nixie tube display

  // ensure not to access outside of array bounds
  if (menuIndex >= MENU_ORDER_LIST_SIZE) menuIndex = 0;

  // main state machines that implements the menu navigation with the different 
  // display and setting modes
  // - each state is entered through a corresponding entry state (with _E suffix)
  // - each state defines the next state to be selected via short-pressing button 0 (nextState)
  // - a feature has been used (by pressing one or more buttons), pressing button 0 will return to the clock display state
  // - setting state may define the return state to be selected by long-pressing button 0 (returnState)
  // - in order to reduce the code size functionality that is common to several states is nested within
  //   the conditional statements defined above
  switch (G.menuState) {

    /*################################################################################*/
    case SHOW_TIME_E:
      Nixie.enable (true);
      Nixie.cancelScroll ();
      Nixie.setDigits (&G.timeDigits);
      for (i = 0; i < 6; i++) G.timeDigits.blnk[i] = false;
      menuIndex = 0;
      nextState = G.menuOrder[menuIndex]; // switch to this state after short-pressing button 0
                                                     // use dynamic menu ordering
      returnState = SET_HOUR_E;                      // switch to this state after long-pressing button 0
      G.menuState = SHOW_TIME;
    case SHOW_TIME:
      break;
      
    /*################################################################################*/
    case SHOW_DATE_E:
      Nixie.setDigits (&G.dateDigits);
      G.dateDigits.comma[4] = true;
      G.dateDigits.comma[2] = true;
      for (i = 0; i < 6; i++) G.dateDigits.blnk[i] = false;
      //menuIndex = 0;
      nextState   = SHOW_TIME_E;
      returnState = SET_DAY_E;
      G.menuState = SHOW_DATE;
    case SHOW_DATE:
      // do nothing
      break;

    /*################################################################################*/
    case SHOW_WEEK_E: 
      Nixie.resetDigits (&valueDigits);
      Nixie.setDigits (&valueDigits); 
      valU8 = week_of_year (G.systemTm, (Settings.weekStartDay == 7 ? 0 : Settings.weekStartDay)) + 1;
      valueDigits.value[0] = dec2bcdLow (valU8);
      valueDigits.value[1] = dec2bcdHigh (valU8);
      valueDigits.blank[2] = true;
      valueDigits.blank[3] = true;
      valueDigits.value[4] = (uint8_t)(G.systemTm->tm_wday == 0 ? 7 : G.systemTm->tm_wday);
      valueDigits.blank[5] = true;
      nextState   = SHOW_TIME_E;
      returnState = SHOW_WEEK_E;
      G.menuState = SHOW_WEEK; 
    case SHOW_WEEK:
      // do nothing
      break;

    /*################################################################################*/
    case SHOW_ALARM_E:
      Nixie.setDigits (&Alarm.digits);
      Alarm.displayRefresh ();
      for (i = 0; i < 6; i++) Alarm.digits.blnk[i] = false;
      //menuIndex = 0;
      nextState   = SHOW_TIME_E;
      returnState = SET_ALARM_E;
      G.menuState = SHOW_ALARM;
    case SHOW_ALARM:
      // do nothing
      break;

    /*################################################################################*/
    case SHOW_TIMER_E:
      Nixie.enable (true);
      Nixie.setDigits (&CdTimer.digits);
      if (!CdTimer.running && !CdTimer.alarm && !Stopwatch.active) CdTimer.reset ();
      menuIndex++;
      nextState   = G.menuOrder[menuIndex];
      menuTimeout = menuTimeoutExtended; // extend the menu timeout
      G.menuState = SHOW_TIMER;
    case SHOW_TIMER:
      // reset the menu timeout as long as timer is running
      if (CdTimer.running && ts - timeoutTs > menuTimeout - 1000) timeoutTs = ts;
      
      // button 0 - long press --> reset the countdown timer
      if (Button[0].longPress ()) { 
        if (CdTimer.active) {
          CdTimer.reset (); 
        }
        else {
          Stopwatch.reset ();
          CdTimer.start ();
        }
        nextState = SHOW_TIME_E; // if feature was used, return to time display upon pressing button 0
        reorderMenu (menuIndex); // if feature was used, it will appear first once the menu is accessed
      }
      // button 1 - falling edge --> increment minutes / arm countdown timer
      else if (Button[1].falling ()) {
        if (!CdTimer.active) Stopwatch.reset ();
        if (CdTimer.alarm) CdTimer.resetAlarm ();
        else               CdTimer.minuteIncrease ();  
        nextState = SHOW_TIME_E;
        reorderMenu (menuIndex);
      }
      // button 2 - falling edge --> decrement minutes / arm countdown timer
      else if (Button[2].falling ()) {
        if (!CdTimer.active) Stopwatch.reset ();
        if (CdTimer.alarm) CdTimer.resetAlarm ();
        else               CdTimer.minuteDecrease ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIndex);
      }
      // button 1 or 2 - long press --> increase/decrease /*seconds then*/ minutes / arm countdown timer
      else if (Button[1].longPressContinuous () || Button[2].longPressContinuous ()) {
        if (ts - scrollTs >= scrollDelay) {
          if (!CdTimer.active) Stopwatch.reset ();
          /*if (scrollCount < 6) {
            if (Button[1].pressed) CdTimer.secondIncrease ();
            else                   CdTimer.secondDecrease ();
            accelTs = ts; scrollDelay = scrollDelayDefault;
          }
          else {*/
          if (Button[1].pressed) CdTimer.minuteIncrease ();
          else                   CdTimer.minuteDecrease ();
          /*}*/
          CdTimer.resetAlarm ();
          scrollTs  = ts;
          nextState = SHOW_TIME_E;
          reorderMenu (menuIndex);
          /*scrollCount++;*/
        }
      }
      break;

    /*################################################################################*/
    case SHOW_STOPWATCH_E:
      Nixie.setDigits (&Stopwatch.digits);
      if (!Stopwatch.active && !CdTimer.active) Stopwatch.reset ();
      menuIndex++;
      nextState   = G.menuOrder[menuIndex];
      menuTimeout = menuTimeoutExtended; // extend the menu timeout
      G.menuState = SHOW_STOPWATCH;
    case SHOW_STOPWATCH:
      // reset the menu timeout as long as stopwatch is running
      if (Stopwatch.running && ts - timeoutTs > menuTimeout - 1000) timeoutTs = ts;
    
      // button 0 - long press --> reset the stopwatch
      if (Button[0].longPress ()) { 
        if (!CdTimer.active) {
          Stopwatch.reset ();
          nextState = SHOW_TIME_E;
          reorderMenu (menuIndex);
        } 
      }
      // button 1 rising edge --> start/stop stopwatch
      else if (Button[1].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        if (Stopwatch.running) Stopwatch.stop ();
        else CdTimer.reset(), Stopwatch.start ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIndex);
      }
      // button 2 rising edge --> toggle pause stopwatch
      else if (Button[2].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        if (Stopwatch.active) {
          if (Stopwatch.paused) Stopwatch.pause (false);
          else                  Stopwatch.pause (true); 
          nextState = SHOW_TIME_E;
          reorderMenu (menuIndex);
        }
      }
      break;

    /*################################################################################*/
    case SHOW_SERVICE_E:
      Nixie.resetDigits (&valueDigits);
      Nixie.setDigits (&valueDigits);   
      bannerTs = ts;
      vIdx = 0;
      nextState   = SHOW_TIME_E;
      returnState = SET_SETTINGS_E;
      G.menuState = SHOW_SERVICE;
      goto SHOW_SERVICE_ENTRY_POINT;
    case SHOW_SERVICE:      
      // button 1 or 2 rising edge --> cycle back and forth between system parameters
      if (Button[1].rising () || Button[2].rising ()){
        timeoutTs = ts;  // reset the menu timeout
        bannerTs  = ts;  // reset display scroll period
        Nixie.cancelScroll ();
        Nixie.resetDigits (&valueDigits);
        if      (Button[1].pressed) {
          vIdx++; if (vIdx >= NUM_SERVICE_VALUES) vIdx = 0;
        }
        else if (Button[2].pressed) {
          vIdx--; if (vIdx < 0) vIdx = NUM_SERVICE_VALUES - 1;
        }
        SHOW_SERVICE_ENTRY_POINT:
        // show Timer1 period (default)
        if (vIdx == 0) {
          valueDigits.numDigits = 9;
          Nixie.dec2bcd (Settings.timer1Period, &valueDigits, 7);
          valueDigits.value[8] = 1;
          valueDigits.comma[8] = true;
          valueDigits.blank[7] = true;
        }
        // show Nixie tube uptime
        else if (vIdx == 1) {
          valueDigits.numDigits = 8;
          cli ();
          Nixie.dec2bcd (Settings.nixieUptime / 3600, &valueDigits, 6);
          sei ();
          valueDigits.value[7] = 2;
          valueDigits.comma[7] = true;
          valueDigits.blank[6] = true;
        }
        // show firmware version
        else if (vIdx == 2) {
          valueDigits.numDigits = 8;
          valueDigits.value[7] = 3;
          valueDigits.comma[7] = true;
          valueDigits.blank[6] = true; 
          valueDigits.value[5] = dec2bcdHigh (VERSION_MAJOR);
          valueDigits.value[4] = dec2bcdLow (VERSION_MAJOR);
          valueDigits.comma[4] = true;
          valueDigits.value[3] = dec2bcdHigh (VERSION_MINOR);
          valueDigits.value[2] = dec2bcdLow (VERSION_MINOR);
          valueDigits.comma[2] = true;    
          valueDigits.value[1] = dec2bcdHigh (VERSION_MAINT);
          valueDigits.value[0] = dec2bcdLow (VERSION_MAINT);  
        }
        #ifdef VALUE_DEBUG   
        // show the Debug values
        else if (vIdx > 2 && vIdx < NUM_SERVICE_VALUES) {
          uint8_t idx = vIdx - (NUM_SERVICE_VALUES - NUM_DEBUG_VALUES);
          valueDigits.numDigits = NUM_DEBUG_DIGITS + 2;
          valueDigits.value[NUM_DEBUG_DIGITS + 1] = idx + 1;
          valueDigits.comma[NUM_DEBUG_DIGITS]     = true;
          valueDigits.blank[NUM_DEBUG_DIGITS]     = true;
          if (idx < NUM_DEBUG_VALUES)  {   
            if (G.debugValue[idx] < 0) {
              Nixie.dec2bcd ((uint32_t)(-G.debugValue[idx]), &valueDigits, NUM_DEBUG_DIGITS);
              for (i = 0; i < NUM_DEBUG_DIGITS; i++) valueDigits.comma[i] = true;
            }
            else {
              Nixie.dec2bcd ((uint32_t)G.debugValue[idx], &valueDigits, NUM_DEBUG_DIGITS);
            }
          }
          else {
            Nixie.dec2bcd (0, &valueDigits, NUM_DEBUG_DIGITS);
          }
        }
        #endif

        Nixie.scroll ();
      }
      // scroll the display every x seconds
      if (ts - bannerTs > 6000) Nixie.scroll (), bannerTs = ts; 
      break;
    
    /*################################################################################*/
    case SHOW_BLANK_E:
      Nixie.enable (false);
      G.menuState = SHOW_BLANK;
    case SHOW_BLANK:
      // button 0, 1 or 2 - rising edge --> catch rising edge
      Button[0].rising ();
      Button[1].rising ();
      Button[2].rising ();
      // button 0, 1 or 2 - falling edge --> re-activate display
      if (Button[0].falling () || Button[1].falling () || Button[2].falling ()) {
        G.menuState = SHOW_TIME_E;
      }
      break;

    /*################################################################################*/
    case SET_ALARM_E:
      Nixie.setDigits (&Alarm.digits);
      Alarm.displayRefresh ();
      for (i = 0; i < 6; i++) Alarm.digits.blnk[i] = false;
      sIdx = 0;
      Alarm.digits.blnk[5] = true;
      Alarm.digits.blnk[4] = true;
      returnState = SHOW_ALARM_E;
      G.menuState = SET_ALARM;
    case SET_ALARM:
      // button 0 - falling edge --> select next setting value
      if (Button[0].falling ()) {   
        sIdx++; if (sIdx > 2) sIdx = 0;
        if (sIdx == 0) {
          Alarm.digits.blnk[5] = true;
          Alarm.digits.blnk[4] = true;
          Alarm.digits.blnk[1] = false;
          Alarm.digits.blnk[0] = false;
        }
        else if (sIdx == 1) {
          Alarm.digits.blnk[5] = false;
          Alarm.digits.blnk[4] = false;
          Alarm.digits.blnk[3] = true;
          Alarm.digits.blnk[2] = true;
        }
        else if (sIdx == 2) {
          Alarm.digits.blnk[3] = false;
          Alarm.digits.blnk[2] = false;
          Alarm.digits.blnk[1] = true;
          Alarm.digits.blnk[0] = true;
        }
      }
      // button 1 or 2 - pressed --> increase/decrease value
      else if (Button[1].pressed || Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          if (sIdx == 0) {
            if (Button[1].pressed) Alarm.hourIncrease ();
            else                   Alarm.hourDecrease ();
          }
          else if (sIdx == 1) {
            if (Button[1].pressed) Alarm.minuteIncrease ();
            else                   Alarm.minuteDecrease ();
          }
          else if (sIdx == 2) {
            if (Button[1].pressed) Alarm.modeIncrease ();
            else                   Alarm.modeDecrease ();
          }
          scrollTs = ts;
        }   
      }
      break;
    
    /*################################################################################*/
    case SET_SETTINGS_E:
      Nixie.cancelScroll ();
      Nixie.setDigits (&valueDigits);
      valueDigits.blnk[0]  = true;
      valueDigits.blnk[1]  = true;
      valueDigits.blnk[4]  = false;
      valueDigits.blnk[5]  = false;
      valueDigits.blank[2] = true;
      valueDigits.blank[3] = true;
      valueDigits.comma[5] = true;
      sIdx = 0;
      valueDigits.value[5] = SettingsLut[sIdx].idDigit1;
      valueDigits.value[4] = SettingsLut[sIdx].idDigit0;
      val8 = *SettingsLut[sIdx].value;
      valueDigits.comma[2] = (val8 < 0);
      valueDigits.value[1] = dec2bcdHigh ((uint8_t)abs(val8)); 
      valueDigits.value[0] = dec2bcdLow ((uint8_t)abs(val8)); 
      returnState = SHOW_TIME_E;
      G.menuState = SET_SETTINGS;
    case SET_SETTINGS:
      // button 0 - falling edge --> select next setting value
      if (Button[0].falling ()) {   
        sIdx++;
        if (sIdx >= SETTINGS_LUT_SIZE) sIdx = 0;
        valueDigits.value[5] = SettingsLut[sIdx].idDigit1;
        valueDigits.value[4] = SettingsLut[sIdx].idDigit0;
        val8 = *SettingsLut[sIdx].value;
        valueDigits.comma[2] = (val8 < 0);
        valueDigits.value[1] = dec2bcdHigh ((uint8_t)abs(val8)); 
        valueDigits.value[0] = dec2bcdLow ((uint8_t)abs(val8));  
      }
      // button 1 or 2 pressed --> increase/decrease value
      else if (Button[1].pressed || Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          val16 = (int16_t)*SettingsLut[sIdx].value;
          if (Button[1].pressed) {
            val16++;
            if (val16 > SettingsLut[sIdx].maxVal) val16 = SettingsLut[sIdx].minVal; 
          }
          else {
            val16--;
            if (val16 < SettingsLut[sIdx].minVal) val16 = SettingsLut[sIdx].maxVal;
          }
          *SettingsLut[sIdx].value = (int8_t)val16;
          valueDigits.comma[2] = (val16 < 0);
          valueDigits.value[1] = dec2bcdHigh ((uint8_t)abs(val16)); 
          valueDigits.value[0] = dec2bcdLow ((uint8_t)abs(val16));
          Nixie.refresh ();
          scrollTs = ts;
          
          // if seconds-per-day correction value has been set
          if (SettingsLut[sIdx].value == &Settings.secPerDayCorrect) {
            // derive new timer1Period 
            cli ();
            Settings.timer1Period = SEC_PER_DAY_TO_TIMER1 (Settings.secPerDayCorrect);
            sei (); 
            // to avoid race conditions, timer period is updated from within the ISRs
            G.timer1PeriodUpdateFlag = true;
            G.timer2PeriodUpdateFlag = true;
          } 
          // if auto brightness feature was activated/deactivated
          else if (SettingsLut[sIdx].value == (int8_t *)&Settings.brightnessAutoAdjust) {
            Brightness.autoEnable (Settings.brightnessAutoAdjust);
          }
        #ifndef SERIAL_DEBUG
          // if the brighness boost feature has been activated/deactivated
          else if (SettingsLut[sIdx].value == (int8_t *)&Settings.brightnessBoost) {
            Brightness.boostEnable (Settings.brightnessBoost);
          }
        #endif
        }
      }
      
      break;
    
    /*################################################################################*/
    case SET_HOUR_E:
      Nixie.cancelScroll ();
      Nixie.setDigits (&G.timeDigits);
      G.timeDigits.blnk[0] = false;
      G.timeDigits.blnk[1] = false;
      G.timeDigits.blnk[2] = false;
      G.timeDigits.blnk[3] = false;
      G.timeDigits.blnk[4] = true;
      G.timeDigits.blnk[5] = true;
      nextState   = SET_MIN_E;
      returnState = SHOW_TIME_E;
      G.menuState = SET_HOUR;
    case SET_HOUR:
      // button 1 - pressed --> increase hours
      if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_hour++; if (t->tm_hour > 23) t->tm_hour = 0; );
          scrollTs = ts;
        }   
      }
      // button 2 - pressed --> decrease hours
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_hour--; if (t->tm_hour < 0) t->tm_hour = 23; );
          scrollTs = ts;
        }   
      }
      break;
      
    /*################################################################################*/
    case SET_MIN_E:
      Nixie.setDigits (&G.timeDigits);
      G.timeDigits.blnk[0] = false;
      G.timeDigits.blnk[1] = false;
      G.timeDigits.blnk[2] = true;
      G.timeDigits.blnk[3] = true;
      G.timeDigits.blnk[4] = false;
      G.timeDigits.blnk[5] = false;
      nextState   = SET_SEC_E;
      returnState = SHOW_TIME_E;
      G.menuState = SET_MIN;
    case SET_MIN:
      // button 1 - pressed --> increase minutes
      if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_min++; if (t->tm_min > 59) t->tm_min = 0; );
          scrollTs = ts;
        }   
      }
      // button 2 - pressed --> decrease minutes
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_min--; if (t->tm_min < 0) t->tm_min = 59; );
          scrollTs = ts;
        }   
      }
      break;
      
    /*################################################################################*/
    case SET_SEC_E:
      Nixie.setDigits (&G.timeDigits);
      G.timeDigits.blnk[0] = true;
      G.timeDigits.blnk[1] = true;
      G.timeDigits.blnk[2] = false;
      G.timeDigits.blnk[3] = false;
      G.timeDigits.blnk[4] = false;
      G.timeDigits.blnk[5] = false;
      nextState   = SET_DAY_E;
      returnState = SHOW_TIME_E;
      G.menuState = SET_SEC;
    case SET_SEC:
      // button 1 - rising edge --> stop Timer1 then reset seconds to 0
      if (Button[1].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        G.timeDigits.blnk[0] = false;
        G.timeDigits.blnk[1] = false; 
        cli ();
        Timer1.stop ();
        Timer1.restart ();
        sei ();
        t = G.systemTm; 
        t->tm_sec = 0;
        sysTime = mktime (t);
        set_system_time (sysTime);
        updateDigits ();
        G.dcfSyncActive = true;
        G.manuallyAdjusted = true;
      }
      // button 1 - falling edge --> start Timer1
      else if (Button[1].falling ()) {
        G.timeDigits.blnk[0] = true;
        G.timeDigits.blnk[1] = true;
        Nixie.resetBlinking ();
        sysTime = time (NULL);
        set_system_time (sysTime - 1);
        Timer1.start ();
      }
      break;

    /*################################################################################*/
    case SET_DAY_E:
      Nixie.setDigits (&G.dateDigits);
      G.dateDigits.blnk[0] = false;
      G.dateDigits.blnk[1] = false;
      G.dateDigits.blnk[2] = false;
      G.dateDigits.blnk[3] = false;
      G.dateDigits.blnk[4] = true;
      G.dateDigits.blnk[5] = true; 
      G.dateDigits.comma[4] = true;
      G.dateDigits.comma[2] = true;
      nextState   = SET_MONTH_E;
      returnState = SHOW_DATE_E;
      G.menuState = SET_DAY;
    case SET_DAY:
      // button 1 - pressed --> increase days
      if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_mday++; if (t->tm_mday > month_length (t->tm_year, t->tm_mon+1) ) t->tm_mday = 1; );
          scrollTs = ts;
        }   
      }
      // button 2 - pressed --> decrease days
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_mday--; if (t->tm_mday < 1) t->tm_mday = month_length (t->tm_year, t->tm_mon+1); );
          scrollTs = ts;
        }   
      }
      break;

    /*################################################################################*/
    case SET_MONTH_E:
      Nixie.setDigits (&G.dateDigits);
      G.dateDigits.blnk[0] = false;
      G.dateDigits.blnk[1] = false;
      G.dateDigits.blnk[2] = true;
      G.dateDigits.blnk[3] = true;
      G.dateDigits.blnk[4] = false;
      G.dateDigits.blnk[5] = false; 
      nextState   = SET_YEAR_E;
      returnState = SHOW_DATE_E;
      G.menuState = SET_MONTH;
    case SET_MONTH:
      // button 1 - pressed --> increase months
      if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_mon++; if (t->tm_mon > 11) t->tm_mon = 0; );
          scrollTs = ts;
        }   
      }
      // button 2 - pressed --> decrease months
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_mon--; if (t->tm_mon < 0) t->tm_mon = 11; );
          scrollTs = ts;
        }   
      }
      break;

    /*################################################################################*/
    case SET_YEAR_E:
      Nixie.setDigits (&G.dateDigits);
      G.dateDigits.blnk[0] = true;
      G.dateDigits.blnk[1] = true;
      G.dateDigits.blnk[2] = false;
      G.dateDigits.blnk[3] = false;
      G.dateDigits.blnk[4] = false;
      G.dateDigits.blnk[5] = false; 
      nextState   = SET_HOUR_E;
      returnState = SHOW_DATE_E;
      G.menuState = SET_YEAR;
    case SET_YEAR:
      // button 1 - pressed --> increase years
      if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_year++; if (t->tm_year > 199) t->tm_year = 199; )
          scrollTs = ts;
        }   
      }
      // button 2 - pressed --> decrease years
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          SET_TIME_DATE( t->tm_year--; if (t->tm_year < 100) t->tm_year = 100; );
          scrollTs = ts;
        }   
      }
      break;

    /*################################################################################*/
    default:
      G.menuState = SHOW_TIME_E;
      break;
   
  }
   
}
