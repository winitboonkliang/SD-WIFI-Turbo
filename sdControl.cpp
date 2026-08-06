#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile uint32_t SDControl::_spiBlockoutTime = 0;
volatile uint32_t SDControl::_blockoutMs = SPI_BLOCKOUT_PERIOD;
volatile bool SDControl::_weTookBus = false;

// ISR must live in IRAM on current ESP8266 cores - a flash-resident ISR
// crashes the moment it fires during a flash operation.
void IRAM_ATTR SDControl::csSenseISR() {
	if(!_weTookBus)
		_spiBlockoutTime = millis() + _blockoutMs;
}

void SDControl::setBlockout(uint32_t ms) {
	_blockoutMs = ms;
}

void SDControl::setup() {
  // ----- GPIO -------
	// Detect when other master uses SPI bus.
	// INPUT_PULLUP: the old floating input picked up WiFi noise and caused
	// phantom "printer is using the card" blockouts on standalone boards.
	pinMode(CS_SENSE, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(CS_SENSE), csSenseISR, FALLING);

	// brief observation window at boot: an actively-used bus asserts CS
	// within milliseconds. (was a hard delay(20000) - 20 seconds dead
	// on every power-up)
	delay(SD_BOOT_OBSERVE_MS);
}

// ------------------------
void SDControl::takeBusControl()	{
// ------------------------
	_weTookBus = true;
	pinMode(MISO_PIN, SPECIAL);
	pinMode(MOSI_PIN, SPECIAL);
	pinMode(SCLK_PIN, SPECIAL);
	// preload HIGH so switching to OUTPUT never glitches CS low
	digitalWrite(SD_CS, HIGH);
	pinMode(SD_CS, OUTPUT);
}

// ------------------------
void SDControl::relinquishBusControl()	{
// ------------------------
	pinMode(MISO_PIN, INPUT);
	pinMode(MOSI_PIN, INPUT);
	pinMode(SCLK_PIN, INPUT);
	// keep the card deselected through a weak pull-up instead of floating;
	// a real external master can still drive the line low
	pinMode(SD_CS, INPUT_PULLUP);
	_weTookBus = false;
}

// ------------------------
bool SDControl::canWeTakeBus() {
// ------------------------
	// rollover-safe compare (the old "millis() < blockout" broke after 49 days)
	return (int32_t)(millis() - _spiBlockoutTime) >= 0;
}
