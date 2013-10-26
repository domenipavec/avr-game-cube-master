/* File: display.h
 * Everything for display interaction
 */
/* Copyright (c) 2012-2013 Domen Ipavec (domen.ipavec@z-v.si)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "io.h"

class DisplaySettings {
 public:
	DisplaySettings(bool llsz = false, bool mlsz = false, bool lmsz = false, bool mmsz = false, bool llsd = false, bool mlsd = false, bool lmsd = false, bool mmsd = false);
	bool llsZero;
	bool mlsZero;
	bool lmsZero;
	bool mmsZero;
	uint32_t init;

	static const uint8_t LLS_DOT = 8;
	static const uint8_t MLS_DOT = 13;
	static const uint8_t LMS_DOT = 18;
	static const uint8_t MMS_DOT = 23;
};

class Display {
 public:
	Display(avr_cpp_lib::OutputPin d, avr_cpp_lib::OutputPin c, avr_cpp_lib::OutputPin l);

	// api
	bool increaseLS(uint8_t lls = 10, uint8_t mls = 10);
	bool increaseMS(uint8_t lms = 10, uint8_t mms = 10);
	bool increase(uint8_t lls = 10, uint8_t mls = 10, uint8_t lms = 10, uint8_t mms = 10);
	void zero();
	void set(uint8_t lls, uint8_t mls, uint8_t lms, uint8_t mms);
	void turnOff();
	void settings(DisplaySettings * s);
	uint16_t value();

	// actions
	// bit update, every ms
	void update();
	// screen update, every 50 ms
	void refresh();
 
	bool frozen;
	bool needRefresh;

 private:
	// data
	avr_cpp_lib::OutputPin data;
	avr_cpp_lib::OutputPin clock;
	avr_cpp_lib::OutputPin latch;

	uint8_t digits[4];
	uint32_t segments;

	uint8_t bit;

	DisplaySettings * ds;
};

#endif
