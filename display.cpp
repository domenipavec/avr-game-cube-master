/* File: display.cpp
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
 
#include "display.h"
#include "shiftOut.h"
#include "bitop.h"

#include <avr/pgmspace.h>

DisplaySettings::DisplaySettings(bool llsz, bool mlsz, bool lmsz, bool mmsz, bool llsd, bool mlsd, bool lmsd, bool mmsd)
	: llsZero(llsz), mlsZero(mlsz), lmsZero(lmsz), mmsZero(mmsz), init(0) {
	if (llsd) {
		SETBIT(init, LLS_DOT);
	}
	if (mlsd) {
		SETBIT(init, MLS_DOT);
	}
	if (lmsd) {
		SETBIT(init, LMS_DOT);
	}
	if (mmsd) {
		SETBIT(init, MMS_DOT);
	}
}

Display::Display(avr_cpp_lib::OutputPin d, avr_cpp_lib::OutputPin c, avr_cpp_lib::OutputPin l)
	: frozen(false), data(d), clock(c), latch(l), segments(0), bit(33) {
	zero();
}

bool Display::increaseLS(uint8_t lls, uint8_t mls) {
	needRefresh = true;

	digits[0]++;
	if (digits[0] >= lls) {
		digits[0] = 0;
		digits[1]++;
		if (digits[1] >= mls) {
			digits[1] = 0;
			return true;
		}
	}
	return false;
}

bool Display::increaseMS(uint8_t lms, uint8_t mms) {
	needRefresh = true;

	digits[2]++;
	if (digits[2] >= lms) {
		digits[2] = 0;
		digits[3]++;
		if (digits[3] >= mms) {
			digits[3] = 0;
			return true;
		}
	}
	return false;
}

bool Display::increase(uint8_t lls, uint8_t mls, uint8_t lms, uint8_t mms) {
	if (increaseLS(lls, mls)) {
		return increaseMS(lms, mms);
	}
	return false;
}

void Display::zero() {
	needRefresh = true;

	for (uint8_t i = 0; i < 4; i++) {
		digits[i] = 0;
	}
}

void Display::set(uint8_t lls, uint8_t mls, uint8_t lms, uint8_t mms) {
	digits[0] = lls;
	digits[1] = mls;
	digits[2] = lms;
	digits[3] = mms;
	needRefresh = true;
}

void Display::turnOff() {
	avr_cpp_lib::shiftOut(&data, &clock, 0);
	avr_cpp_lib::shiftOut(&data, &clock, 0);
	avr_cpp_lib::shiftOut(&data, &clock, 0);
	avr_cpp_lib::shiftOut(&data, &clock, 0);
	latch.set();
	latch.clear();
}

void Display::settings(DisplaySettings * s) {
	ds = s;
}

uint16_t Display::value() {
	return digits[0] + 10*digits[1] + 100*digits[2] + 1000*digits[3];
}

void Display::update() {
	if (bit < 33) {
		if (bit == 32) {
			latch.set();
			latch.clear();
		} else {
			if (BITSET(segments, bit)) {
				data.set();
			} else {
				data.clear();
			}
			clock.set();
			clock.clear();
		}
		bit++;
	}
}

const uint32_t lls_masks[] PROGMEM = {
	0b01011111000, // 0
	0b00001100000, // 1
	0b10011011000, // 2
	0b10011110000, // 3
	0b11001100000, // 4
	0b11010110000, // 5
	0b11010111000, // 6
	0b00011100000, // 7
	0b11011111000, // 8
	0b11011110000, // 9
};
const uint32_t mls_masks[] PROGMEM = {
	0b0101100000000111, // 0
	0b0000100000000100, // 1
	0b1001100000000011, // 2
	0b1001100000000110, // 3
	0b1100100000000100, // 4
	0b1101000000000110, // 5
	0b1101000000000111, // 6
	0b0001100000000100, // 7
	0b1101100000000111, // 8
	0b1101100000000110, // 9
};
const uint32_t lms_masks[] PROGMEM = {
	0b11100000000010110000000000000000, // 0
	0b10000000000000010000000000000000, // 1
	0b01100000000100110000000000000000, // 2
	0b11000000000100110000000000000000, // 3
	0b10000000000110010000000000000000, // 4
	0b11000000000110100000000000000000, // 5
	0b11100000000110100000000000000000, // 6
	0b10000000000000110000000000000000, // 7
	0b11100000000110110000000000000000, // 8
	0b11000000000110110000000000000000, // 9
};
const uint32_t mms_masks[] PROGMEM = {
	0b11101011000000000000000000000, // 0
	0b10000001000000000000000000000, // 1
	0b01110011000000000000000000000, // 2
	0b11010011000000000000000000000, // 3
	0b10011001000000000000000000000, // 4
	0b11011010000000000000000000000, // 5
	0b11111010000000000000000000000, // 6
	0b10000011000000000000000000000, // 7
	0b11111011000000000000000000000, // 8
	0b11011011000000000000000000000, // 9
};

void Display::refresh() {
	if (needRefresh && !frozen && bit > 32) {
		segments = ds->init;
		if (!(digits[0] == 0 && !ds->llsZero)) {
			segments |= pgm_read_dword(&lls_masks[digits[0]]);
		}
		if (!(digits[1] == 0 && !ds->mlsZero)) {
			segments |= pgm_read_dword(&mls_masks[digits[1]]);
		}
		if (!(digits[2] == 0 && !ds->lmsZero)) {
			segments |= pgm_read_dword(&lms_masks[digits[2]]);
		}
		if (!(digits[3] == 0 && !ds->mmsZero)) {
			segments |= pgm_read_dword(&mms_masks[digits[3]]);
		}
		
		needRefresh = false;
		bit = 0;
	}
}
