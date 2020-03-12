/*
 * Nixie tube display driver 
 *
 * Karim Hraibi - 2018
 */

#ifndef __NIXIE_H
#define __NIXIE_H

#include <Arduino.h>

/*
 * Nixie tube display configurations
 */
enum NixieNumTubes_e {
  NIXIE_4TUBES = 4,
  NIXIE_6TUBES = 6
};


#define NIXIE_MAX_NUM_TUBES NIXIE_6TUBES
#define NIXIE_DIGIT_BUF_SIZE 14

/*
 * Array of Nixie tube digit values
 */
struct NixieDigits_s {
  uint8_t value[NIXIE_DIGIT_BUF_SIZE] = { 0 };     /* BCD value */
  bool    blank[NIXIE_DIGIT_BUF_SIZE] = { false }; /* blank digit */
  bool    comma[NIXIE_DIGIT_BUF_SIZE] = { false }; /* decimal point state */
  bool    blnk[NIXIE_DIGIT_BUF_SIZE]  = { false }; /* enable blinking */
  uint8_t numDigits = NIXIE_MAX_NUM_TUBES;         /* number of active digits */
};


/*
 * Main class
 */
class NixieClass {

  public:

    /*
     * Initialize the hardware
     * Parameters:
     *   numTubes     : number of Nixie tubes in the display
     *   anodePin0..5 : anode control pins
     *   bcdPin0..5   : pins connected to the BCD to decimal anode driver decoder chip (74141, K155ID1)
     *   commaPin     : pin connected to the decimal point symbol
     *   digits       : pointer to the nixie digits structure
     *   brightness   : default brightness value (0.255)
     */
    void initialize (
            NixieNumTubes_e numTubes,
            uint8_t anodePin0,
            uint8_t anodePin1,
            uint8_t anodePin2,
            uint8_t anodePin3,
            uint8_t anodePin4,
            uint8_t anodePin5,
            uint8_t bcdPin0,
            uint8_t bcdPin1,
            uint8_t bcdPin2,
            uint8_t bcdPin3,
            uint8_t commaPin,
            NixieDigits_s *digits,
            uint8_t brightness = 255
            );

    /* 
     * Set the pointer to the Nixie digits structure
     * Parameters:
     *   digits     : array of Nixie tube display digits
     */
    void setDigits (NixieDigits_s *digits);
    
    /* 
     * Refresh the display
     * This function must be called from within a very fast loop
     */
    void refresh (void);

    /*
     * Set display brightness
     * Parameters:
     *   brightness : 0..99
     */
    void setBrightness (uint8_t brightness);
    
    /*
     * Force blink all digits disregarding the individual digit selecten
     * Parameters:
     *   enable : enable/disable blinking
     */
    void blinkAll (bool enable);

    /*
     * Resets the phase of digit blinking
     */
    void resetBlinking (void);
    
    /*
     * Trigger the "Slot Machine" effect
     */
    void slotMachine (void);

    /*
     * Exectue cathode poisoniong prevention sequence
     */
    void cathodePoisonPrevent (void);

    /*
     * Scroll once through the digit buffer
     */
    void scroll (void);

    /*
     * Cancel any ongoing scrolling activity
     */
    void cancelScroll (void);

    /*
     * Convert a decimal value to Nixie digits BCD format
     * Parameters:
     *   value     : value to be converted
     *   output    : pointer to the Nixie digits structure structure
     *   numDigits : number of BCD digits to be converted
     */
    void dec2bcd (uint32_t value, NixieDigits_s *output, uint8_t numDigits);

    /*
     * Reset a Nixie Digit buffer
     * Parameters:
     *   output : pointer to the Nixie digits structure structure
     */
    void resetDigits (NixieDigits_s *output);

    /*
     * Temporarily Blank the display by turning-off all the Anodes
     * will re-activate the next time refresh() is called
     */
    void blank (void);

    /*
     * Permanently Enable/disable display output
     * Parameters:
     *   enbale : enable/disable display
     */
    void enable (bool enable);

    /*
     * Tells if display is enabled or not
     */
    volatile bool enabled = true;

    /*
     * Pointer to the Nixie digits structure
     */
    NixieDigits_s *digits;

    /*
     * Activate decimal point of a specific digit 
     * disregarding the scroll effect
     */
    bool comma[NIXIE_MAX_NUM_TUBES] = { false };

  private:
  
    NixieNumTubes_e numTubes;
    
    uint8_t anodePin [NIXIE_MAX_NUM_TUBES];
    uint8_t bcdPin [4];
    uint8_t commaPin;
    uint32_t digitOnDuration;
    uint32_t lastTs = 0;
    uint32_t dimFactor = 0;
    uint8_t digit = 0;
    bool blinkAllEnabled = false;
    uint32_t blinkTs = 0;
    bool blinkFlag = false;
    bool slotMachineEnabled[NIXIE_MAX_NUM_TUBES] = { false };
    uint32_t slotMachineTs[NIXIE_MAX_NUM_TUBES] = { 0 };
    uint8_t slotMachineCnt[NIXIE_MAX_NUM_TUBES] = { 0 };
    uint32_t slotMachineDelay[NIXIE_MAX_NUM_TUBES] = { 0 };
    uint8_t slotMachineCntStart[NIXIE_MAX_NUM_TUBES] = { 11,  9,  7,  5, 13, 15 };
    uint8_t slotMachineCntMax[NIXIE_MAX_NUM_TUBES]   = { 50, 40, 30, 20, 60, 70 };
    bool cppEnabled = false;
    uint32_t cppTs = 0;
    uint8_t cppCnt = 0;
    uint32_t scrollTs = 0;
    int8_t scrollOffset = 0;
    bool fullBrightness = false;
};





/*
 * Nixie display class as singleton
 */
extern NixieClass Nixie;


#endif // __NIXIE_H

