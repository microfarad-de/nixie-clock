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
 * Copyright (C) 2021 Karim Hraibi (khraibi at gmail.com)
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
 * Version: 4.0.0
 * Date:    January 2021
 */
#define VERSION_MAJOR 4  // Major version
#define VERSION_MINOR 0  // Minor version
#define VERSION_MAINT 0  // Maintenance version



#include <time.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include "src/TimerOne/TimerOne.h"
#include "src/TimerTwo/TimerTwo.h"
#include "src/Button/Button.h"
#include "src/Dcf/Dcf.h"
#include "src/Adc/Adc.h"
#include "src/Nvm/Nvm.h"
#include "src/MathMf/MathMf.h"
#include "Nixie.h"
#include "Brightness.h"
#include "Features.h"
//#include "BuildDate.h"


//#define SERIAL_DEBUG  // activate debug printing over RS232
#define DEBUG_VALUES    // activate the debug values within the service menu


#ifdef SERIAL_DEBUG
  #define SERIAL_BAUD 115200  // serial baud rate
#endif

// use these macros for printing to serial port
#ifdef SERIAL_DEBUG  
  #define PRINT(...)   Serial.print   (__VA_ARGS__)
  #define PRINTLN(...) Serial.println (__VA_ARGS__)
#else
  #define PRINT(...)
  #define PRINTLN(...)
#endif


// if the folllowing value is changed, the Nixie tube uptime will be reset to NIXIE_UPTIME_RESET_VALUE
//#define NIXIE_UPTIME_RESET 

#ifdef NIXIE_UPTIME_RESET
  // reset the Nixie tube uptime to this value in seconds upon booting for the very first time
  #define NIXIE_UPTIME_RESET_VALUE  ((uint32_t)0*3600)
#endif

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

// DCF77 settings
#define DCF_PIN        3        // DCF77 digital pin
#define DCF_START_EDGE FALLING  // trigger the start of a DCF bit on FALLING/RISING edge of DCF_PIN

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
#define TIMER1_DIVIDER         64            // (Timer1 period) = TIMER_DEFAULT_PERIOD / TIMER1_DIVIDER
#define TIMER2_DIVIDER         40            // (Timer2 period) = (Timer1 period) / TIMER2_DIVIDER
#define TIMER_DEFAULT_PERIOD   (1000000 * TIMER1_DIVIDER)  // default value of timerPeriod (total is 1 second)
#define TIMER_MIN_PERIOD       (TIMER_DEFAULT_PERIOD - TIMER_DEFAULT_PERIOD / 100)  // minimum allowed value of timerPeriod
#define TIMER_MAX_PERIOD       (TIMER_DEFAULT_PERIOD + TIMER_DEFAULT_PERIOD / 100)  // maximum allowed value of timerPeriod
#define WDT_TIMEOUT            WDTO_4S       // watcchdog timer timeout setting
#define EEPROM_SETTINGS_ADDR   0             // EEPROM address of the settngs structure
#define EEPROM_BRIGHTNESS_ADDR (EEPROM_SETTINGS_ADDR + sizeof (Settings))  // EEPROM address of the display brightness lookup table
#define MENU_ORDER_LIST_SIZE   3             // size of the dynamic menu ordering list
#define SETTINGS_LUT_SIZE      15            // size of the settings lookup table
#ifdef DEBUG_VALUES
  #define NUM_DEBUG_VALUES     10             // total number of debug values shown in the service menu
  #define NUM_DEBUG_DIGITS     7             // number of digits for the debug values shown in the service menu  
#else 
  #define NUM_DEBUG_VALUES     0
#endif
#define NUM_SERVICE_VALUES     (3 + NUM_DEBUG_VALUES)  // total number of values inside the service menu

/*
 * Enumerations for the states of the menu navigation state machine
 * the program relies on the exact order of the below definitions
 */
enum MenuState_e { SHOW_TIME_E, SHOW_DATE_E,    SHOW_WEEK_E, SHOW_ALARM_E, SHOW_TIMER_E, SHOW_STOPWATCH_E, SHOW_SERVICE_E, SHOW_BLANK_E, 
                   SET_ALARM_E, SET_SETTINGS_E, SET_HOUR_E,  SET_MIN_E,    SET_SEC_E,    SET_DAY_E,        SET_MONTH_E,    SET_YEAR_E,   SET_WEEK_E,
                   SHOW_TIME,   SHOW_DATE,      SHOW_WEEK,   SHOW_ALARM,   SHOW_TIMER,   SHOW_STOPWATCH,   SHOW_SERVICE,   SHOW_BLANK,   
                   SET_ALARM,   SET_SETTINGS,   SET_HOUR,    SET_MIN,      SET_SEC,      SET_DAY,          SET_MONTH,      SET_YEAR,     SET_WEEK  };


#ifdef DEBUG_VALUES
// Class for storing debug values
class DebugClass {
  public:
    int32_t values[NUM_DEBUG_VALUES];
    void initialize (void) {
      for (uint8_t i = 0; i < NUM_DEBUG_VALUES; i++) {
        set ( i, (int32_t)111111111 * i );
        if (i % 2 != 0) {
          values[i] = -values[i];
        }
      }
    }
    void set (uint8_t index, int32_t value) {
      if (index < NUM_DEBUG_VALUES) {
        values[index] = value;
      }
    }
} Debug;
#endif

/*
 * Structure that holds the settings to be stored in EEPROM
 */
struct Settings_t {
  uint32_t timerPeriod;           // virtual period of Timer1/Timer2 (equivalent to 1 second)
  volatile uint32_t nixieUptime;  // stores the nixie tube uptime in seconds
  uint8_t  reserved[4];           // reserved for future use
  bool     dcfSyncEnabled;        // enables DCF77 synchronization feature
  bool     dcfSignalIndicator;    // enables the live DCF77 signal strength indicator (blinking decimal point on digit 1)
  uint8_t  dcfSyncHour;           // hour of day when DCF77 sync shall start
  uint8_t  blankScreenMode;       // turn-off display during a time interval in order to reduce tube wear (1 = every day, 2 = on weekdays, 3 = on weekends, 4 = permanent)
  uint8_t  blankScreenStartHr;    // start hour for disabling the display
  uint8_t  blankScreenFinishHr;   // finish hour for disabling the display
  uint8_t  cathodePoisonPrevent;  // enables cathode poisoning prevention measure by cycling through all digits (1 = on preset time, 2 = "Slot Machine" every minute, 3 = "Slot Machine" every 10 min)
  uint8_t  cppStartHr;            // start hour for the cathode poisoning prevention measure
  int8_t   clockDriftCorrect;     // manual clock drift correction
  uint8_t  reserved0;             // reserved for future use
  bool     brightnessAutoAdjust;  // enables the brightness auto-adjustment feature
  bool     brightnessBoost;       // enables the brightness boosting feature
  uint8_t  blankScreenMode2;      // turn-off display during a time interval in order to reduce tube wear (second profile) (1 = every day, 2 = on weekdays, 3 = on weekends)
  uint8_t  blankScreenStartHr2;   // start hour for disabling the display (second profile)
  uint8_t  blankScreenFinishHr2;  // finish hour for disabling the display (second profile)
  uint8_t  reserved1[1];          // reserved for future use
  uint32_t settingsResetCode;     // all settings will be reset to default if this value is different than the value of SETTINGS_RESET_CODE
  AlarmEeprom_s alarm;            // alarm clock settings
  int8_t   weekStartDay;          // the first day of a calendar week (1 = Monday, 7 = Sunday)
  int8_t   calWeekAdjust;         // calendar week compensation value
  uint8_t  reserved2[2];          // reserved for future use
} Settings;


/*
 * Lookup table that maps the individual system settings 
 * to their ranges and IDs  
 */
const struct SettingsLut_t {
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
  { (int8_t *)&Settings.clockDriftCorrect,      6, 1,   -99,   99,     0 }, // manual clock drift correction
  { (int8_t *)&Settings.weekStartDay,           6, 2,     1,    7,     1 }  // start day of the week (1 = Monday, 7 = Sunday)
};

/*
 * Global variables
 */
struct G_t {
  uint32_t timer1Step;                         // minimum adjustment step for Timer1 in µs
  uint32_t timer1Period;                       // Timer1 period = timerPeriod / TIMER1_DIVIDER
  uint32_t timer1PeriodFH;                     // Timer1 high fractional part
  uint32_t timer1PeriodFL;                     // Timer1 low fractional part
  uint32_t timer1PeriodLow;                    // Timer1 period rounded down to the multiple of timer1Step (µs)
  uint32_t timer1PeriodHigh;                   // Timer1 period rounded up to the multiple of timer1Step (µs)
  uint32_t timer2Step;                         // minimum adjustment step for Timers in µs
  uint32_t timer2PeriodFH;                     // Timer2 high fractional part
  uint32_t timer2PeriodFL;                     // Timer2 low fractional part
  uint32_t timer2PeriodLow;                    // Timer2 period rounded down to the multiple of timer2Step (µs)
  uint32_t timer2PeriodHigh;                   // Timer2 period rounded up to the multiple of timer2Step (µs)
  volatile bool timer1UpdateFlag = true;       // set to true if Timer1 parameters have been updated
  volatile bool timer2UpdateFlag = true;       // set to true if Timer2 parameters have been updated
  uint32_t dcfSyncInterval    = 0;             // DCF77 synchronization interval in minutes
  time_t   lastDcfSyncTime    = 0;             // stores the time of last successful DCF77 synchronizaiton
  bool     manuallyAdjusted   = true;          // prevent crystal drift compensation if clock was manually adjusted  
  bool     dcfSyncActive      = true;          // enable/disable DCF77 synchronization
  bool     cppEffectEnabled   = false;         // Nixie digit cathod poison prevention effect is triggered every x seconds (avoids cathode poisoning) 
  uint32_t secTickMsStamp     = 0;             // millis() at the last second tick, used for accurate crystal drift compensation
  volatile bool    timer1TickFlag     = false; // flag is set every second by the Timer1 ISR 
  volatile uint8_t timer2SecCounter   = 0;     // increments every time Timer2 ISR is called, used for converting 25ms into 1s ticks
  volatile uint8_t timer2TenthCounter = 0;     // increments every time Timer2 ISR is called, used for converting 25ms into 1/10s ticks
  time_t   systemTime                 = 0;     // current system time
  tm       *systemTm                  = NULL;  // pointer to the current system time structure
  NixieDigit_s timeDigits[NIXIE_NUM_TUBES];    // stores the Nixie display digit values of the current time
  NixieDigit_s dateDigits[NIXIE_NUM_TUBES];    // stores the Nixie display digit values of the current date 
  MenuState_e   menuState     = SHOW_TIME_E;   // state of the menu navigation state machine 
#ifdef SERIAL_DEBUG
  volatile uint8_t printTickCount     = 0;     // incremented by the Timer1 ISR every second
#endif

  // analog pins as an array
  const uint8_t analogPin[NUM_APINS] = { BUTTON0_APIN, BUTTON1_APIN, BUTTON2_APIN, LIGHTSENS_APIN, EXTPWR_APIN };       

  // dynamically defines the order of the menu items
  MenuState_e menuOrder[MENU_ORDER_LIST_SIZE] = { SHOW_STOPWATCH_E, SHOW_TIMER_E, SHOW_SERVICE_E }; 
} G;

/*
 * Various objects
 */
ButtonClass Button[NUM_APINS]; // array of push button objects
AlarmClass Alarm;              // alarm clock object
CdTimerClass CdTimer;          // countdown timer object
StopwatchClass Stopwatch;      // stopwatch object





/***********************************
 * Function prototypes
 ***********************************/
void eepromWriteSettings (void);
void timer1ISR (void);
void timer2ISR (void);
void timerCallback (bool);
void syncToDCF (void);
void timerCalibrate (time_t, int32_t);
void timerCalculate (void);
void updateDigits (void);
void adcRead (void);
void powerSave (void);
void reorderMenu (int8_t);
uint8_t calendarWeek (void);
void settingsMenu (void);



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

#ifdef DEBUG_VALUES
  // initialize the debug values
  Debug.initialize ();
#endif
  
  PRINTLN (" ");
  PRINTLN ("+ + +  N I X I E  C L O C K  + + +");
  PRINTLN (" ");

  // initialize the ADC
  Adc.initialize (ADC_PRESCALER_128, ADC_DEFAULT);

  // initialize the Nixie tube display
  Nixie.initialize ( ANODE0_PIN, ANODE1_PIN, ANODE2_PIN, ANODE3_PIN, ANODE4_PIN, ANODE5_PIN,
          BCD0_PIN, BCD1_PIN, BCD2_PIN, BCD3_PIN, COMMA_PIN, G.timeDigits, NIXIE_NUM_TUBES);
  
  // initialize the brightness control algorithm
  Brightness.initialize (EEPROM_BRIGHTNESS_ADDR, BRIGHTNESS_PIN);

  // initilaize the DCF77 receiver
  Dcf.initialize (DCF_PIN, DCF_START_EDGE, INPUT);
  
  // reset system time
  set_system_time (0);
  sysTime = time (NULL);
  G.systemTm = localtime (&sysTime);

  // retrieve system settings period from EEOROM
  eepromRead (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));

  // validate the Timer1 period loaded from EEPROM
  if (Settings.timerPeriod < TIMER_MIN_PERIOD || Settings.timerPeriod > TIMER_MAX_PERIOD) 
          Settings.timerPeriod = TIMER_DEFAULT_PERIOD;
  
#ifdef NIXIE_UPTIME_RESET
  // force a nixie tube uptime reset
  Settings.nixieUptime = NIXIE_UPTIME_RESET_VALUE;
  eepromWriteSettings ();
  PRINTLN ("[setup] Nixie uptime reset");
#endif

  // reset all settings on first-time boot
  if (Settings.settingsResetCode != SETTINGS_RESET_CODE) {
    Brightness.initializeLut ();
    Settings.alarm.hour     = 06;
    Settings.alarm.minute   = 45;
    Settings.alarm.mode     = ALARM_OFF;
    Settings.alarm.lastMode = ALARM_WEEKDAYS;
    Settings.timerPeriod    = TIMER_DEFAULT_PERIOD;
    Settings.calWeekAdjust  = 0;
    Settings.nixieUptime    = 0;
    for (i = 0; i < SETTINGS_LUT_SIZE; i++) {
      *SettingsLut[i].value = SettingsLut[i].defaultVal;
      Settings.settingsResetCode = SETTINGS_RESET_CODE;
    }
    eepromWriteSettings ();
    PRINTLN ("[setup] settings initialized");
  }
  
  PRINTLN ("[setup] other:");
  // validate settings loaded from EEPROM
  for (i = 0; i < SETTINGS_LUT_SIZE; i++) {
    PRINT("  ");
    PRINT (SettingsLut[i].idDigit1, DEC); PRINT ("."); PRINT (SettingsLut[i].idDigit0, DEC); PRINT ("=");
    PRINTLN (*SettingsLut[i].value, DEC);
    if (*SettingsLut[i].value < SettingsLut[i].minVal || *SettingsLut[i].value > SettingsLut[i].maxVal) *SettingsLut[i].value = SettingsLut[i].defaultVal;
  }

  PRINT   ("[setup] timerPeriod=");
  PRINTLN (Settings.timerPeriod, DEC);
  
  // initialize Timer1 to trigger timer1ISR once per second
  G.timer1Step = Timer1.initialize (Settings.timerPeriod / TIMER1_DIVIDER);
  Timer1.attachInterrupt (timer1ISR);

  // Timer2 is used for Chronometer and Countdown Timer features
  // Timer2 has a maximum period of 32768us
  G.timer2Step = Timer2.initialize (Settings.timerPeriod / (TIMER1_DIVIDER * TIMER2_DIVIDER)); 
  Timer2.attachInterrupt (timer2ISR);
  cli ();
  Timer2.stop ();
  Timer2.restart ();
  sei ();
  G.timer2SecCounter = 0; 
  G.timer2TenthCounter = 0;
  
  // intialize the Timer1 and Timer2 parameters
  timerCalculate();
 
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
  CdTimer.initialize (timerCallback);
  Stopwatch.initialize (timerCallback);

  // enable the watchdog
  wdt_enable (WDT_TIMEOUT);
}
/*********/



/***********************************
 * Arduino main loop
 ***********************************/
void loop() {
  static bool cppWasEnabled = false, blankWasEnabled = false;
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
    if (!cppWasEnabled) {
      G.cppEffectEnabled = true;
      cppWasEnabled = true;
    }  
  }
  else {
    G.cppEffectEnabled = false;  
    cppWasEnabled = false;
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
    if (!blankWasEnabled && G.menuState == SHOW_TIME) {
      G.menuState = SHOW_BLANK_E;
      blankWasEnabled = true;
    }
    // re-enable blanking after switching to the service display /*or at the change of an hour*/ 
    else if (G.menuState == SHOW_SERVICE /*|| ( G.menuState != SHOW_BLANK && G.menuState != SET_HOUR && hour != lastHour)*/ ) {
      blankWasEnabled = false;
    }
  }
  // disable blanking if blanking blankScreenMode != 4, CPP is enabled or outside the preset time intervals
  else if (G.menuState == SHOW_BLANK) {
    G.menuState = SHOW_TIME_E;
    blankWasEnabled = false;
  }
  // ensure that blankWasEnabled is reset after blanking period elapses, even if not in blanking mode
  else if (blankWasEnabled) {
    blankWasEnabled = false;
  }

  Nixie.refresh ();

  // write-back system settings to EEPROM every night
  if (hour != lastHour && hour == 1 && G.menuState != SET_HOUR) {
      Nixie.blank ();
      eepromWriteSettings ();
      PRINTLN ("[loop] >EEPROM");   
  }

  Nixie.refresh ();

  // toggle the decimal point for the DCF signal indicator
  static bool dcfSyncWasEnabled = false;;
  if (Settings.dcfSyncEnabled) {  
    // DCF77 sync status indicator
    Nixie.comma[1] = G.dcfSyncActive && (Dcf.edge || !Settings.dcfSignalIndicator);
    dcfSyncWasEnabled = true;
#ifdef DCF_DEBUG_VALUES
    static bool debugValuesWereEnabled = false;
    if (G.menuState == SHOW_TIME) {
      Nixie.comma[2] = G.dcfSyncActive && Dcf.debug[0];
      Nixie.comma[3] = G.dcfSyncActive && Dcf.debug[1];
      Nixie.comma[4] = G.dcfSyncActive && Dcf.debug[2];
      Nixie.comma[5] = G.dcfSyncActive && Dcf.debug[3];
      debugValuesWereEnabled = true;
    }
    else if (debugValuesWereEnabled) {
      Nixie.comma[2] = false;
      Nixie.comma[3] = false;
      Nixie.comma[4] = false;
      Nixie.comma[5] = false;
      debugValuesWereEnabled = false;
    }
#endif
  }
  else if (dcfSyncWasEnabled) {
    Nixie.comma[1] = false;
    dcfSyncWasEnabled = false;
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
 * Write settings back to EEPROM 
 ***********************************/
void eepromWriteSettings (void) {
  eepromWrite (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
  Brightness.eepromWrite ();
}
/*********/



/***********************************
 * Timer1 ISR
 * Triggered once every second by Timer 1
 ***********************************/
void timer1ISR (void) {
  static uint32_t fractCountH = 0;
  static uint32_t fractCountL = 0;
  static bool toggleFlag = false;
  uint8_t add1;

  system_tick ();

  if (Nixie.enabled) Settings.nixieUptime++;
  
  if (fractCountL < G.timer1PeriodFL) add1 = 1;
  else                                add1 = 0;

  // dynamically adjust the timer period to account for fractions of a step
  if (fractCountH < (G.timer1PeriodFH + add1) && (!toggleFlag || G.timer1UpdateFlag) ) {
    Timer1.setPeriod (G.timer1PeriodHigh);
    toggleFlag = true;
    G.timer1UpdateFlag = false;
  }

  // dynamically adjust the timer period to account for fractions of a step
  if (fractCountH >= (G.timer1PeriodFH + add1) && (toggleFlag || G.timer1UpdateFlag) ) {
    Timer1.setPeriod (G.timer1PeriodLow);
    toggleFlag = false;
    G.timer1UpdateFlag = false;
  }

  fractCountH++;
  if (fractCountH >= G.timer1Step ) {
    fractCountH = 0;
    fractCountL++;
    if (fractCountL >= TIMER1_DIVIDER ) {
      fractCountL = 0;
    }
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
void timer2ISR (void) {
  static uint32_t fractCountH = 0;
  static uint32_t fractCountL = 0;
  static bool toggleFlag = false;
  uint8_t add1;
    
  G.timer2SecCounter++;
  G.timer2TenthCounter++;
  
  // 1s period = 25ms * 40
  if (G.timer2SecCounter >= TIMER2_DIVIDER) {
    CdTimer.tick ();
    G.timer2SecCounter = 0;
  }

  // 1/10s period = 25ms * 4
  if (G.timer2TenthCounter >= TIMER2_DIVIDER / 10) {
    Stopwatch.tick ();
    G.timer2TenthCounter = 0;
  }

  if (fractCountL < G.timer2PeriodFL) add1 = 1;
  else                                add1 = 0;

  // dynamically adjust the timer period to account for fractions of a step
  if (fractCountH < (G.timer2PeriodFH + add1) && (!toggleFlag || G.timer2UpdateFlag) ) {
    Timer2.setPeriod (G.timer2PeriodHigh);
    toggleFlag = true;
    G.timer2UpdateFlag = false;
  }

  // dynamically adjust the timer period to account for fractions of a step
  if (fractCountH >= (G.timer2PeriodFH + add1) && (toggleFlag || G.timer2UpdateFlag) ) {
    Timer2.setPeriod (G.timer2PeriodLow);
    toggleFlag = false;
    G.timer2UpdateFlag = false;
  }

  fractCountH++;
  if (fractCountH >= G.timer2Step ) {
    fractCountH = 0;
    fractCountL++;
    if (fractCountL >= TIMER2_DIVIDER ) {
      fractCountL = 0;
    }
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
void timerCallback (bool start) {
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
void syncToDCF (void) {
  static bool coldStart = true;  // flag to indicate initial sync after power-up
  static bool dcfWasEnabled = true;
  static int32_t lastDelta = 0;
  uint8_t rv;
  int32_t delta, deltaMs, ms;
  time_t timeSinceLastSync;
  time_t sysTime, dcfTime;

  // enable DCF77 pin interrupt
  if (Settings.dcfSyncEnabled && G.dcfSyncActive) {
    if (!dcfWasEnabled) {
      Dcf.resumeReception ();
      PRINTLN ("[syncToDcf] resume");
      dcfWasEnabled = true;
    }
  }
  else {
    if (dcfWasEnabled) {
      Dcf.pauseReception ();
      PRINTLN ("[syncToDcf] pause");
      dcfWasEnabled = false;
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

#ifdef DEBUG_VALUES
      Debug.set ( 0, (int32_t)Dcf.currentTm.tm_hour*10000 + (int32_t)Dcf.currentTm.tm_min*100 + (int32_t)Dcf.currentTm.tm_sec);
#endif

      // calibrate timer1 to compensate for crystal drift
      if (abs (delta) < 60 && timeSinceLastSync > 1800 && !G.manuallyAdjusted && !coldStart) {
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
  int32_t drift;

  drift = (timeOffsetMs * 1000 * TIMER1_DIVIDER) / (int32_t)measDuration;
  
  Settings.timerPeriod += drift;
  timerCalculate ();

#ifdef DEBUG_VALUES
  Debug.set ( 1, (int32_t)measDuration );
  Debug.set ( 2, timeOffsetMs);
  Debug.set ( 3, drift);
#endif

  PRINT   ("[timerCalibrate] measDuration=");
  PRINTLN (measDuration, DEC);
  PRINT   ("[timerCalibrate] timeOffsetMs=");
  PRINTLN (timeOffsetMs, DEC);
  PRINT   ("[timerCalibrate] drift=");
  PRINTLN (drift, DEC);
  PRINT   ("[timerCalibrate] timerPeriod=");
  PRINTLN (Settings.timerPeriod, DEC);
}
/*********/



/***********************************
 * Derive the Timer1 and Timer2 parameters
 * out of the Timer1 period
 ***********************************/
void timerCalculate (void) {
  volatile uint32_t f, fh ,fl, low, high;

  if (Settings.timerPeriod < TIMER_MIN_PERIOD) Settings.timerPeriod = TIMER_MIN_PERIOD;
  if (Settings.timerPeriod > TIMER_MAX_PERIOD) Settings.timerPeriod = TIMER_MAX_PERIOD;

  G.timer1Period = Settings.timerPeriod / TIMER1_DIVIDER;
  
  f                  = Settings.timerPeriod % (G.timer1Step * TIMER1_DIVIDER);
  fh                 = f / TIMER1_DIVIDER;
  fl                 = f % TIMER1_DIVIDER;
  low                = (Settings.timerPeriod - f) / TIMER1_DIVIDER;
  high               = low + G.timer1Step;
  
  cli ();
  G.timer1PeriodFH   = fh;
  G.timer1PeriodFL   = fl;
  G.timer1PeriodLow  = low;
  G.timer1PeriodHigh = high;
  G.timer1UpdateFlag = true;
  sei ();
  
  f                  = G.timer1Period % (G.timer2Step * TIMER2_DIVIDER);
  fh                 = f / TIMER2_DIVIDER;
  fl                 = f % TIMER2_DIVIDER;
  low                = (G.timer1Period - f) / TIMER2_DIVIDER;
  high               = low + G.timer2Step;
  
  cli ();
  G.timer2PeriodFH   = fh;
  G.timer2PeriodFL   = fl;
  G.timer2PeriodLow  = low;
  G.timer2PeriodHigh = high;
  G.timer2UpdateFlag = true;
  sei ();
  
#ifdef DEBUG_VALUES
  Debug.set ( 4, G.timer1PeriodLow);
  Debug.set ( 5, G.timer1PeriodFH);
  Debug.set ( 6, G.timer1PeriodFL);
  Debug.set ( 7, G.timer2PeriodLow);
  Debug.set ( 8, G.timer2PeriodFH);
  Debug.set ( 9, G.timer2PeriodFL);
#endif
}
/*********/



/***********************************
 * Update the Nixie display digits
 ***********************************/
void updateDigits () {
  static int8_t lastMin = 0;
  
  // check whether current state requires time or date display
  G.timeDigits[0].value = dec2bcdLow  (G.systemTm->tm_sec);
  G.timeDigits[1].value = dec2bcdHigh (G.systemTm->tm_sec);
  G.timeDigits[2].value = dec2bcdLow  (G.systemTm->tm_min);
  G.timeDigits[3].value = dec2bcdHigh (G.systemTm->tm_min);
  G.timeDigits[4].value = dec2bcdLow  (G.systemTm->tm_hour);
  G.timeDigits[5].value = dec2bcdHigh (G.systemTm->tm_hour);
  G.dateDigits[0].value = dec2bcdLow  (G.systemTm->tm_year);
  G.dateDigits[1].value = dec2bcdHigh (G.systemTm->tm_year);
  G.dateDigits[2].value = dec2bcdLow  (G.systemTm->tm_mon + 1);
  G.dateDigits[3].value = dec2bcdHigh (G.systemTm->tm_mon + 1);
  G.dateDigits[4].value = dec2bcdLow  (G.systemTm->tm_mday);
  G.dateDigits[5].value = dec2bcdHigh (G.systemTm->tm_mday); 

  
  if (G.menuState == SHOW_TIME) {
    // trigger Nixie digit "Slot Machine" effect
    if (G.systemTm->tm_min != lastMin && (Settings.cathodePoisonPrevent == 2 || (Settings.cathodePoisonPrevent == 3 && G.timeDigits[2].value == 0))) {
      Nixie.slotMachine();
    }
    // trigger the cathode poisoning prevention routine
    if (Settings.cathodePoisonPrevent == 1 && G.cppEffectEnabled && G.timeDigits[0].value == 0) {
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
      avgVal[chanIdx] = (avgVal[chanIdx] * 15 + (int32_t)adcVal) >> 4;  // IIR low-pass filtering for button debouncing
      if (avgVal[chanIdx] < 400) {
        if (Button[chanIdx].pressed == false) Nixie.resetBlinking();    // synchronize digit blinking with the rising edge of a button press
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
  eepromWriteSettings ();
  
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
        G.manuallyAdjusted = true;     // inaccurate time-keeping should never be used for timer calibration
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
void reorderMenu (int8_t menuIdx) {
  static MenuState_e lastState = SHOW_TIME;
  MenuState_e temp; 
  
  // Remove the return statement to enable this feature
  return;
  
  // never call the function more than once per menu state
  // ensure that menuIdx is within range
  if (G.menuState != lastState && menuIdx > 0 && menuIdx < MENU_ORDER_LIST_SIZE) {
    
    temp = G.menuOrder[menuIdx-1];
    for (int8_t i = menuIdx-1; i > 0; i--) {
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
 * Code snipped for calculating the week day
 ***********************************/
#define WEEK_DAY() \
  (uint8_t)(G.systemTm->tm_wday == 0 ? 7 : G.systemTm->tm_wday);
/*********/


/***********************************
 * Code snipped for calculating the calendar week
 ***********************************/
#define CALENDAR_WEEK() \
  (int8_t)week_of_year (G.systemTm, (Settings.weekStartDay == 7 ? 0 : Settings.weekStartDay)) + Settings.calWeekAdjust + 1;
/*********/

/***********************************
 * Calculate the current calendar week
 ***********************************/
uint8_t calendarWeek (void) {
  int8_t week = CALENDAR_WEEK();
    while (week < 1) {
    Settings.calWeekAdjust++;
    week = CALENDAR_WEEK();
  }
  while (week > 53) {
    Settings.calWeekAdjust--;
    week = CALENDAR_WEEK();
  }
  return (uint8_t)week;
}
/*********/


#define SCROLL_LUT_SIZE   18    // digit scrolling lookup dable size
#define SCROLL_INTERVAL   1000  // digit scrolling interval
#define VALUE_DIGITS_SIZE 14    // size of the general purpose digit buffer

/***********************************
 * Implements the settings menu 
 * navigation structure
 ***********************************/
void settingsMenu (void) {
  static uint16_t scrollLut[SCROLL_LUT_SIZE] = 
      { 250, 160, 155, 150, 145, 140, 135, 130, 120, 110, 100, 90, 80, 70, 60, 50, 40, 30 };
  static MenuState_e nextState = SHOW_DATE_E;
  static MenuState_e returnState = SHOW_TIME_E; 
  static int8_t menuIdx = 0;
  static uint8_t scrollIdx = 0;
  static const uint32_t menuTimeoutDefault = (uint32_t)60*1000;
  static const uint32_t menuTimeoutExtended = 60*60000;
  static const uint32_t menuTimeoutNextState = 5*1000;
  static uint32_t timeoutTs = 0; 
  static uint32_t scrollTs = 0;
  static uint32_t brightnessTs = 0;
  static uint32_t accelTs = 0; 
  static uint32_t bannerTs = 0;
  static uint32_t scrollDelay = scrollLut[0];
  static uint32_t menuTimeout = menuTimeoutDefault;
  static NixieDigit_s valueDigits[VALUE_DIGITS_SIZE];
  static int8_t sIdx = 0;
  static int8_t vIdx = 0;
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
      accelTs   = ts;
      timeoutTs = ts;                // reset the menu timeout
      scrollTs  = ts - scrollDelay;  // ensure digits are updated immediately after a button press
      scrollIdx = 0;
    }
    // button 1 or 2 - long press --> accelerate scrolling digits
    else if ( (Button[1].pressed || Button[2].pressed) && ts - accelTs >= SCROLL_INTERVAL) {
      accelTs += SCROLL_INTERVAL;
      if (scrollIdx + 1 < SCROLL_LUT_SIZE) {
        scrollIdx++;
        scrollDelay = scrollLut[scrollIdx];
      }
    }
    // button 1 or 2 - falling edge --> reset scroll speed
    else if (Button[1].fallingContinuous () || Button[2].fallingContinuous ()) {
      scrollDelay = scrollLut[0]; 
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
        if (G.menuState >= SET_ALARM) Nixie.blinkOnce ();
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
  if (menuIdx >= MENU_ORDER_LIST_SIZE) menuIdx = 0;

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
      Nixie.setDigits (G.timeDigits, NIXIE_NUM_TUBES);
      for (i = 0; i < 6; i++) G.timeDigits[i].blink = false;
      menuIdx = 0;
      nextState = G.menuOrder[menuIdx]; // switch to this state after short-pressing button 0
                                        // use dynamic menu ordering
      returnState = SET_HOUR_E;         // switch to this state after long-pressing button 0
      G.menuState = SHOW_TIME;
    case SHOW_TIME:
      break;
      
    /*################################################################################*/
    case SHOW_DATE_E:
      Nixie.setDigits (G.dateDigits, NIXIE_NUM_TUBES);
      G.dateDigits[4].comma = true;
      G.dateDigits[2].comma = true;
      for (i = 0; i < 6; i++) G.dateDigits[i].blink = false;
      //menuIdx = 0;
      nextState   = SHOW_TIME_E;
      returnState = SET_DAY_E;
      G.menuState = SHOW_DATE;
    case SHOW_DATE:
      // do nothing
      break;

    /*################################################################################*/
    case SHOW_WEEK_E: 
      Nixie.resetDigits (valueDigits, VALUE_DIGITS_SIZE);
      Nixie.setDigits (valueDigits, NIXIE_NUM_TUBES); 
      valU8 = calendarWeek ();
      valueDigits[0].value = dec2bcdLow (valU8);
      valueDigits[1].value = dec2bcdHigh (valU8);
      valueDigits[2].blank = true;
      valueDigits[3].blank = true;
      valueDigits[4].value = WEEK_DAY();
      valueDigits[5].blank = true;
      nextState   = SHOW_TIME_E;
      returnState = SET_WEEK_E;
      G.menuState = SHOW_WEEK; 
    case SHOW_WEEK:
      // do nothing
      break;

    /*################################################################################*/
    case SHOW_ALARM_E:
      Nixie.setDigits (Alarm.digits, NIXIE_NUM_TUBES);
      Alarm.displayRefresh ();
      for (i = 0; i < NIXIE_NUM_TUBES; i++) Alarm.digits[i].blink = false;
      //menuIdx = 0;
      nextState   = SHOW_TIME_E;
      returnState = SET_ALARM_E;
      G.menuState = SHOW_ALARM;
    case SHOW_ALARM:
      // do nothing
      break;

    /*################################################################################*/
    case SHOW_TIMER_E:
      Nixie.enable (true);
      Nixie.setDigits (CdTimer.digits, NIXIE_NUM_TUBES);
      if (!CdTimer.running && !CdTimer.alarm && !Stopwatch.active) CdTimer.reset ();
      menuIdx++;
      nextState   = G.menuOrder[menuIdx];
      menuTimeout = menuTimeoutExtended; // extend the menu timeout
      G.menuState = SHOW_TIMER;
    case SHOW_TIMER:
      // reset the menu timeout as long as timer is running
      if (CdTimer.running && ts - timeoutTs > menuTimeout - 1000) timeoutTs = ts;
      
      // button 0 - long press --> reset the countdown timer
      if (Button[0].longPress ()) { 
        Nixie.blinkOnce ();
        if (CdTimer.running) {
          CdTimer.stop (); 
          CdTimer.resetAlarm ();
        }
        else if (CdTimer.active) {
          CdTimer.reset ();
        }
        else {
          Stopwatch.reset ();
          CdTimer.start ();
        }
        nextState = SHOW_TIME_E; // if feature was used, return to time display upon pressing button 0
        reorderMenu (menuIdx); // if feature was used, it will appear first once the menu is accessed
      }
      // button 1 or 2 - falling edge --> reset timer alarm
      else if (Button[1].falling () || Button[2].falling ()) {
        CdTimer.resetAlarm ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIdx);
      }
      // button 1 or 2 - pressed --> increase/decrease minutes / arm countdown timer
      else if (Button[1].pressed || Button[2].pressed) {
        if (ts - scrollTs >= scrollDelay) {
          if (!CdTimer.alarm) {
            if (Button[1].pressed) CdTimer.minuteIncrease ();
            else                   CdTimer.minuteDecrease ();
          }
          scrollTs  = ts;
          nextState = SHOW_TIME_E;
          reorderMenu (menuIdx);
        }
      }
      break;

    /*################################################################################*/
    case SHOW_STOPWATCH_E:
      Nixie.setDigits (Stopwatch.digits, NIXIE_NUM_TUBES);
      if (!Stopwatch.active && !CdTimer.active) Stopwatch.reset ();
      menuIdx++;
      nextState   = G.menuOrder[menuIdx];
      menuTimeout = menuTimeoutExtended; // extend the menu timeout
      G.menuState = SHOW_STOPWATCH;
    case SHOW_STOPWATCH:
      // reset the menu timeout as long as stopwatch is running
      if (Stopwatch.running && ts - timeoutTs > menuTimeout - 1000) timeoutTs = ts;
    
      // button 0 - long press --> reset the stopwatch
      if (Button[0].longPress ()) { 
        Stopwatch.reset ();
        Nixie.blinkOnce ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIdx);
      }
      // button 1 rising edge --> start/stop stopwatch
      else if (Button[1].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        if (Stopwatch.running) Stopwatch.stop ();
        else CdTimer.reset(), Stopwatch.start ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIdx);
      }
      // button 2 rising edge --> toggle pause stopwatch
      else if (Button[2].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        if (Stopwatch.active) {
          if (Stopwatch.paused) Stopwatch.pause (false);
          else                  Stopwatch.pause (true); 
          nextState = SHOW_TIME_E;
          reorderMenu (menuIdx);
        }
      }
      break;

    /*################################################################################*/
    case SHOW_SERVICE_E:
      Nixie.resetDigits (valueDigits, VALUE_DIGITS_SIZE);
      bannerTs = ts;
      vIdx = 0;
      nextState   = SHOW_TIME_E;
      returnState = SET_SETTINGS_E;
      G.menuState = SHOW_SERVICE;
      goto SHOW_SERVICE_ENTRY_POINT;
    case SHOW_SERVICE:      
      // button 1 or 2 rising edge --> cycle back and forth between system parameters
      if (Button[1].rising () || Button[2].rising ()) {
          timeoutTs = ts;  // reset the menu timeout
          bannerTs  = ts;  // reset display scroll period
          Nixie.cancelScroll ();
          Nixie.resetDigits (valueDigits, VALUE_DIGITS_SIZE);
        if      (Button[1].pressed) {
          vIdx++; if (vIdx >= NUM_SERVICE_VALUES) vIdx = 0;
        }
        else if (Button[2].pressed) {
          vIdx--; if (vIdx < 0) vIdx = NUM_SERVICE_VALUES - 1;
        }
        SHOW_SERVICE_ENTRY_POINT:
        // show Timer1 period (default)
        if (vIdx == 0) {
          Nixie.setDigits (valueDigits, 11);
          Nixie.dec2bcd (G.timer1Period * 100, valueDigits, VALUE_DIGITS_SIZE, 9);
          valueDigits[10].value = 1;
          valueDigits[10].comma = true;
          valueDigits[9].blank  = true;
          valueDigits[2].comma  = true;
          valueDigits[1].value  = dec2bcdHigh(G.timer1PeriodFL);
          valueDigits[0].value  = dec2bcdLow(G.timer1PeriodFL);
        }
        // show Nixie tube uptime
        else if (vIdx == 1) {
          Nixie.setDigits (valueDigits, 8);
          cli ();
          Nixie.dec2bcd (Settings.nixieUptime / 3600, valueDigits, VALUE_DIGITS_SIZE, 6);
          sei ();
          valueDigits[7].value = 2;
          valueDigits[7].comma = true;
          valueDigits[6].blank = true;
        }
        // show firmware version
        else if (vIdx == 2) {
          Nixie.setDigits (valueDigits, 8);
          valueDigits[7].value = 3;
          valueDigits[7].comma = true;
          valueDigits[6].blank = true; 
          valueDigits[5].value = dec2bcdHigh (VERSION_MAJOR);
          valueDigits[4].value = dec2bcdLow (VERSION_MAJOR);
          valueDigits[4].comma = true;
          valueDigits[3].value = dec2bcdHigh (VERSION_MINOR);
          valueDigits[2].value = dec2bcdLow (VERSION_MINOR);
          valueDigits[2].comma = true;    
          valueDigits[1].value = dec2bcdHigh (VERSION_MAINT);
          valueDigits[0].value = dec2bcdLow (VERSION_MAINT);  
        }
#ifdef DEBUG_VALUES   
        // show the Debug values
        else if (vIdx > 2 && vIdx < NUM_SERVICE_VALUES) {
          uint8_t idx = vIdx - (NUM_SERVICE_VALUES - NUM_DEBUG_VALUES);
          Nixie.setDigits (valueDigits, NUM_DEBUG_DIGITS + 2);
          valueDigits[NUM_DEBUG_DIGITS + 1].value = idx;
          valueDigits[NUM_DEBUG_DIGITS] .comma    = true;
          valueDigits[NUM_DEBUG_DIGITS].blank     = true;
          if (idx < NUM_DEBUG_VALUES)  {   
            if (Debug.values[idx] < 0) {
              Nixie.dec2bcd ((uint32_t)(-Debug.values[idx]), valueDigits, VALUE_DIGITS_SIZE, NUM_DEBUG_DIGITS);
              for (i = 0; i < NUM_DEBUG_DIGITS; i++) valueDigits[i].comma = true;
            }
            else {
              Nixie.dec2bcd ((uint32_t)Debug.values[idx], valueDigits, VALUE_DIGITS_SIZE, NUM_DEBUG_DIGITS);
            }
          }
          else {
            Nixie.dec2bcd (0, valueDigits, VALUE_DIGITS_SIZE, NUM_DEBUG_DIGITS);
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
      Nixie.setDigits (Alarm.digits, NIXIE_NUM_TUBES);
      Alarm.displayRefresh ();
      for (i = 0; i < NIXIE_NUM_TUBES; i++) Alarm.digits[i].blink = false;
      sIdx = 0;
      Alarm.digits[5].blink = true;
      Alarm.digits[4].blink = true;
      returnState = SHOW_ALARM_E;
      G.menuState = SET_ALARM;
    case SET_ALARM:
      // button 0 - falling edge --> select next setting value
      if (Button[0].falling ()) {   
        sIdx++; if (sIdx > 2) sIdx = 0;
        if (sIdx == 0) {
          Alarm.digits[5].blink = true;
          Alarm.digits[4].blink = true;
          Alarm.digits[1].blink = false;
          Alarm.digits[0].blink = false;
        }
        else if (sIdx == 1) {
          Alarm.digits[5].blink = false;
          Alarm.digits[4].blink = false;
          Alarm.digits[3].blink = true;
          Alarm.digits[2].blink = true;
        }
        else if (sIdx == 2) {
          Alarm.digits[3].blink = false;
          Alarm.digits[2].blink = false;
          Alarm.digits[1].blink = true;
          Alarm.digits[0].blink = true;
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
      Nixie.setDigits (valueDigits, NIXIE_NUM_TUBES);
      valueDigits[0].blink = true;
      valueDigits[1].blink = true;
      valueDigits[4].blink = false;
      valueDigits[5].blink = false;
      valueDigits[2].blank = true;
      valueDigits[3].blank = true;
      valueDigits[5].comma = true;
      sIdx = 0;
      valueDigits[5].value = SettingsLut[sIdx].idDigit1;
      valueDigits[4].value = SettingsLut[sIdx].idDigit0;
      val8 = *SettingsLut[sIdx].value;
      valueDigits[2].comma = (val8 < 0);
      valueDigits[1].value = dec2bcdHigh ((uint8_t)abs(val8)); 
      valueDigits[0].value = dec2bcdLow ((uint8_t)abs(val8)); 
      returnState = SHOW_TIME_E;
      G.menuState = SET_SETTINGS;
    case SET_SETTINGS:
      // button 0 - falling edge --> select next setting value
      if (Button[0].falling ()) {   
        sIdx++;
        if (sIdx >= SETTINGS_LUT_SIZE) sIdx = 0;
        
        // reset the clock drift correction value, it is used for display purposes only
        if (SettingsLut[sIdx].value == (int8_t *)&Settings.clockDriftCorrect) {
          Settings.clockDriftCorrect = 0;
        }
        
        valueDigits[5].value = SettingsLut[sIdx].idDigit1;
        valueDigits[4].value = SettingsLut[sIdx].idDigit0;
        val8 = *SettingsLut[sIdx].value;
        valueDigits[2].comma = (val8 < 0);
        valueDigits[1].value = dec2bcdHigh ((uint8_t)abs(val8)); 
        valueDigits[0].value = dec2bcdLow ((uint8_t)abs(val8));
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
          valueDigits[2].comma = (val16 < 0);
          valueDigits[1].value = dec2bcdHigh ((uint8_t)abs(val16)); 
          valueDigits[0].value = dec2bcdLow ((uint8_t)abs(val16));
          Nixie.refresh ();
          scrollTs = ts;
          
          // if clock drift correction value has been set
          if (SettingsLut[sIdx].value == (int8_t *)&Settings.clockDriftCorrect) {
            if (Button[1].pressed) Settings.timerPeriod++;
            else                   Settings.timerPeriod--;
            timerCalculate ();
            G.manuallyAdjusted = true;
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
      Nixie.setDigits (G.timeDigits, NIXIE_NUM_TUBES);
      G.timeDigits[0].blink = false;
      G.timeDigits[1].blink = false;
      G.timeDigits[2].blink = false;
      G.timeDigits[3].blink = false;
      G.timeDigits[4].blink = true;
      G.timeDigits[5].blink = true;
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
      Nixie.setDigits (G.timeDigits, NIXIE_NUM_TUBES);
      G.timeDigits[0].blink = false;
      G.timeDigits[1].blink = false;
      G.timeDigits[2].blink = true;
      G.timeDigits[3].blink = true;
      G.timeDigits[4].blink = false;
      G.timeDigits[5].blink = false;
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
      Nixie.setDigits (G.timeDigits, NIXIE_NUM_TUBES);
      G.timeDigits[0].blink = true;
      G.timeDigits[1].blink = true;
      G.timeDigits[2].blink = false;
      G.timeDigits[3].blink = false;
      G.timeDigits[4].blink = false;
      G.timeDigits[5].blink = false;
      nextState   = SET_DAY_E;
      returnState = SHOW_TIME_E;
      G.menuState = SET_SEC;
    case SET_SEC:
      // button 1 - rising edge --> stop Timer1 then reset seconds to 0
      if (Button[1].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        G.timeDigits[0].blink = false;
        G.timeDigits[1].blink = false; 
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
        G.timeDigits[0].blink = true;
        G.timeDigits[1].blink = true;
        Nixie.resetBlinking ();
        sysTime = time (NULL);
        set_system_time (sysTime - 1);
        Timer1.start ();
      }
      break;

    /*################################################################################*/
    case SET_DAY_E:
      Nixie.setDigits (G.dateDigits, NIXIE_NUM_TUBES);
      G.dateDigits[0].blink = false;
      G.dateDigits[1].blink = false;
      G.dateDigits[2].blink = false;
      G.dateDigits[3].blink = false;
      G.dateDigits[4].blink = true;
      G.dateDigits[5].blink = true; 
      G.dateDigits[2].comma = true;
      G.dateDigits[4].comma = true;
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
      Nixie.setDigits (G.dateDigits, NIXIE_NUM_TUBES);
      G.dateDigits[0].blink = false;
      G.dateDigits[1].blink = false;
      G.dateDigits[2].blink = true;
      G.dateDigits[3].blink = true;
      G.dateDigits[4].blink = false;
      G.dateDigits[5].blink = false; 
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
      Nixie.setDigits (G.dateDigits, NIXIE_NUM_TUBES);
      G.dateDigits[0].blink = true;
      G.dateDigits[1].blink = true;
      G.dateDigits[2].blink = false;
      G.dateDigits[3].blink = false;
      G.dateDigits[4].blink = false;
      G.dateDigits[5].blink = false; 
      nextState   = SET_WEEK_E;
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
    case SET_WEEK_E:
      Nixie.resetDigits (valueDigits, VALUE_DIGITS_SIZE);
      Nixie.setDigits (valueDigits, NIXIE_NUM_TUBES); 
      valU8 = calendarWeek ();
      valueDigits[0].blink = true;
      valueDigits[1].blink = true;
      valueDigits[0].value = dec2bcdLow (valU8);
      valueDigits[1].value = dec2bcdHigh (valU8);
      valueDigits[2].blank = true;
      valueDigits[3].blank = true;
      valueDigits[4].value = WEEK_DAY();
      valueDigits[5].blank = true;
      nextState   = SET_HOUR_E;
      returnState = SHOW_WEEK_E;
      G.menuState = SET_WEEK;
    case SET_WEEK:
      // button 1 - pressed --> increase calendar week
      if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          Settings.calWeekAdjust++;
          valU8 = calendarWeek ();
          valueDigits[0].value = dec2bcdLow (valU8);
          valueDigits[1].value = dec2bcdHigh (valU8);
          scrollTs = ts;
        }   
      }
      // button 2 - pressed --> decrease calendar week
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          Settings.calWeekAdjust--;
          valU8 = calendarWeek ();
          valueDigits[0].value = dec2bcdLow (valU8);
          valueDigits[1].value = dec2bcdHigh (valU8);
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
/*********/



