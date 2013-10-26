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
#include <avr/eeprom.h> 

#include <stdint.h>

#include "bitop.h"
#include "shiftOut.h"
#include "io.h"

using namespace avr_cpp_lib;

// led output pin
OutputPin ledOut(&DDRC, &PORTC, PC2);

#include "display.h"
Display display(OutputPin(&DDRD, &PORTD, PD2), OutputPin(&DDRD, &PORTD, PD3), OutputPin(&DDRD, &PORTD, PD4));

#include "mode.h"
Mode * mode;

uint8_t spi_transceive(const uint8_t send) {
	SPDR = send;
	while (BITCLEAR(SPSR, SPIF));
	return SPDR;
}

#include "cc1101.h"
#define CC_A (6)
#define CC_B (5)

CC1101 cc(&spi_transceive, OutputPin(&DDRB, &PORTB, PB0), InputPin(&DDRB, &PINB, PB4));

InputPin gdo0(&DDRB, &PINB, PB1);

static inline void ccinit() {
	uint8_t config[3];

	cc.reset();

	// GDO2 to high impedance
	config[0] = 0x2E;
	// GDO1 to high impedance
	config[1] = 0x2E;
	// GDO0(PB1) assert when CRC packet received
	config[2] = 0x07;	
	cc.writeBurst(CC1101::IOCFG2, config, 3);
	
	// PKTLEN to 1
	config[0] = 1;
	// PKTCTRL1 no append status, crc autoflush
	config[1] = BIT(3);
	// PKTCTRL0 white data, normal mode, crc fixed length
	config[2] = BIT(6) | BIT(2);
	cc.writeBurst(CC1101::PKTLEN, config, 3);
	
	// set frequency in FREQ[0-2]
	static const uint8_t F_CRYSTAL = 27;
	static const uint32_t F_CARRIER = 433;
	static const uint32_t FREQ = (F_CARRIER << 16)/F_CRYSTAL;
	config[0] = FREQ>>16;
	config[1] = (FREQ>>8) & 0xff;
	config[2] = FREQ & 0xff;
	cc.writeBurst(CC1101::FREQ2, config, 3);
	
	// set manchester encoding, 2-fsk, 16/16 sync
	cc.write(CC1101::MDMCFG2, BIT(3) | 0b010);
	
	// cca mode always, rx to rx, tx to rx
	cc.write(CC1101::MCSM1, 0b00001111);
	
	// calibrate after setting stuff
	cc.command(CC1101::SCAL);
	_delay_ms(1);
}

#define IR_ERROR 13

volatile bool voltage_warning = false;

uint8_t button_state = 0;
uint8_t speaker_timeout = 0;

// function prototypes
static void shutdown();

// timer counts
static volatile uint8_t ir_count = 0;
static volatile uint8_t ms_count = 0;
static volatile uint16_t general_count = 0;
static volatile uint16_t ms50_count = 0;

// loop repeating code
static inline void general_loop() {
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
		
		display.update();
	}

	// execute every 50ms
	if (ms50_count >= 3810) {
		ms50_count = 0;

		// blinking led
		if (voltage_warning) {
			led_state++;
			if (led_state == 16) {
				ledOut.set();
			} else if (led_state == 18) {
				ledOut.clear();
				led_state = 0;
			}
		}
		
		// adc
		if (adc_timeout > 0) {
			adc_timeout--;
		} else {
			adc_timeout = ADC_TIMEOUT;
			SETBIT(ADCSRA, ADSC);
		}

		display.refresh();

		// button
		if (BITSET(PINC, PC1)) {
			button_state = 0;
		} else {
			// shutdown if pressed for 1s
			if (button_state >= 21) {
				shutdown();
			}
						
			// button pressed
			button_state++;
		}
	}
}

// SHUTDOWN ROUTINE
static void shutdown() {
	// disable interrupts
	cli();

	// going to shutdown, turn off display
	display.turnOff();
	ledOut.clear();
	
	// wait for button release
	while (BITCLEAR(PINC, PC1));
	CLEARBIT(PORTC, PC0);

	// infinite loop
	for (;;);
}

DisplaySettings chooseDS(true);
// CHOOSE ROUTINE
uint8_t choose(uint8_t max) {
	max--;
	display.settings(&chooseDS);
	display.zero();

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

	general_count = 0;

	for (;;) {
		general_loop();

		// sweep through modes (approx. 1.16x per second)
		if (general_count >= 65535) {
			general_count = 0;
			
			display.increase(max0, max1, max2, 0);
		}
		
		if (button_state == 1) {
			button_state++;
			break;
		}
	}

	return display.value();
}

static inline void nastavitve() {
	for (;;) {
		chooseDS.init = BIT(DisplaySettings::LLS_DOT);
		display.needRefresh = true;
		uint8_t mode = choose(NUM_MODES-1);
		chooseDS.init = BIT(DisplaySettings::MLS_DOT);
		display.needRefresh = true;
		uint8_t bd = choose(2);
		chooseDS.init = BIT(DisplaySettings::LMS_DOT);
		display.needRefresh = true;
		uint8_t value;
		// bd = 0 broken time
		// bd = 1 delay time
		if (bd == 0) {
			value = choose(32);
		} else {
			value = choose(8);
		}

		eeprom_write_byte(reinterpret_cast<uint8_t *>(mode*2+bd), value);
	}	
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

	// init spi (ss pin must be output for some reason in master mode)
	SETBIT(DDRB, PB2);
	SETBIT(DDRB, PB3);
	SETBIT(DDRB, PB5);
	SPCR = 0b01010000;
	SPSR = BIT(SPI2X);

	// cc1101 config
	ccinit();
	cc.command(CC1101::SRX);
	uint8_t last_i = 5;

	// mode stuff
	uint8_t m = choose(NUM_MODES + 1);
	if (m == NUM_MODES) {
		nastavitve();
	}
	mode = getMode(m);
	display.settings(&(mode->ds));
	display.zero();

	// ir stuff
	uint16_t ir_delay = 0;
	uint8_t ir_broken = 100;
	uint8_t ir_state = 0;
	ir_count = 0;

	// main loop with ir sensing
	for (;;) {
		general_loop();

		// ir state machine
		switch (ir_state) {
			case 0: // check for signal 0 and timeout
				if (BITCLEAR(PIND, PD7)) {
					ir_state = 1;
				}
				if (ir_count >= 85) {
					ir_state = IR_ERROR;
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
					ir_state = IR_ERROR;
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
					if (m == 7 && ir_broken != 0) {
						ir_delay = 100;
						speaker_timeout = 150;
					}
					ir_broken = 0;
				}
				if (ir_count >= 130) {
					ir_state = IR_ERROR;
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
					ir_state = 0;
				}
				break;
			case IR_ERROR:
				if (ir_broken < 100) {
					if (m == 7 && ir_delay == 0) {
						display.set(ir_broken % 10, (ir_broken / 10) % 10, ir_broken/100, 0);
					}
					ir_broken++;
				}
				ir_count = 0;
				ir_state = 0;
				break;
		}

		// receive packet
		if (gdo0.isSet()) {
			uint8_t v = cc.read(CC1101::FIFO);
			uint8_t i = v & 0b011;
			if (i != last_i) {
				last_i = i;
				if (BITSET(v, CC_A)) {
					speaker_timeout = 150;
					mode->event(Mode::EVENT_SLAVE_BUTTON);
				} else if (BITSET(v, CC_B)) {
					speaker_timeout = 150;
					mode->event(Mode::EVENT_SLAVE_BROKEN);
				}
			}
			cc.write(CC1101::FIFO, mode->packet);
			cc.command(CC1101::STX);
		}
		
		// ir broken
		if (ir_broken == mode->irBroken && ir_delay == 0) {
			ir_broken++;

			speaker_timeout = 150;

			ir_delay = mode->irDelay;

			mode->event(Mode::EVENT_MASTER_BROKEN);
		}

		// button pressed
		if (button_state == 1) {
			button_state++;

			mode->event(Mode::EVENT_MASTER_BUTTON);
			speaker_timeout = 150;
		}

		// execute every 10ms
		if (general_count >= 762) {
			general_count = 0;

			// delay another action upon detection
			if (ir_delay > 0) {
				ir_delay--;
			}

			mode->event(Mode::EVENT_MS10);
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
		voltage_warning = true;
		if (tmp < (uint16_t)(850u*ADC_CALIBRATION)) {
			shutdown();
		}
	}
}
