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

#ifndef __TIMERTWO_H
#define __TIMERTWO_H

#include <avr/io.h>
#include <avr/interrupt.h>


class TimerTwo
{
  public:
  
    // properties
    unsigned int pwmPeriod;
    unsigned char clockSelectBits;

    // methods
    void initialize(long microseconds=25000);
    void start();
    void stop();
    void restart();
    void attachInterrupt(void (*isr)(), long microseconds=-1);
    void detachInterrupt();
    void setPeriod(long microseconds);
    void (*isrCallback)();
};

extern TimerTwo Timer2;

#endif // __TIMERTWO_H
