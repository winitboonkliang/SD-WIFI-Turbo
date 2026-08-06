#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

#include <Arduino.h>

// How long to keep off the SPI bus after another master (3D printer or the
// GL823K USB card reader) is seen using the card. Was 20000 - which also ran
// as a blocking delay(20000) on EVERY boot. Boot now only observes for
// SD_BOOT_OBSERVE_MS; an active master shows up within milliseconds anyway.
#define SPI_BLOCKOUT_PERIOD	10000UL		// default, overridden by SETUP.INI / web
#define SD_BOOT_OBSERVE_MS	1000UL

class SDControl {
public:
  SDControl() { }
  static void setup();
  static void takeBusControl();
  static void relinquishBusControl();
  static bool canWeTakeBus();
  // how long to stay off the bus after another master (printer / USB reader)
  // touches the card. Short = snappier WiFi uploads between printer reads,
  // long = safer during a print.
  static void setBlockout(uint32_t ms);
  static uint32_t blockout() { return _blockoutMs; }

private:
  static void csSenseISR();

  static volatile uint32_t _spiBlockoutTime;
  static volatile uint32_t _blockoutMs;
  static volatile bool _weTookBus;
};

extern SDControl sdcontrol;

#endif
