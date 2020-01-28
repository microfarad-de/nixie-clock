/*
 * A Nixie Clock Implementation
 * Features:
 * - 6 IN-8-2 Nixie tubes with decimal points
 * - Multiplexed display, requires one single K155ID1 Nixie driver chip
 * - Synchronization with the DCF77 time signal
 * - Automatic crystal drift compensation using DCF77 time
 * - Power saving mode for running on a backup super-capacitor
 * - Dual timers: Timer1 used for timekeeping and Timer2 for countdown timer / stopwatch
 * - Automatic and manual display brightness adjustment
 * - Menu navigation using 3 push-buttons
 * - Alarm clock with the weekday-only option
 * - Countdown timer
 * - Stopwatch
 * - "Service" menu
 * - "Slot machine" effect
 * - Screen blanking
 * - Settings are stored to EEPROM
 * - and more...
 * 
 * Karim Hraibi - 2018
 * 
 */


#include <time.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include "TimerOne.h"
#include "TimerTwo.h"
#include "DCF.h"
#include "Nixie.h"
#include "ADC.h"
#include "Brightness.h"
#include "Helper.h"
#include "Features.h"
#include "BuildDate.h"



//#define SERIAL_DEBUG              // activate debug printing over RS232
#define SERIAL_BAUD 115200          // serial baud rate

// use these macros for printing to serial port
#ifdef SERIAL_DEBUG  
  #define PRINT(...)   Serial.print   (__VA_ARGS__)
  #define PRINTLN(...) Serial.println (__VA_ARGS__)
#else
  #define PRINT(...)
  #define PRINTLN(...)
#endif



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

// analog pins
#define NUM_APINS  5        // total number of hardware analog pins in use for non-blocking ADC 
#define BUTTON0_APIN A1     // button 0 - "mode"
#define BUTTON1_APIN A3     // button 1 - "increase"
#define BUTTON2_APIN A2     // button 2 - "decrease"
#define LIGHTSENS_APIN A0   // light sensor
#define EXTPWR_APIN A6      // measures external power voltage
#define VOLTAGE_APIN A7     // measures the retention super capacitor voltage

// various constants
#define TIMER1_DEFUALT_PERIOD 1000000            // default period for Timer1 (1 second)
#define WDT_TIMEOUT WDTO_4S                      // watcchdog timer timeout setting
#define EEPROM_SETTINGS_ADDR 0                   // EEPROM address of the Timer1 period
#define EEPROM_BRIGHTNESS_ADDR (EEPROM_SETTINGS_ADDR + sizeof (Settings))  // EEPROM address of the display brightness lookup table
#define MENU_ORDER_LIST_SIZE 3                   // size of the dynamic menu ordering list

/*
 * Enumerations for the states of the menu navigation state machine
 */
enum MenuState_e { SHOW_TIME_E, SHOW_DATE_E,    SHOW_ALARM_E, SHOW_TIMER_E, SHOW_STOPWATCH_E, SHOW_SERVICE_E, SHOW_BLANK_E, 
                   SET_ALARM_E, SET_SETTINGS_E, SET_HOUR_E,   SET_MIN_E,    SET_SEC_E,        SET_DAY_E,      SET_MONTH_E,  SET_YEAR_E, 
                   SHOW_TIME,   SHOW_DATE,      SHOW_ALARM,   SHOW_TIMER,   SHOW_STOPWATCH,   SHOW_SERVICE,   SHOW_BLANK,   
                   SET_ALARM,   SET_SETTINGS,   SET_HOUR,     SET_MIN,      SET_SEC,          SET_DAY,        SET_MONTH,    SET_YEAR };




/*
 * Structure that holds the settings to be stored in EEPROM
 */
struct Settings_t {
  volatile uint32_t timer1Period = TIMER1_DEFUALT_PERIOD;  // stores the period of Timer1 in microseconds
  volatile uint32_t nixieUptime;                   // stores the nixie tube uptime in seconds
  uint32_t nixieUptimeResetCode;                   // uptime is reset to zero if this value is different than 0xDEADBEEF
  bool dcfSyncEnabled = true;                      // enables DCF77 synchronization feature
  bool dcfSignalIndicator = true;                  // enables the live DCF77 signal strength indicator (blinking decimal point on digit 0)
  uint8_t dcfSyncHour = 3;                         // hour of day when DCF77 sync shall start
  uint8_t blankScreenMode = 0;                     // turn-off display during a time interval in order to reduce tube wear (2 = on weekdays, 3 = permanently)
  uint8_t blankScreenStartHr = 2;                  // start hour for disabling the display
  uint8_t blankScreenFinishHr = 5;                 // finish hour for disabling the display
  bool cathodePoisonPrevent = true;                // enables cathode poisoning prevention measure by cycling through all digits
  uint8_t cppStartHr = 5;                          // start hour for the cathode poisoning prevention measure
  uint8_t reserved1;                               // reserved for future use
  bool slotMachineEveryMinute = false;             // Nixie digit "Slot Machine" effect is triggered every minute
  uint8_t autoAdjustBrightness = true;             // enables the brightness auto-adjustment feature
  AlarmEeprom_s alarmSettings;                     // alarm clock settings
  MenuState_e menuOrderList[MENU_ORDER_LIST_SIZE] = { SHOW_TIMER_E, SHOW_STOPWATCH_E, SHOW_SERVICE_E }; // dynamically defines the order of the menu items
  uint8_t reserved2;                               // reserved for future use
} Settings;



#define SETTINGS_LUT_SIZE 10  // size of the settings lookup table

/*
 * Lookup table that maps the individual system settings 
 * to their allowed values and IDs  
 */
struct {
  uint8_t *value;     // pointer to the value variable
  uint8_t idDigit1;   // most significant digit to be displayed for the settings ID
  uint8_t idDigit0;   // least significant digit to be dispalyed for the settings ID
  uint8_t minVal;     // minimum value
  uint8_t maxVal;     // maximum value
  uint8_t defaultVal; // default value
} SettingsLut[SETTINGS_LUT_SIZE] = 
{

  { (uint8_t *)&Settings.blankScreenMode,        1, 0,     0,    3,    0  },
  { (uint8_t *)&Settings.blankScreenStartHr,     1, 1,     0,   23,    2  },
  { (uint8_t *)&Settings.blankScreenFinishHr,    1, 2,     0,   23,    6  },
  { (uint8_t *)&Settings.cathodePoisonPrevent,   2, 0, false, true, true  },
  { (uint8_t *)&Settings.cppStartHr,             2, 1,     0,   23,    4  },
  { (uint8_t *)&Settings.dcfSyncEnabled,         3, 0, false, true, true  },
  { (uint8_t *)&Settings.dcfSignalIndicator,     3, 1, false, true, true  },
  { (uint8_t *)&Settings.dcfSyncHour,            3, 2,     0,   23,    3  },
  { (uint8_t *)&Settings.slotMachineEveryMinute, 4, 0, false, true, false },
  { (uint8_t *)&Settings.autoAdjustBrightness,   5, 0, false, true, true  }
};

/*
 * Main class that holds the global variables
 */
class MainClass {
  public:
    uint32_t dcfSyncInterval = 0;                    // DCF77 synchronization interval in minutes
    time_t lastDcfSyncTime = 0;                      // stores the time of last successful DCF77 synchronizaiton
    bool manuallyAdjusted = true;                    // prevent crystal drift compensation if clock was manually adjusted  
    bool dcfSyncActive = true;                       // enable/disable DCF77 synchronization
    bool cppEffectEnabled = false;                   // Nixie digit cathod poison prevention effect is triggered every x seconds (avoids cathode poisoning) 
    volatile uint32_t secTickMsStamp = 0;            // millis() at the last system tick, used for accurate crystal drift compensation
    volatile bool timer1TickFlag = false;            // flag is set every second by the Timer1 ISR 
    volatile bool timer1PeriodUpdateFlag = false;    // flag is set whenever Timer1 period needs to be updated by the ISR
    volatile bool timer2PeriodUpdateFlag = false;    // flag is set whenever Timer2 period needs to be updated by the ISR
    volatile uint8_t timer2SecCounter = 0;           // increments every time Timer2 ISR is called, used for converting 25ms into 1s ticks
    volatile uint8_t timer2TenthCounter = 0;         // increments every time Timer2 ISR is called, used for converting 25ms into 1/10s ticks
  #ifdef SERIAL_DEBUG
    volatile uint8_t printTickCount = 0;             // incremented by the Timer1 ISR every second
  #endif
    tm *systemTm = NULL;                             // pointer to the current system time structure
    NixieDigits_s timeDigits;                        // structure for storing the Nixie display digit values of the current time
    NixieDigits_s dateDigits;                        // structure for storing the Nixie display digit values of the current date 
    MenuState_e menuState = SHOW_TIME_E;             // state of the menu navigation state machine 

    // analog pins as an array
    uint8_t analogPin[NUM_APINS] = 
      { BUTTON0_APIN, BUTTON1_APIN, BUTTON2_APIN, LIGHTSENS_APIN, EXTPWR_APIN };         
    
} Main;

ButtonClass Button[NUM_APINS]; // array of push button objects
AlarmClass Alarm;              // alarm clock object
CdTimerClass CdTimer;          // countdown timer object
StopwatchClass Stopwatch;      // stopwatch object

// countdown timer and stopwatch callback function
void featureCallback (bool start) {
  if (start) {
    Timer2.start ();
  }
  else {
    cli (); Timer2.stop (); Timer2.restart (); sei (); Main.timer2SecCounter = 0; Main.timer2TenthCounter = 0;   
  }
}




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
  PRINTLN ("+ + +  R A D I O  C L O C K  + + +");
  PRINTLN (" ");

  // initialize the ADC
  ADConv.initialize (ADC_PRESCALER_128, ADC_DEFAULT);

  // initialize the Nixie tube display
  Nixie.initialize ( NIXIE_MAX_NUM_TUBES,
          ANODE0_PIN, ANODE1_PIN, ANODE2_PIN, ANODE3_PIN, ANODE4_PIN, ANODE5_PIN,
          BCD0_PIN, BCD1_PIN, BCD2_PIN, BCD3_PIN, COMMA_PIN, &Main.timeDigits);

  // initialize the brightness control algorithm
  Brightness.initialize (EEPROM_BRIGHTNESS_ADDR);
  
  // reset system time
  set_system_time (0);
  sysTime = time (NULL);
  Main.systemTm = localtime (&sysTime);

  // retrieve system settings period from EEOROM
  eepromRead (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
  PRINT   ("[setup] timer1Period = ");
  PRINTLN (Settings.timer1Period, DEC);
  PRINT   ("[setup] nixieUptime = ");
  PRINTLN (Settings.nixieUptime, DEC);
  PRINT   ("[setup] nixieUptimeResetCode = ");
  PRINTLN (Settings.nixieUptimeResetCode, HEX);

  // validate the Timer1 period loaded from EEPROM
  if (Settings.timer1Period < TIMER1_DEFUALT_PERIOD - 10000 ||
      Settings.timer1Period > TIMER1_DEFUALT_PERIOD + 10000) Settings.timer1Period = TIMER1_DEFUALT_PERIOD;

  // reset nixie tube uptime on initial start
  if (Settings.nixieUptimeResetCode != 0xDEADBEEF) {
    Settings.nixieUptime = 0;
    Settings.nixieUptimeResetCode = 0xDEADBEEF;
    PRINTLN ("[setup] nixieUptime was reset to 0");
  }
  PRINTLN ("[setup] other settings:");
  // validate settings loaded from EEPROM
  for (i = 0; i < SETTINGS_LUT_SIZE; i++) {
    PRINT("  ");
    PRINT (SettingsLut[i].idDigit1, DEC); PRINT ("."); PRINT (SettingsLut[i].idDigit0, DEC); PRINT (" = ");
    PRINTLN (*SettingsLut[i].value, DEC);
    if (*SettingsLut[i].value < SettingsLut[i].minVal || *SettingsLut[i].value > SettingsLut[i].maxVal) *SettingsLut[i].value = SettingsLut[i].defaultVal;
  }

  // validate the dynamic menu ordering list
  for (i = 0; i < MENU_ORDER_LIST_SIZE; i++) {
    if (Settings.menuOrderList[i] < SHOW_TIMER_E || Settings.menuOrderList[i] > SHOW_SERVICE_E) {
      Settings.menuOrderList[0] = SHOW_TIMER_E;
      Settings.menuOrderList[1] = SHOW_STOPWATCH_E;
      Settings.menuOrderList[2] = SHOW_SERVICE_E;
      break;
    }
  }

  
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
  Main.timer2SecCounter = 0; 
  Main.timer2TenthCounter = 0;
  

  // initilaize the DCF77 receiver
  DCF.initialize (DCF_PIN, FALLING, INPUT);

  // initialize the Buzzer driver
  // re-uses serial communication pin
#ifndef SERIAL_DEBUG
  Buzzer.initialize (BUZZER_PIN);
#endif

  // initialize the countdown alarm, timer and stopwatch objects
  Alarm.initialize (&Settings.alarmSettings);
  CdTimer.initialize (featureCallback);
  Stopwatch.initialize (featureCallback);
  
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
  time_t sysTime;
  

/* #ifdef SERIAL_DEBUG
  // measure main loop duration
  static uint32_t ts, lastTs;
  static bool tsFlag = false;
  lastTs = ts;
  ts = micros ();
  if (Main.printTickCount == 5) {
    if (!tsFlag) {
      PRINT   ("loop duration (us) = ");
      PRINTLN (ts - lastTs, DEC);
      tsFlag = true;
    }
  }
  else {
    tsFlag = false;
  }
#endif */
  
  // get the current hour
  cli ();
  sysTime = time (NULL);
  sei ();
  lastHour = hour;
  hour = Main.systemTm->tm_hour;
  minute = Main.systemTm->tm_min;
  wday = Main.systemTm->tm_wday;
  
  // start DCF77 reception at the specified hour
  if (hour != lastHour && hour == Settings.dcfSyncHour) Main.dcfSyncActive = true;

  Nixie.refresh ();  // refresh the Nixie tube display
  
  // enable cathode poisoning prevention effect at a preset hour
  if (Settings.cathodePoisonPrevent && hour == Settings.cppStartHr && Main.menuState != SET_HOUR) {
    if (!cppCondition) {
      Main.cppEffectEnabled = true;
      cppCondition = true;
    }  
  }
  else {
    Main.cppEffectEnabled = false;  
    cppCondition = false;
  }
  if (Main.menuState != SHOW_TIME)  Main.cppEffectEnabled = false; // disable when pressing button 0

  // disable Nixie display at a preset hour interval in order to extend the tube lifetime
  if (hour != lastHour && (Settings.blankScreenMode == 1 || Settings.blankScreenMode == 2) ) {
    if (hour == Settings.blankScreenStartHr  && 
        (Settings.blankScreenMode != 2 || (wday >= 1 && wday <= 5) ) && Main.menuState != SET_HOUR) Main.menuState = SHOW_BLANK_E;
    else if (hour == Settings.blankScreenFinishHr && Main.menuState == SHOW_BLANK) Main.menuState = SHOW_TIME_E;
  }

  // permanently disable Nixie Display
  if (Settings.blankScreenMode == 3) {
    if (!blankCondition && Main.menuState == SHOW_TIME) {  
      Main.menuState = SHOW_BLANK_E;
      blankCondition = true;
      //PRINTLN ("[loop] disable display");
    }
    else if (Main.menuState == SHOW_SERVICE || (hour != lastHour && Main.menuState != SET_HOUR)) {
      blankCondition = false;
    }
  }

  Nixie.refresh ();  // refresh the Nixie tube display

  // write-back system settings to EEPROM every night (needed for saving Nixe tube uptime)
  if (hour != lastHour && hour == 1 && Main.menuState != SET_HOUR) {
      Nixie.blank ();
      eepromWrite (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
      Brightness.eepromWrite ();
      PRINTLN ("[loop] write EEPROM");   
  }

  // toggle the decimal point on the Nixie tube
  if (Settings.dcfSyncEnabled) {  
    Nixie.comma[1] = Main.dcfSyncActive && (DCF.lastIrqTrigger || !Settings.dcfSignalIndicator);  // DCF77 sync status indicator
  }
  else {
    Nixie.comma[1] = false; 
  }

  Nixie.refresh ();  // refresh the Nixie tube display
                     // refresh method is called many times across the code to ensure smooth display operation

  // update the Nixie display digits after every second tick
  if (Main.timer1TickFlag) {
    updateDigits ();
    Main.timer1TickFlag = false;
  }

  Nixie.refresh ();  // refresh the Nixie tube display

  adcRead ();        // process the ADC channels
  
  Nixie.refresh ();  // refresh the Nixie tube display
 
  settingsMenu ();   // navigate the settings menu
  
  Nixie.refresh ();  // refresh the Nixie tube display
  
  syncToDCF ();      // synchronize with DCF77 time

  Nixie.refresh ();  // refresh the Nixie tube display
  
  Alarm.loopHandler (hour, minute, wday, Main.menuState != SET_MIN && Main.menuState != SET_SEC);  // alarm clock loop handler
  CdTimer.loopHandler ();            // countdown timer loop handler 

  Nixie.refresh ();  // refresh the Nixie tube display
  
  Stopwatch.loopHandler ();          // stopwatch loop handler
  Buzzer.loopHandler ();             // buzzer loop handlrer


#ifdef SERIAL_DEBUG  
  // print the current time
  if (Main.printTickCount >= 15) {
    PRINT ("[loop] ");
    PRINTLN (asctime (localtime (&sysTime)));
    //PRINT ("[loop] nixieUptime = ");
    //PRINTLN (Settings.nixieUptime, DEC);
    Main.printTickCount = 0;
  }
#endif
}
/*********/




/***********************************
 * Timer ISR
 * Triggered once every second by Timer 1
 ***********************************/
void timer1ISR () {

  Main.secTickMsStamp = millis ();
  system_tick ();

  if (Nixie.enabled) Settings.nixieUptime++;

  // to avoid race conditions, Timer1 period must be updated from within the ISR
  if (Main.timer1PeriodUpdateFlag) {
    Timer1.setPeriod (Settings.timer1Period);
    Main.timer1PeriodUpdateFlag = false;
  }

  Main.timer1TickFlag = true;

#ifdef SERIAL_DEBUG  
  Main.printTickCount++;
#endif  
}
/*********/


/***********************************
 * Timer2 ISR
 * Triggered once every 25ms by Timer 2
 ***********************************/
void timer2ISR () {
    
  Main.timer2SecCounter++;
  Main.timer2TenthCounter++;
  
  // 1s period = 25ms * 40
  if (Main.timer2SecCounter >= 40) {
    CdTimer.tick ();
    Main.timer2SecCounter = 0;
  }

  // 1/10s period = 25ms * 4
  if (Main.timer2TenthCounter >= 4) {
    Stopwatch.tick ();
    Main.timer2TenthCounter = 0;
  }

  // to avoid race conditions, Timer2 period must be updated from within the ISR
  if (Main.timer2PeriodUpdateFlag) {
    // initialize Timer2, set the period to 25ms (1s / 40)
    Timer2.setPeriod (Settings.timer1Period / 40);
    Main.timer2PeriodUpdateFlag = false;
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
 * Synchronize the system clock 
 * with the DCF77 time
 ***********************************/
void syncToDCF () {
  static bool coldStart = true;  // flag to indicate initial sync ater power-up
  static bool enabledCondition = true;
  static int32_t lastDelta = 0;
  uint8_t rv;
  int32_t delta, deltaMs;
  time_t timeSinceLastSync;
  time_t sysTime, dcfTime;

  // enable DCF77 pin interrupt
  if (Settings.dcfSyncEnabled && Main.dcfSyncActive) {
    if (!enabledCondition) {
      DCF.resumeReception ();
      PRINTLN ("[syncToDcf] resume");
      enabledCondition = true;
    }
  }
  else {
    if (enabledCondition) {
      DCF.pauseReception ();
      PRINTLN ("[syncToDcf] pause");
      enabledCondition = false;
    }  
  }
  
  // read DCF77 time
  rv = DCF.getTime ();

  Nixie.refresh ();  // refresh the Nixie tube display

  // DCF77 time has been successfully decoded
  if (Main.dcfSyncActive && rv == 0) {
    cli ();                                      // enter critical section by disabling interruptsg
    deltaMs = millis () - Main.secTickMsStamp;   // milliseconds elapsed since the last full second
    sysTime = time (NULL);                       // get the current system time 
    sei();                                       // re-enable interrupts  
    dcfTime = mktime (&DCF.currentTm);           // get the DCF77 timestamp                     
    
    delta = (int32_t)(sysTime - dcfTime);               // time difference between the sytem time and DCF77 time in seconds     
    deltaMs = delta * 1000  + deltaMs;                  // above time difference in milliseconds
    timeSinceLastSync = dcfTime - Main.lastDcfSyncTime; // time elapsed since the last successful DCF77 synchronization in seconds
    
    // if no big time deviation was detected or 
    // two consecutive DCF77 timestamps produce a similar time deviation
    // then update the system time 
    if (abs (delta) < 60 || abs (delta - lastDelta) < 60) {
      cli ();                                      
      Timer1.restart ();                           // reset the beginning of a second 
      set_system_time (dcfTime - 1);               // apply the new system time (Timer1.restart adds 1s)
      sei ();                                      
      Main.lastDcfSyncTime = dcfTime;              // remember last sync time
      Main.dcfSyncActive = false;                  // pause DCF77 reception

      // calibrate timer1 to compensate for crystal drift
      if (abs (delta) < 60 && timeSinceLastSync > 12*3600 && !Main.manuallyAdjusted && !coldStart) {
        timerCalibrate (timeSinceLastSync, deltaMs);     
      }

      PRINTLN ("[syncToDCF] updated system time");
      PRINT   ("[syncToDCF] dcfSyncInterval = ");
      PRINTLN (Main.dcfSyncInterval, DEC);

      #ifdef SERIAL_DEBUG
        Main.printTickCount = 0;          // reset RS232 print period
      #endif

      Main.manuallyAdjusted = false;      // clear the manual adjustment flag
      coldStart = false;                  // clear the initial startup flag
    }

    lastDelta = delta; // needed for validating consecutive DCF77 measurements against each other
  }

#ifdef SERIAL_DEBUG  
  // debug printing
  // timestamp validation failed
  if (rv < 31) {
    PRINT   ("[syncToDCF] DCF.getTime = ");
    PRINTLN (rv, DEC);
    if (rv == 0) {
      PRINT   ("[syncToDCF] delta = ");
      PRINTLN (delta, DEC);
    }
    PRINT ("[syncToDCF] ");
    PRINT   (asctime (&DCF.currentTm));
    PRINTLN (" *");
  }
  // sync bit has been missed, bits buffer overflow
  else if (rv == 31) {
    PRINT   ("[syncToDCF] many bits = ");
    PRINTLN (DCF.lastIdx, DEC);
    DCF.lastIdx = 0;
  }
  // missed bits or false sync bit detected
  else if (rv == 32) {
    PRINT   ("[syncToDCF] few bits = ");
    PRINTLN (DCF.lastIdx, DEC);
    DCF.lastIdx = 0;
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

  cli ();
  Settings.timer1Period += errorPPM;
  sei ();

  // to avoid race conditions, timer period is updated from within the ISRs
  Main.timer1PeriodUpdateFlag = true;
  Main.timer2PeriodUpdateFlag = true;

  PRINT   ("[timerCalibrate] measDuration = ");
  PRINTLN (measDuration, DEC);
  PRINT   ("[timerCalibrate] timeOffsetMs = ");
  PRINTLN (timeOffsetMs, DEC);
  PRINT   ("[timerCalibrate] errorPPM = ");
  PRINTLN (errorPPM, DEC);
  PRINT   ("[timerCalibrate] timer1Period = ");
  PRINTLN (Settings.timer1Period, DEC);
}
/*********/




/***********************************
 * Update the Nixie display digits
 ***********************************/
void updateDigits () {
  static int8_t lastMin = 0;
  time_t sysTime = time (NULL);
  Main.systemTm = localtime (&sysTime);
  // check whether current state requires time or date display
  Main.timeDigits.value[0] = dec2bcdLow  (Main.systemTm->tm_sec);
  Main.timeDigits.value[1] = dec2bcdHigh (Main.systemTm->tm_sec);
  Main.timeDigits.value[2] = dec2bcdLow  (Main.systemTm->tm_min);
  Main.timeDigits.value[3] = dec2bcdHigh (Main.systemTm->tm_min);
  Main.timeDigits.value[4] = dec2bcdLow  (Main.systemTm->tm_hour);
  Main.timeDigits.value[5] = dec2bcdHigh (Main.systemTm->tm_hour);
  Main.dateDigits.value[0] = dec2bcdLow  (Main.systemTm->tm_year);
  Main.dateDigits.value[1] = dec2bcdHigh (Main.systemTm->tm_year);
  Main.dateDigits.value[2] = dec2bcdLow  (Main.systemTm->tm_mon + 1);
  Main.dateDigits.value[3] = dec2bcdHigh (Main.systemTm->tm_mon + 1);
  Main.dateDigits.value[4] = dec2bcdLow  (Main.systemTm->tm_mday);
  Main.dateDigits.value[5] = dec2bcdHigh (Main.systemTm->tm_mday); 

  // trigger Nixie digit "Slot Machine" effect
  if (Main.menuState == SHOW_TIME) {
    if (Settings.slotMachineEveryMinute && Main.systemTm->tm_min != lastMin) {
      Nixie.slotMachine();
      lastMin = Main.systemTm->tm_min;
    }
    if (Settings.cathodePoisonPrevent && Main.cppEffectEnabled && Main.timeDigits.value[0] == 0) {
      Nixie.cathodePoisonPrevent ();
    }
  }
  else {
    lastMin = Main.systemTm->tm_min;
  }
}
/*********/





/***********************************
 * Read ADC channels
 ***********************************/
void adcRead (void) {
  static uint8_t chanIdx = 0;
  static uint32_t lightsensTs = 0;
  static int32_t avgVal[NUM_APINS];
  uint32_t ts;
  int16_t adcVal;
  uint8_t val;
  
  // start ADC for the current button (will be ignored if already started)
  ADConv.start (Main.analogPin[chanIdx]);

  // check if ADC finished detecting the value
  adcVal = ADConv.readVal ();
 
  // ADC finished
  if (adcVal >= 0) {
    
    ts = millis ();
    
    // process light sensor value
    if (Main.analogPin[chanIdx] == LIGHTSENS_APIN) {
      if (ts - lightsensTs > 200 && abs (adcVal - avgVal[chanIdx]) > 32 && Settings.autoAdjustBrightness) {
        avgVal[chanIdx] = (avgVal[chanIdx] * 31 + (int32_t)adcVal) >> 5;          // IIR low-pass filtering for smooth transitions
        val = Brightness.lightSensorUpdate ((int16_t)avgVal[chanIdx]);
        Nixie.setBrightness (val);
        lightsensTs = ts;
      }
    }
    // check external power voltage
    else if (Main.analogPin[chanIdx] == EXTPWR_APIN) {
      if (adcVal < 512) powerSave();  // enable power save mode if external power is lost
    }
    // process button values
    else {
      avgVal[chanIdx] = (avgVal[chanIdx] * 15 + (int32_t)adcVal) >> 4;             // IIR low-pass filtering for button debouncing
      if (avgVal[chanIdx] > 512) {
        if (Button[chanIdx].pressed == false) Nixie.resetBlinking(); // synchronize digit blinking with the rising edge of a button press
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
        power_adc_disable ();
        cli ();
        WDTCSR |= _BV(WDCE) | _BV(WDE);
        WDTCSR = _BV(WDIE) | _BV(WDP2) | _BV(WDP1); // Enable watchdog interrupt, set to 1s (see Datasheet Section 15.9.2)
        sei ();
        set_sleep_mode (SLEEP_MODE_PWR_DOWN);
        mode = DEEP_SLEEP;
        Main.dcfSyncActive = true;        // time must be synced to DCF77 upon power re-connect
        Main.manuallyAdjusted = true;     // inaccurate time-keeping should never be used for Timer1 calibration
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
  wdt_enable (WDT_TIMEOUT); // enable watchdog timer
  
#ifdef SERIAL_DEBUG
  delay (500);
  PRINT   ("[powerSave] mode = ");
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
  if (Main.menuState != lastState && menuIndex > 0 && menuIndex < MENU_ORDER_LIST_SIZE) {
    
    temp = Settings.menuOrderList[menuIndex-1];
    for (int8_t i = menuIndex-1; i > 0; i--) {
      Settings.menuOrderList[i] = Settings.menuOrderList[i-1];
    }
    Settings.menuOrderList[0] = temp;
    
    lastState = Main.menuState;
  }
}
/*********/

/***********************************
 * Code snippet for setting time/date value
 * called repeatedly from within settingsMenu()
 ***********************************/
#define SET_TIME_DATE( EXPR ) { \
  cli (); \
  t = Main.systemTm; \
  EXPR; \
  sysTime = mktime (t); \
  set_system_time (sysTime); \
  updateDigits (); \
  sei (); \
  Main.dcfSyncActive = true; \
  Main.manuallyAdjusted = true; \
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
  static const uint32_t scrollDelayDefault = 300, menuTimeoutDefault = 30*1000, menuTimeoutExtended = 180*60000, menuTimeoutNextState = 5*1000;
  static uint32_t timeoutTs = 0, scrollTs = 0, brightnessTs = 0, /*settingsTs = 0,*/ accelTs = 0, bannerTs = 0;
  static uint32_t scrollDelay = scrollDelayDefault, menuTimeout = menuTimeoutDefault, scrollCount = 0;
  /*static bool brightnessAdjusted = false, settingsAdjusted = false;*/
  static NixieDigits_s valueDigits;
  static int8_t sIdx = 0, vIdx = 0;
  uint8_t i;
  uint8_t val8;
  int16_t val16;
  time_t sysTime;
  tm *t;
  uint32_t ts = millis ();

  // button 0 - rising edge --> initiate a long press
  if (Button[0].rising ()) {
    timeoutTs = ts; // reset the menu timeout
  }

  Nixie.refresh ();  // refresh the Nixie tube display

  // modes where buttons 1 and 2 are used for digit/setting adjustments 
  // using the progressive scrolling acceleration feature
  if (Main.menuState > SET_YEAR_E && Main.menuState != SHOW_TIME && Main.menuState != SHOW_DATE && Main.menuState != SHOW_ALARM &&  
      Main.menuState != SHOW_STOPWATCH && Main.menuState != SHOW_SERVICE && Main.menuState != SHOW_BLANK && Main.menuState != SET_SEC) {
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
      scrollCount = 0; 
    }
  }

  // in time display mode, buttons 1 and 2 are used switching to the date and alarm modes 
  // or for adjusting display brightness when long-pressed
  if (Main.menuState == SHOW_TIME || Main.menuState == SHOW_DATE || Main.menuState == SHOW_ALARM) {
    // button 1  or 2- rising edge --> initiate a long press
    if (Button[1].rising () || Button[2].rising ()) {
      timeoutTs = ts;
    }
    // button 1 - falling edge --> change state: SHOW_DATE->SHOW_ALARM->SHOW_TIME 
    else if (Button[1].falling ()) {
      if (Alarm.alarm) Alarm.snooze ();
      else if (Main.menuState == SHOW_TIME) Main.menuState = SHOW_DATE_E;
      else if (Main.menuState == SHOW_DATE) Main.menuState = SHOW_ALARM_E;
      else if (Main.menuState == SHOW_ALARM) Main.menuState = SHOW_TIME_E;
    }
    // button 2 - falling edge --> change state: SHOW_ALARM->SHOW_DATE->SHOW_TIME
    else if (Button[2].falling ()) {
      if (Alarm.alarm) Alarm.snooze ();
      else if (Main.menuState == SHOW_TIME) Main.menuState = SHOW_ALARM_E;
      else if (Main.menuState == SHOW_ALARM) Main.menuState = SHOW_DATE_E;
      else if (Main.menuState == SHOW_DATE) Main.menuState = SHOW_TIME_E;
    }
    else if ( Main.menuState == SHOW_TIME) {
      // button 1 - long press --> increase brightness
      if (Button[1].longPressContinuous () && ts - brightnessTs >= 15) {
        val8 = Brightness.increase (1);
        Nixie.setBrightness (val8);
        Nixie.refresh ();
        /*brightnessAdjusted = true;*/
        brightnessTs = ts;
      }   
      // button 2 - long press --> decrease brightness
      else  if (Button[2].longPressContinuous () && ts - brightnessTs >= 15) {
        val8 = Brightness.decrease (1);
        Nixie.setBrightness (val8);
        Nixie.refresh ();
        /*brightnessAdjusted = true;*/
        brightnessTs = ts;
      }    
    }
  }

  Nixie.refresh ();  // refresh the Nixie tube display

  // modes where short-pressing button 0 is used for switching to the next display mode
  // when alarm(s) are active short-pressing button 0 will cancel or snooze the alarm(s) instead
  if (Main.menuState <= SHOW_SERVICE || Main.menuState >= SET_HOUR || CdTimer.alarm || Alarm.alarm) {
    // button 0 - falling edge --> reset alarms or change state: nextState
    if (Button[0].falling ()) {
      if (Alarm.alarm || CdTimer.alarm) {
        Alarm.snooze ();       // snooze the alarm clock
        CdTimer.resetAlarm (); // reset alarm of the countdown timer feature
      }
      else {
        menuTimeout = menuTimeoutDefault;
        Main.menuState = nextState;
      }
    }
  }

  // in selected modes modes, long-pressing button 0 shall return to the pre-defined return display mode
  // when the alarm clock is snoozed or active, long-pressing button 0 will cancel the alarm
  if (Main.menuState == SHOW_TIME || Main.menuState == SHOW_DATE || Main.menuState == SHOW_ALARM ||
      Main.menuState == SHOW_SERVICE || Main.menuState >= SET_ALARM || Alarm.alarm || Alarm.snoozing) {
    // button 0 - long press --> reset alarm or change state: returnState 
    if (Button[0].longPress ()) {
      if (Alarm.alarm || Alarm.snoozing) Alarm.resetAlarm ();  // stop the alarm clock
      else Main.menuState = returnState;
    }
  }


  Nixie.refresh ();  // refresh the Nixie tube display

  /*// write-back brightness values lookup table to EEPROM
  if (brightnessAdjusted && ts - brightnessTs > 30000) {
    Nixie.blank ();             // turn off display to avoid ugly blinking due to slow EEPROM write
    Brightness.eepromWrite ();
    brightnessAdjusted = false;
  }

  // write-back settings values to EEPROM
  if (settingsAdjusted && ts - settingsTs > 10000) {
    Nixie.blank ();
    eepromWrite (EEPROM_SETTINGS_ADDR, (uint8_t *)&Settings, sizeof (Settings));
    settingsAdjusted = false;
    PRINTLN ("[settingsMenu] write EEPROM");
  }*/

  // timeout --> change state: SHOW_TIME
  if (Main.menuState != SHOW_BLANK_E && Main.menuState != SHOW_BLANK && Main.menuState != SHOW_TIME) {
    if (ts - timeoutTs > menuTimeout) {
      menuTimeout = menuTimeoutDefault;
      Main.menuState = SHOW_TIME_E;
    }
  }

  // timeout --> return to time display upon pressing button 0
  if (Main.menuState <= SHOW_SERVICE && Main.menuState != SHOW_TIME && Main.menuState != SHOW_DATE && Main.menuState != SHOW_ALARM) {
    if (ts - timeoutTs > menuTimeoutNextState) {
      nextState = SHOW_TIME_E;
    }
  }

  // if alarm is ringing the switch to the corresponding display mode
  if (Main.menuState != SHOW_BLANK) {
    if (Alarm.alarm && Main.menuState != SHOW_TIME) Main.menuState = SHOW_TIME_E;
    if (CdTimer.alarm && Main.menuState != SHOW_TIMER) Main.menuState = SHOW_TIMER_E;
  }
  
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
  switch (Main.menuState) {

    /*################################################################################*/
    case SHOW_TIME_E:
      Nixie.enable (true);
      Nixie.cancelScroll ();
      Nixie.setDigits (&Main.timeDigits);
      for (i = 0; i < 6; i++) Main.timeDigits.blnk[i] = false;
      menuIndex = 0;
      nextState = Settings.menuOrderList[menuIndex]; // switch to this state after short-pressing button 0
                                                     // use dynamic menu ordering
      returnState = SET_HOUR_E;                      // switch to this state after long-pressing button 0
      Main.menuState = SHOW_TIME;
    case SHOW_TIME:
      break;
      
    /*################################################################################*/
    case SHOW_DATE_E:
      Nixie.setDigits (&Main.dateDigits);
      Main.dateDigits.comma[4] = true;
      Main.dateDigits.comma[2] = true;
      for (i = 0; i < 6; i++) Main.dateDigits.blnk[i] = false;
      //menuIndex = 0;
      nextState = SHOW_TIME_E; //Settings.menuOrderList[menuIndex];
      returnState = SET_DAY_E;
      Main.menuState = SHOW_DATE;
    case SHOW_DATE:
      // button 1 long press --> toggle DCF77 sync status
      if (Button[1].longPress ()) {
        Main.dcfSyncActive = !Main.dcfSyncActive;
      }
      break;

    /*################################################################################*/
    case SHOW_ALARM_E:
      Nixie.setDigits (&Alarm.digits);
      Alarm.displayRefresh ();
      for (i = 0; i < 6; i++) Alarm.digits.blnk[i] = false;
      //menuIndex = 0;
      nextState = SHOW_TIME_E; //Settings.menuOrderList[menuIndex];
      returnState = SET_ALARM_E;
      Main.menuState = SHOW_ALARM;
    case SHOW_ALARM:
      // button 1 long press --> toggle alarm active
      if (Button[1].longPress ()) {
        Alarm.toggleActive ();
        Alarm.resetAlarm ();
        CdTimer.resetAlarm ();
        /*settingsAdjusted = true; // trigger deferred EEPROM write
        settingsTs = ts;*/
      }
      break;

    /*################################################################################*/
    case SHOW_TIMER_E:
      Nixie.enable (true);
      Nixie.setDigits (&CdTimer.digits);
      if (!CdTimer.running && !CdTimer.alarm && !Stopwatch.active) CdTimer.reset ();
      menuIndex++;
      nextState = Settings.menuOrderList[menuIndex];
      menuTimeout = menuTimeoutExtended; // extend the menu timeout
      Main.menuState = SHOW_TIMER;
    case SHOW_TIMER:
      //if      (CdTimer.running || CdTimer.alarm) menuTimeout = menuTimeoutExtended; // extend menu timeout
      //else if (menuTimeout != menuTimeoutDefault) timeoutTs = ts, menuTimeout = menuTimeoutDefault;
      
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
        CdTimer.minuteIncrease ();
        CdTimer.resetAlarm ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIndex);
      }
      // button 2 - falling edge --> decrement minutes / arm countdown timer
      else if (Button[2].falling ()) {
        if (!CdTimer.active) Stopwatch.reset ();
        CdTimer.minuteDecrease ();
        CdTimer.resetAlarm ();
        nextState = SHOW_TIME_E;
        reorderMenu (menuIndex);
      }
      // button 1 - long press --> increment seconds then minutes / arm countdown timer
      else if (Button[1].longPressContinuous ()) {
        if (ts - scrollTs >= scrollDelay) {
          if (!CdTimer.active) Stopwatch.reset ();
          if (scrollCount < 6) {
            CdTimer.secondIncrease ();
            accelTs = ts; scrollDelay = scrollDelayDefault;
          }
          else {
            CdTimer.minuteIncrease ();
          }
          CdTimer.resetAlarm ();
          scrollTs = ts;
          nextState = SHOW_TIME_E;
          reorderMenu (menuIndex);
          scrollCount++;
        }
      }
      // button 2 - long press --> decrement seconds then minutes / arm countdown timer
      else if (Button[2].longPressContinuous ()) {
        if (ts - scrollTs >= scrollDelay) {
          if (!CdTimer.active) Stopwatch.reset ();
          if (scrollCount < 6) {
            CdTimer.secondDecrease ();
            accelTs = ts; scrollDelay = scrollDelayDefault;
          }
          else {
            CdTimer.minuteDecrease ();
          }
          CdTimer.resetAlarm ();
          scrollTs = ts;
          nextState = SHOW_TIME_E;
          reorderMenu (menuIndex);
          scrollCount++;
        }
      }
      break;

    /*################################################################################*/
    case SHOW_STOPWATCH_E:
      Nixie.setDigits (&Stopwatch.digits);
      if (!Stopwatch.active && !CdTimer.active) Stopwatch.reset ();
      menuIndex++;
      nextState = Settings.menuOrderList[menuIndex];
      menuTimeout = menuTimeoutExtended; // extend the menu timeout
      Main.menuState = SHOW_STOPWATCH;
    case SHOW_STOPWATCH:
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
      valueDigits.numDigits = 8;
      cli ();
      Nixie.dec2bcd (Settings.nixieUptime / 3600, &valueDigits, 6);
      sei ();
      valueDigits.value[7] = 1;
      valueDigits.comma[7] = true;
      valueDigits.blank[6] = true;
      Nixie.scroll ();
      bannerTs = ts;
      vIdx = 0;
      nextState = SHOW_TIME_E;
      returnState = SET_SETTINGS_E;
      Main.menuState = SHOW_SERVICE;
    case SHOW_SERVICE:      
      // button 1 or 2 rising edge --> cycle back and forth between system parameters
      if (Button[1].rising () || Button[2].rising ()){
        timeoutTs = ts; // reset the menu timeout
        bannerTs = ts;  // reset display scroll period
        Nixie.cancelScroll ();
        Nixie.resetDigits (&valueDigits);
        if      (Button[1].pressed) {
          vIdx++; if (vIdx > 2) vIdx = 0;
        }
        else if (Button[2].pressed) {
          vIdx--; if (vIdx < 0) vIdx = 2;
        }
        
        // show Nixie tube uptime
        if (vIdx == 0) {
          valueDigits.numDigits = 8;
          cli ();
          Nixie.dec2bcd (Settings.nixieUptime / 3600, &valueDigits, 6);
          sei ();
          valueDigits.value[7] = 1;
          valueDigits.comma[7] = true;
          valueDigits.blank[6] = true;
          Nixie.scroll (); 
        }
        // show firmware version
        else if (vIdx == 1) {
          valueDigits.numDigits = 9;
          Nixie.dec2bcd (Settings.timer1Period, &valueDigits, 7);
          valueDigits.value[8] = 2;
          valueDigits.comma[8] = true;
          valueDigits.blank[7] = true;
          Nixie.scroll (); 
        }
        // show Timer1 period (default)
        else if (vIdx == 2) {
          valueDigits.numDigits = 12;
          valueDigits.value[11] = 3;
          valueDigits.comma[11] = true;
          valueDigits.blank[10] = true; 
          valueDigits.value[9]  = BUILD_HOUR_CH0 - '0';
          valueDigits.value[8]  = BUILD_HOUR_CH1 - '0';
          valueDigits.value[7]  = BUILD_MIN_CH0 - '0';
          valueDigits.value[6]  = BUILD_MIN_CH1 - '0';       
          valueDigits.value[5]  = BUILD_DAY_CH0 - '0';
          valueDigits.value[4]  = BUILD_DAY_CH1 - '0';
          valueDigits.value[3]  = BUILD_MONTH_CH0 - '0';
          valueDigits.value[2]  = BUILD_MONTH_CH1 - '0'; 
          valueDigits.value[1]  = BUILD_YEAR_CH2 - '0';
          valueDigits.value[0]  = BUILD_YEAR_CH3 - '0';
          Nixie.scroll (); 
        }   
        /*settingsAdjusted = true; // trigger deferred EEPROM write
        settingsTs = ts;*/
      }
      // scroll the display every x seconds
      if (ts - bannerTs > 6000) Nixie.scroll (), bannerTs = ts; 
      break;
    
    /*################################################################################*/
    case SHOW_BLANK_E:
      Nixie.enable (false);
      Main.menuState = SHOW_BLANK;
    case SHOW_BLANK:
      // button 0, 1 or 2 - rising edge --> catch rising edge
      if (Button[0].rising () || Button[1].rising () || Button[2].rising ()) {
      }
      // button 0, 1 or 2 - falling edge --> re-activate display
      else if (Button[0].falling () || Button[1].falling () || Button[2].falling ()) {
        Main.menuState = SHOW_TIME_E;
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
      Main.menuState = SET_ALARM;
    case SET_ALARM:
      // button 0 - falling edge --> select next setting value
      if (Button[0].falling ()) {   
        sIdx++; if (sIdx > 3) sIdx = 0;
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
           Alarm.displayRefreshWeekdays ();
        }
        else if (sIdx == 3) {
          Alarm.digits.blnk[3] = false;
          Alarm.digits.blnk[2] = false;
          Alarm.digits.blnk[1] = true;
          Alarm.digits.blnk[0] = true;
          Alarm.displayRefresh ();
        }
      }
      // button 1 - pressed --> increase value
      else if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          if (sIdx == 0)      Alarm.hourIncrease ();
          else if (sIdx == 1) Alarm.minuteIncrease ();
          else if (sIdx == 2) Alarm.toggleWeekdays ();
          else if (sIdx == 3) Alarm.toggleActive ();
          scrollTs = ts;
          /*settingsAdjusted = true; // trigger deferred EEPROM write
          settingsTs = ts;*/
        }   
      }
      // button 2 - pressed --> decrease value
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          if (sIdx == 0)      Alarm.hourDecrease ();
          else if (sIdx == 1) Alarm.minuteDecrease ();
          else if (sIdx == 2) Alarm.toggleWeekdays ();
          else if (sIdx == 3) Alarm.toggleActive ();
          scrollTs = ts;
          /*settingsAdjusted = true; // trigger deferred EEPROM write
          settingsTs = ts;*/
        }   
      }
      break;
    
    /*################################################################################*/
    case SET_SETTINGS_E:
      Nixie.cancelScroll ();
      Nixie.setDigits (&valueDigits);
      valueDigits.blnk[0] = true;
      valueDigits.blnk[1] = true;
      valueDigits.blnk[4] = false;
      valueDigits.blnk[5] = false;
      valueDigits.blank[2] = true;
      valueDigits.blank[3] = true;
      valueDigits.comma[2] = false;
      valueDigits.comma[5] = true;
      sIdx = 0;
      valueDigits.value[5] = SettingsLut[sIdx].idDigit1;
      valueDigits.value[4] = SettingsLut[sIdx].idDigit0;
      valueDigits.value[1] = dec2bcdHigh (*SettingsLut[sIdx].value); 
      valueDigits.value[0] = dec2bcdLow (*SettingsLut[sIdx].value);
      returnState = SHOW_TIME_E;
      Main.menuState = SET_SETTINGS;
    case SET_SETTINGS:
      // button 0 - falling edge --> select next setting value
      if (Button[0].falling ()) {   
        sIdx++;
        if (sIdx >= SETTINGS_LUT_SIZE) sIdx = 0;
        valueDigits.value[5] = SettingsLut[sIdx].idDigit1;
        valueDigits.value[4] = SettingsLut[sIdx].idDigit0;
        valueDigits.value[1] = dec2bcdHigh (*SettingsLut[sIdx].value); 
        valueDigits.value[0] = dec2bcdLow (*SettingsLut[sIdx].value);  
      }
      // button 1 - pressed --> increase value
      else if (Button[1].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          val16 = (int16_t)*SettingsLut[sIdx].value;
          val16++;
          if (val16 > SettingsLut[sIdx].maxVal) val16 = SettingsLut[sIdx].minVal; 
          *SettingsLut[sIdx].value = (uint8_t)val16;
          valueDigits.value[1] = dec2bcdHigh (*SettingsLut[sIdx].value); 
          valueDigits.value[0] = dec2bcdLow (*SettingsLut[sIdx].value);
          Nixie.refresh ();
          scrollTs = ts;
          /*settingsAdjusted = true; // trigger deferred EEPROM write
          settingsTs = ts;*/
        }   
      }
      // button 2 - pressed --> decrease value
      else if (Button[2].pressed) {
        Nixie.resetBlinking();
        if (ts - scrollTs >= scrollDelay) {
          val16 = (int16_t)*SettingsLut[sIdx].value;
          val16--;
          if (val16 < SettingsLut[sIdx].minVal) val16 = SettingsLut[sIdx].maxVal;
          *SettingsLut[sIdx].value = (uint8_t)val16;
          valueDigits.value[1] = dec2bcdHigh (*SettingsLut[sIdx].value); 
          valueDigits.value[0] = dec2bcdLow (*SettingsLut[sIdx].value);
          Nixie.refresh ();
          scrollTs = ts;
          /*settingsAdjusted = true; // trigger deferred EEPROM write
          settingsTs = ts;*/
        }   
      }
      
      break;
    
    /*################################################################################*/
    case SET_HOUR_E:
      Nixie.cancelScroll ();
      Nixie.setDigits (&Main.timeDigits);
      Main.timeDigits.blnk[0] = false;
      Main.timeDigits.blnk[1] = false;
      Main.timeDigits.blnk[2] = false;
      Main.timeDigits.blnk[3] = false;
      Main.timeDigits.blnk[4] = true;
      Main.timeDigits.blnk[5] = true;
      nextState = SET_MIN_E;
      returnState = SHOW_TIME_E;
      Main.menuState = SET_HOUR;
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
      Nixie.setDigits (&Main.timeDigits);
      Main.timeDigits.blnk[0] = false;
      Main.timeDigits.blnk[1] = false;
      Main.timeDigits.blnk[2] = true;
      Main.timeDigits.blnk[3] = true;
      Main.timeDigits.blnk[4] = false;
      Main.timeDigits.blnk[5] = false;
      nextState = SET_SEC_E;
      returnState = SHOW_TIME_E;
      Main.menuState = SET_MIN;
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
      Nixie.setDigits (&Main.timeDigits);
      Main.timeDigits.blnk[0] = true;
      Main.timeDigits.blnk[1] = true;
      Main.timeDigits.blnk[2] = false;
      Main.timeDigits.blnk[3] = false;
      Main.timeDigits.blnk[4] = false;
      Main.timeDigits.blnk[5] = false;
      nextState = SET_DAY_E;
      returnState = SHOW_TIME_E;
      Main.menuState = SET_SEC;
    case SET_SEC:
      // button 1 - rising edge --> stop Timer1 then reset seconds to 0
      if (Button[1].rising ()) {
        timeoutTs = ts; // reset the menu timeout
        Main.timeDigits.blnk[0] = false;
        Main.timeDigits.blnk[1] = false; 
        cli ();
        Timer1.stop ();
        Timer1.restart ();
        sei ();
        t = Main.systemTm; 
        t->tm_sec = 0;
        sysTime = mktime (t);
        set_system_time (sysTime);
        updateDigits ();
        Main.dcfSyncActive = true;
        Main.manuallyAdjusted = true;
      }
      // button 1 - falling edge --> start Timer1
      else if (Button[1].falling ()) {
        Main.timeDigits.blnk[0] = true;
        Main.timeDigits.blnk[1] = true;
        Nixie.resetBlinking ();
        sysTime = time (NULL);
        set_system_time (sysTime - 1);
        Timer1.start ();
      }
      break;

    /*################################################################################*/
    case SET_DAY_E:
      Nixie.setDigits (&Main.dateDigits);
      Main.dateDigits.blnk[0] = false;
      Main.dateDigits.blnk[1] = false;
      Main.dateDigits.blnk[2] = false;
      Main.dateDigits.blnk[3] = false;
      Main.dateDigits.blnk[4] = true;
      Main.dateDigits.blnk[5] = true; 
      Main.dateDigits.comma[4] = true;
      Main.dateDigits.comma[2] = true;
      nextState = SET_MONTH_E;
      returnState = SHOW_DATE_E;
      Main.menuState = SET_DAY;
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
      Nixie.setDigits (&Main.dateDigits);
      Main.dateDigits.blnk[0] = false;
      Main.dateDigits.blnk[1] = false;
      Main.dateDigits.blnk[2] = true;
      Main.dateDigits.blnk[3] = true;
      Main.dateDigits.blnk[4] = false;
      Main.dateDigits.blnk[5] = false; 
      nextState = SET_YEAR_E;
      returnState = SHOW_DATE_E;
      Main.menuState = SET_MONTH;
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
      Nixie.setDigits (&Main.dateDigits);
      Main.dateDigits.blnk[0] = true;
      Main.dateDigits.blnk[1] = true;
      Main.dateDigits.blnk[2] = false;
      Main.dateDigits.blnk[3] = false;
      Main.dateDigits.blnk[4] = false;
      Main.dateDigits.blnk[5] = false; 
      nextState = SET_HOUR_E;
      returnState = SHOW_DATE_E;
      Main.menuState = SET_YEAR;
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
      Main.menuState = SHOW_TIME_E;
      break;
   
  }
   
}











