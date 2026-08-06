// FYSETC SD-WIFI - WebDAV server on ESP8266/ESP8285 + SD card
// "turbo" build: fast, non-blocking, keep-alive, OTA capable

#include "serial.h"
#include "parser.h"
#include "config.h"
#include "network.h"
#include "gcode.h"
#include "sdControl.h"
#include "ESPWebDAV.h"   // FW_VERSION + g_minFreeHeap live here

// Over-the-air updates (espota / "pio run -e sdwifi_ota -t upload")
// Set to 0 if you want the smallest possible image.
#define ENABLE_OTA 1
#define OTA_PASSWORD "fysetc"

#if ENABLE_OTA
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
static bool otaStarted = false;
#endif

// LED is connected to GPIO2 on this board
#define LED_PIN 2
#define INIT_LED			{pinMode(LED_PIN, OUTPUT);}
#define LED_ON				{digitalWrite(LED_PIN, LOW);}
#define LED_OFF				{digitalWrite(LED_PIN, HIGH);}

// ------------------------
void setup() {
	SERIAL_INIT(115200);
	INIT_LED;
	blink();

	SERIAL_ECHOLN("");
	SERIAL_ECHO("FYSETC SD-WIFI turbo, FW "); SERIAL_ECHO(FW_VERSION);
	SERIAL_ECHO(" build "); SERIAL_ECHOLN(FW_BUILD);

	sdcontrol.setup();

	// ----- WIFI -------
  unsigned char haveConfig = config.load();

  // switch the console to the configured baud (boot messages above stay 115200)
  if(config.baud() != 115200) {
    SERIAL_FLUSH();
    Serial.end();
    SERIAL_INIT(config.baud());
  }

  if(haveConfig == 1) { // Have a config (INI file or EEPROM)
    if(!network.start()) {
      SERIAL_ECHOLN("Connect fail, please check your INI file or set the wifi config and connect again");
      printHelp();
    }
  }
  else {
    SERIAL_ECHOLN("Welcome to FYSETC: www.fysetc.com");
    SERIAL_ECHOLN("Please set the wifi config first");
    printHelp();
  }
}

// ------------------------
void loop() {
  // handle the request
	network.handle();

  // Handle gcode
  gcode.Handle();

  // blink (non-blocking - the old version delay()ed inside loop)
  statusBlink();

  // last-resort heap watchdog: never allow a slow leak to turn into a
  // wedged board - restart cleanly if heap stays critical for 10 s
  heapGuard();

  // deferred restart (web rename / web firmware update)
  if(g_restartAt && (int32_t)(millis() - g_restartAt) >= 0) {
    SERIAL_ECHOLN("Restarting (web request)");
    SERIAL_FLUSH();
    delay(100);
    ESP.restart();
  }

#if ENABLE_OTA
  otaHandle();
#endif
}

// ------------------------
void heapGuard() {
  static uint32_t lastChk = 0;
  static uint32_t lowSince = 0;
  uint32_t now = millis();
  if(now - lastChk < 1000) return;
  lastChk = now;

  uint32_t h = ESP.getFreeHeap();
  if(h < g_minFreeHeap) g_minFreeHeap = h;

  if(h < 4096) {
    if(!lowSince)
      lowSince = now ? now : 1;
    else if(now - lowSince > 10000) {
      SERIAL_ECHOLN("!! free heap critical for 10s - restarting");
      SERIAL_FLUSH();
      delay(100);
      ESP.restart();
    }
  }
  else
    lowSince = 0;
}

// ------------------------
void printHelp() {
  SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
  SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
  SERIAL_ECHOLN("- M52: Start to connect the wifi");
  SERIAL_ECHOLN("- M53: Check the connection status");
}

#if ENABLE_OTA
// ------------------------
void otaHandle() {
  if(!otaStarted) {
    if(!network.isConnected())
      return;
    ArduinoOTA.setHostname(config.hostname());
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();               // also starts the mDNS responder
    MDNS.addService("http", "tcp", 80);  // advertise the web UI: http://<name>.local
    otaStarted = true;
    return;
  }
  ArduinoOTA.handle();
  MDNS.update();                      // required or <name>.local stops resolving
}
#endif

// ------------------------
void blink()	{
// ------------------------
	LED_ON;
	delay(80);
	LED_OFF;
	delay(80);
}

// ------------------------
void errorBlink()	{
// ------------------------
	for(int i = 0; i < 100; i++)	{
		LED_ON;
		delay(50);
		LED_OFF;
		delay(50);
	}
}

// ------------------------
// non-blocking status LED:
//   connected    -> short blip every 3 seconds
//   connecting   -> handled inside network.start()
//   no wifi      -> slow 1 Hz blink
void statusBlink() {
  static uint32_t last = 0;
  static bool on = false;
  uint32_t now = millis();

  uint32_t period;
  if(network.isConnected())
    period = on ? 60 : 2940;
  else if(network.isConnecting())
    period = 250;
  else
    period = 500;

  if(now - last >= period) {
    last = now;
    on = !on;
    if(on) { LED_ON; } else { LED_OFF; }
  }
}
