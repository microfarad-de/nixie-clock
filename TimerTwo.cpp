/*
 *  Interrupt utilities for 8 bit Timer2 on ATmega328
 *  Based on "TimerOne" by Jesse Tane for http://labs.ideo.com August 2008
 *  Modified December 2017 by Karim Hraibi to work with the Timer/Counter 2 of ATmega328
 *
 *  This is free software. You can redistribute it and/or modify it under
 *  the terms of Creative Commons Attribution 3.0 United States License. 
 *  To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/us/ 
 *  or send a letter to Creative Commons, 171 Second Street, Suite 300, San Francisco, California, 94105, USA.
 *
 */

#include "TimerTwo.h"

#define RESOLUTION 256    // Timer2 is 8 bit


TimerTwo Timer2;              // preinstatiate

ISR(TIMER2_OVF_vect)          // interrupt service routine that wraps a user defined function supplied by attachInterrupt
{
  Timer2.isrCallback();
}

void TimerTwo::initialize(long microseconds)
{
  TCCR2A = _BV(WGM20);        // clear control register A 
  TCCR2B = _BV(WGM22);        // set mode as phase correct pwm, stop the timer
  setPeriod(microseconds);
}

void TimerTwo::setPeriod(long microseconds)
{
  long cycles = (F_CPU * microseconds) / 2000000;                          // the counter runs backwards after TOP, interrupt is at BOTTOM so divide microseconds by 2
  if(cycles < RESOLUTION)              clockSelectBits = _BV(CS20);              // no prescale, full xtal
  else if((cycles >>= 3) < RESOLUTION) clockSelectBits = _BV(CS21);              // prescale by /8
  else if((cycles >>= 2) < RESOLUTION) clockSelectBits = _BV(CS21) | _BV(CS20);  // prescale by /32
  else if((cycles >>= 1) < RESOLUTION) clockSelectBits = _BV(CS22);              // prescale by /64
  else if((cycles >>= 1) < RESOLUTION) clockSelectBits = _BV(CS22) | _BV(CS20);  // prescale by /128
  else if((cycles >>= 1) < RESOLUTION) clockSelectBits = _BV(CS22) | _BV(CS21);  // prescale by /256
  else if((cycles >>= 2) < RESOLUTION) clockSelectBits = _BV(CS22) | _BV(CS21) | _BV(CS20);  // prescale by /1024
  else        cycles = RESOLUTION - 1, clockSelectBits = _BV(CS22) | _BV(CS21) | _BV(CS20);  // request was out of bounds, set as maximum
  OCR2A = pwmPeriod = cycles;                                                    // OCR2A is TOP in phase correct pwm mode for Timer2
  TCCR2B &= ~(_BV(CS20) | _BV(CS21) | _BV(CS22));                                // reset clock select register
  TCCR2B |= clockSelectBits;                                                     
}

void TimerTwo::attachInterrupt(void (*isr)(), long microseconds)
{
  if(microseconds > 0) setPeriod(microseconds);
  isrCallback = isr;                                       // register the user's callback with the real ISR
  TIMSK2 = _BV(TOIE2);                                     // sets the timer overflow interrupt enable bit
  sei();                                                   // ensures that interrupts are globally enabled
  start();
}

void TimerTwo::detachInterrupt()
{
  TIMSK2 &= ~_BV(TOIE2);                                   // clears the timer overflow interrupt enable bit 
}

void TimerTwo::start()
{
  TCCR2B |= clockSelectBits;
}

void TimerTwo::stop()
{
  TCCR2B &= ~(_BV(CS20) | _BV(CS21) | _BV(CS22));          // clears all clock selects bits
}

void TimerTwo::restart()
{
  TCNT2 = 0;
}
