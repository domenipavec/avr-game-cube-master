/* File: main.cpp
 * Contains base main function and usually all the other stuff that avr does...
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
 
#define F_CPU 8000000UL  // 8 MHz
#include <util/delay.h>

// times are multiplied by 50ms
#define ADC_TIMEOUT 200
#define ADC_STARTUP 10
#define ADC_CALIBRATION 1.005

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
//#include <avr/eeprom.h> 

#include <stdint.h>

#include "bitop.h"
#include "shiftOut.h"
#include "io.h"

#define NUM_MODES 3

// global states and flags
volatile uint8_t adc_flags = 0;
#define VOLTAGE_WARNING 0
#define VOLTAGE_CUTOFF 1

uint8_t button_state = 0;
uint8_t speaker_timeout = 0;

// function prototypes
void shutdown();

// display output pins
avr_cpp_lib::OutputPin displayData(&DDRD, &PORTD, PD2);
avr_cpp_lib::OutputPin displayClock(&DDRD, &PORTD, PD3);
avr_cpp_lib::OutputPin displayLatch(&DDRD, &PORTD, PD4);	

// led output pin
avr_cpp_lib::OutputPin ledOut(&DDRC, &PORTC, PC2);

// timer counts
volatile uint8_t ir_count = 0;
volatile uint8_t ms_count = 0;
volatile uint16_t general_count = 0;
volatile uint16_t ms50_count = 0;

// DISPLAY DATA
volatile uint8_t digits[4] = {0,0,0,0};
volatile uint32_t segments = 0;

// digit zero is right most
const uint32_t digit0masks[] PROGMEM = {
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
const uint32_t digit1masks[] PROGMEM = {
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
const uint32_t digit2masks[] PROGMEM = {
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
const uint32_t digit3masks[] PROGMEM = {
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

volatile uint8_t display_zeros = 0;
#define DISPLAY_ZERO_0 0
#define DISPLAY_ZERO_1 1
#define DISPLAY_ZERO_2 2
#define DISPLAY_ZERO_3 3

volatile uint32_t display_dots = 0;
#define DISPLAY_DOT_0 8
#define DISPLAY_DOT_1 13
#define DISPLAY_DOT_2 18
#define DISPLAY_DOT_3 23

volatile uint8_t display_update = 0;
volatile uint8_t display_bit = 0;


// DISPLAY ROUTINE
// i moved the actual update of segments to general loop,
// to avoid recalculation when not necessary
inline void displayUpdate() {
	display_update = 1;
}

inline void increaseWithMax(uint8_t ol0 = 10, uint8_t ol1 = 10, uint8_t ol2 = 10, uint8_t ol3 = 10) {
	digits[0]++;
	if (digits[0] >= ol0) {
		digits[0] = 0;
		digits[1]++;
		if (digits[1] >= ol1) {
			digits[1] = 0;
			digits[2]++;
			if (digits[2] >= ol2) {
				digits[2] = 0;
				digits[3]++;
				if (digits[3] >= ol3) {
					digits[3] = 0;
				}
			}
		}
	}
}

inline void zeroOut() {
	digits[0] = 0;
	digits[1] = 0;
	digits[2] = 0;
	digits[3] = 0;
}

// loop repeating code
inline void general_loop() {
	static uint16_t adc_timeout = ADC_STARTUP;
	static uint8_t led_state = 0;


	// execute every ms
	if (ms_count >= 76) {
		ms_count = 0;
		
		// speaker
		if (speaker_timeout > 0) {
			speaker_timeout--;
			TOGGLEBIT(PORTD, PD1);
		} else {
			CLEARBIT(PORTD, PD1);
		}
		
			// display_update
		if (display_bit < 32) {
			if (BITSET(segments, display_bit)) {
				displayData.set();
			} else {
				displayData.clear();
			}
			displayClock.set();
			displayClock.clear();
			display_bit++;
		} else if (display_bit == 32) {
			displayLatch.set();
			displayLatch.clear();
			display_bit++;
		}
	}

	// execute every 50ms
	if (ms50_count >= 3810) {
		ms50_count = 0;

		// if anything in adc_flags
		if (adc_flags > 0) {
			// voltage cutoff
			if (BITSET(adc_flags, VOLTAGE_CUTOFF)) {
				shutdown();
			}
			
			// blinking led
			if (BITSET(adc_flags, VOLTAGE_WARNING)) {
				led_state++;
				if (led_state == 16) {
					ledOut.set();
				} else if (led_state == 18) {
					ledOut.clear();
					led_state = 0;
				}
			}
		}
		
		// adc
		if (adc_timeout > 0) {
			adc_timeout--;
		} else {
			adc_timeout = ADC_TIMEOUT;
			SETBIT(ADCSRA, ADSC);
		}

		if (display_update > 0 && display_bit == 33) {
			segments = display_dots;
			if (digits[0] == 0) {
				if (BITSET(display_zeros, DISPLAY_ZERO_0)) {
					segments |= digit0masks[0];
				}
			} else {
				segments |= pgm_read_dword(&digit0masks[digits[0]]);
			}
			if (digits[1] == 0) {
				if (BITSET(display_zeros, DISPLAY_ZERO_1)) {
					segments |= digit1masks[0];
				}
			} else {
				segments |= pgm_read_dword(&digit1masks[digits[1]]);
			}
			if (digits[2] == 0) {
				if (BITSET(display_zeros, DISPLAY_ZERO_2)) {
					segments |= digit2masks[0];
				}
			} else {
				segments |= pgm_read_dword(&digit2masks[digits[2]]);
			}
			if (digits[3] == 0) {
				if (BITSET(display_zeros, DISPLAY_ZERO_3)) {
					segments |= digit3masks[0];
				}
			} else {
				segments |= pgm_read_dword(&digit3masks[digits[3]]);
			}

			display_update = 0;
			display_bit = 0;
		}
		
		// button
		if (BITSET(PINC, PC1)) {
			button_state = 0;
		} else {
			// shutdown if pressed for 1s				
			if (button_state >= 20) {
				shutdown();
			}
						
			// button pressed
			button_state++;
		}
	}
}

// SHUTDOWN ROUTINE
void shutdown() {
	// disable interrupts
	cli();

	// going to shutdown, turn off display
	avr_cpp_lib::shiftOut(&displayData, &displayClock, 0);
	avr_cpp_lib::shiftOut(&displayData, &displayClock, 0);
	avr_cpp_lib::shiftOut(&displayData, &displayClock, 0);
	avr_cpp_lib::shiftOut(&displayData, &displayClock, 0);
	displayLatch.set();
	displayLatch.clear();
	ledOut.clear();
	
	// wait for button release
	while (BITCLEAR(PINC, PC1));
	CLEARBIT(PORTC, PC0);

	// infinite loop
	for (;;);
}

// CHOOSE ROUTINE
uint8_t choose(uint8_t max) {
	display_dots = 0;
	display_zeros = BIT(DISPLAY_ZERO_0);
	zeroOut();

	uint8_t max0 = 0;
	uint8_t max1 = 0;
	uint8_t max2 = 0;
	if (max < 10) {
		max0 = max + 1;
	} else {
		max0 = 10;
		if (max < 100) {
			max1 = (max/10) + 1;
		} else {
			max1 = 10;
			max2 = (max/100)+1;
		}
	}

	displayUpdate();

	general_count = 0;

	for (;;) {
		general_loop();

		// sweep through modes (approx. 1.16x per second)
		if (general_count >= 65535) {
			general_count = 0;
			
			increaseWithMax(max0, max1, max2, 0);
			displayUpdate();
		}
		
		if (button_state == 1) {
			break;
		}
	}

	return digits[0] + 10*digits[1] + 100*digits[2];
}



int main() {
	// INIT
	// soft power button
	SETBIT(DDRC, PC0);
	SETBIT(PORTC, PC0);
	SETBIT(PORTC, PC1);
	while (BITCLEAR(PINC, PC1));
	_delay_ms(5);
	
	// pwm for leds
	//SETBIT(DDRD, PD5);
	//SETBIT(DDRD, PD6);
	TCCR0A = 0b01010010;
	TCCR0B = 0b00000001;
	OCR0A = 105;
	OCR0B = 105;
	TIMSK0 = 0b00000010; // interrupt for ir and ms count

	// speaker
	SETBIT(DDRD, PD1);

	// adc measurement
	ADMUX = 0b11000111;
	ADCSRA = 0b10001111;

	ledOut.set();

	// enable interrupts
	sei();

	// ir stuff
	uint16_t delay_timeout = 0;
	uint8_t ir_state = 0;
	uint8_t ir_flags = 0;
#define IR_CONNECTED 0
#define IR_ACTED 1

#define INIT_IR() ir_flags = BIT(IR_ACTED); \
	ir_state = 0; \
	delay_timeout = 0

	INIT_IR();

	
	// mode stuff
	uint8_t flags = 0;
	uint8_t second_timeout = 0;
#define MODE_ACTIVE 0
#define COUNT_ORDER 1
	uint8_t mode = 0;//choose(NUM_MODES - 1);
	display_zeros = BIT(DISPLAY_ZERO_0);
	display_dots = 0;

	zeroOut();
	displayUpdate();

	// main loop with ir sensing
	for (;;) {
		general_loop();

		// ir state machine
		switch (ir_state) {
			case 0: // not connected, wait
				CLEARBIT(ir_flags, IR_CONNECTED);
				// count number of failiures before the thingy is fixed
				if (BITSET(ir_flags, IR_ACTED)) {
					increaseWithMax();
					displayUpdate();
				}
				ir_state = 15;
				ir_count = 0;
				break;
		case 15:
				if (BITCLEAR(PIND, PD7)) {
					ir_state = 1;
				}
				if (ir_count >= 85) {
					ir_state = 0;
				}
				break;
			case 1: // wait till end of signal 0
				if (BITSET(PIND, PD7)) {
					ir_state = 2;
					ir_count = 0;
				}
				break;
			case 2: // wait for required time and transmit 0
				if (ir_count >= 24) {
					ir_state = 3;
					SETBIT(DDRD, PD5);
					ir_count = 0;
				}
				break;
			case 3: // stop led 0
				if (ir_count >= 20) {
					CLEARBIT(DDRD, PD5);
					ir_state = 4;
				}
				break;
			case 4: // wait till after possible beginning (just)
				if (ir_count >= 64) {
					ir_state = 5;
				}
				break;
			case 5: // check for signal 1 and timeout
				if (BITCLEAR(PINB, PB6)) {
					ir_state = 6;
				}
				if (ir_count >= 130) {
					ir_state = 0;
				}
				break;
			case 6: // wait till end of signal 1
				if (BITSET(PINB, PB6)) {
					ir_state = 7;
					ir_count = 0;
				}
				break;
			case 7: // wait required time, transmit 1
				if (ir_count >= 24) {
					ir_state = 8;
					SETBIT(DDRD, PD6);
					ir_count = 0;
				}
				break;
			case 8:
				if (ir_count >= 20) { // stop led 1
					CLEARBIT(DDRD, PD6);
					ir_state = 9;
					ir_count = 0;
				}
				break;
			case 9: // wait till after possible beginning (just)
				if (ir_count >= 64) {
					ir_state = 10;
				}
				break;
			case 10: // check for signal 2 and timeout
				if (BITCLEAR(PINB, PB7)) {
					ir_state = 11;
				}
				if (ir_count >= 130) {
					ir_state = 0;
				}
				break;
			case 11: // wait till end of signal 2
				if (BITSET(PINB, PB7)) {
					ir_state = 12;
					ir_count = 0;
				}
				break;
			case 12: // wait till after possible beginning (just)
				if (ir_count >= 18) {
					ir_state = 13;
				}
				break;
			case 13: // check for signal 0 and timeout
				if (BITCLEAR(PIND, PD7)) {
					ir_state = 1;
					SETBIT(ir_flags, IR_CONNECTED);
					CLEARBIT(ir_flags, IR_ACTED);
				}
				if (ir_count >= 85) {
					ir_state = 0;
				}
				break;
		}

		if (ir_flags == 0 && delay_timeout == 0) {
			ir_flags = BIT(IR_ACTED);

			speaker_timeout = 150;
			zeroOut();
			displayUpdate();
			
			delay_timeout = 100;
		}

		// execute every 10ms
		if (general_count >= 762) {
			// this just about makes it a bit more accurate for timing
			general_count = 0;



			// delay another action upon detection
			if (delay_timeout > 0) {
				delay_timeout--;
			}

		}
	}
}

ISR(TIMER0_COMPA_vect) {
	ir_count++;
	ms_count++;
	general_count++;
	ms50_count++;
}

ISR(ADC_vect) {
	uint16_t tmp = ADC;
	if (tmp < (uint16_t)(900u*ADC_CALIBRATION)) {
		SETBIT(adc_flags, VOLTAGE_WARNING);
		if (tmp < (uint16_t)(850u*ADC_CALIBRATION)) {
			SETBIT(adc_flags, VOLTAGE_CUTOFF);
		}
	}
}
