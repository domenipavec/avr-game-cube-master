/* File: mode.cpp
 * Everything for mode management
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
 
#include "mode.h"

#include "bitop.h"

#include <avr/eeprom.h>

Mode m;

extern Display display;
extern uint8_t speaker_timeout;
extern uint8_t choose(uint8_t max);

void timerEvent(uint8_t event) {
	static bool active = false;
	static bool zeroed = true;
	static bool minutes = false;
	static uint8_t second_timeout;
	static uint8_t lap_timeout;
	static uint8_t laps;
	if (active) {
		if (event == Mode::EVENT_MASTER_BROKEN || event == Mode::EVENT_SLAVE_BROKEN) {
			if (laps == 0) {
				active = false;
				display.frozen = false;
				display.needRefresh = true;
			} else {
				laps--;
				lap_timeout = 255;
				display.frozen = true;
			}
		}
		if (event == Mode::EVENT_MS10) {
			if (lap_timeout > 0) {
				lap_timeout--;
			} else {
				display.frozen = false;
			}
			
			if (minutes) {
				if (second_timeout == 0) {
					second_timeout = 100;
					display.increase(10,6);
				}
				second_timeout--;
			} else {
				if (display.increase(10,10,10,6)) {
					display.set(0,0,1,0);
					minutes = true;
					second_timeout = 100;
				}
			}
		}
	} else {
		if (zeroed) {
			if (event == Mode::EVENT_MASTER_BROKEN || event == Mode::EVENT_SLAVE_BROKEN) {
				active = true;
				zeroed = false;
				laps = m.laps;
			}
		} else {
			if (event == Mode::EVENT_MASTER_BUTTON || event == Mode::EVENT_SLAVE_BUTTON) {
				zeroed = true;
				display.zero();
				minutes = false;
			}
		}
	}
	
}

void counterEvent(uint8_t event) {
	if (event == Mode::EVENT_MASTER_BROKEN) {
		display.increaseLS();
	} else if (event == Mode::EVENT_SLAVE_BROKEN) {
		display.increaseMS();
	}
}

void counterConfirmEvent(uint8_t event) {
	static bool confirmMaster = false;
	static bool confirmSlave = false;
	static uint16_t masterTimeout;
	static uint16_t slaveTimeout;
	if (confirmMaster) {
		if (masterTimeout > 0) {
			if (event == Mode::EVENT_MS10) {
				masterTimeout--;
			} else if (event == Mode::EVENT_MASTER_BUTTON) {
				confirmMaster = false;
				CLEARBIT(m.ds.init, DisplaySettings::LLS_DOT);
				display.increaseLS();
			}
		} else {
			CLEARBIT(m.ds.init, DisplaySettings::LLS_DOT);
			display.needRefresh = true;
			confirmMaster = false;
		}
	} else {
		if (event == Mode::EVENT_MASTER_BROKEN) {
			confirmMaster = true;
			SETBIT(m.ds.init, DisplaySettings::LLS_DOT);
			display.needRefresh = true;
			masterTimeout = 1000;
		}
	}
	if (confirmSlave) {
		if (slaveTimeout > 0) {
			if (event == Mode::EVENT_MS10) {
				slaveTimeout--;
			} else if (event == Mode::EVENT_SLAVE_BUTTON) {
				confirmSlave = false;
				CLEARBIT(m.ds.init, DisplaySettings::MMS_DOT);
				display.increaseMS();
			}
		} else {
			CLEARBIT(m.ds.init, DisplaySettings::MMS_DOT);
			display.needRefresh = true;
			confirmSlave = false;
		}
	} else {
		if (event == Mode::EVENT_SLAVE_BROKEN) {
			confirmSlave = true;
			SETBIT(m.ds.init, DisplaySettings::MMS_DOT);
			display.needRefresh = true;
			slaveTimeout = 1000;
		}
	}
}

void alarmEvent(uint8_t event) {
	static bool triggered = false;
	static uint8_t timeout = 1;
	if (event == Mode::EVENT_SLAVE_BROKEN || event == Mode::EVENT_MASTER_BROKEN) {
		triggered = true;
	}
	if (event == Mode::EVENT_SLAVE_BUTTON || event == Mode::EVENT_MASTER_BUTTON) {
		triggered = false;
		m.ds.init = BIT(DisplaySettings::LLS_DOT);
		display.set(0,0,0,0);
		timeout = 1;
	}
	if (triggered && event == Mode::EVENT_MS10) {
		timeout--;
		if (timeout == 0) {
			speaker_timeout = 255;
			timeout = 200;
			m.ds.init = BIT(DisplaySettings::LLS_DOT) | BIT(DisplaySettings::MLS_DOT) | BIT(DisplaySettings::LMS_DOT) | BIT(DisplaySettings::MMS_DOT);
			display.set(8,8,8,8);
		} else if (timeout == 100) {
			speaker_timeout = 255;
			m.ds.init = BIT(DisplaySettings::LLS_DOT);
			display.set(0,0,0,0);
		}
	}
}

void measureEvent(uint8_t event) {

}

Mode * getMode(uint8_t i) {
	m.irDelay = 100*(eeprom_read_byte(reinterpret_cast<uint8_t *>(2*i + 1)) + 1); // multiplied by 10ms
	m.irBroken = eeprom_read_byte(reinterpret_cast<uint8_t *>(2*i)) + 1;
	switch (i) {
	case 0: // timer mode 1,2
	case 1:
		m.laps = choose(10);
		m.ds = DisplaySettings(true, true, true, false, false, false, true);
		m.event = timerEvent;
		break;
	case 2: // counter mode 1,2
	case 3:
		m.ds = DisplaySettings(true, false, true, false, false, false, true);
		m.event = counterEvent;
		break;
	case 4: // counter with confirmation mode 1,2
	case 5:
		m.ds = DisplaySettings(true, false, true, false, false, false, true);
		m.event = counterConfirmEvent;
		break;
	case 6: // alarm mode
		m.ds = DisplaySettings(false, false, false, false, true);
		m.event = alarmEvent;
		break;
	case 7:
		m.ds = DisplaySettings(true, true, true);
		m.event = measureEvent;
		m.irBroken = 255;
		m.irDelay = 0;
		break;
	}
	m.packet = (((m.irDelay / 100) - 1)<<5) | (m.irBroken - 1);
	return &m;
}
