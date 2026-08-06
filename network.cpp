#include "network.h"
#include "serial.h"
#include "config.h"
#include "pins.h"
#include "ESP8266WiFi.h"
#include "ESPWebDAV.h"
#include "sdControl.h"

String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ;
}

bool Network::start() {
  wifiConnected = false;
  wifiConnecting = true;

  // Don't let the SDK burn a flash write on every WiFi.begin() -
  // credentials already live in our own EEPROM/INI storage.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(config.hostname());
  // Modem power-save is the #1 cause of 100ms+ latency spikes and random
  // packet loss with many routers. This board is USB powered - keep radio on.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  // max TX power (20.5 dBm = hardware limit) - board is factory-floor gear,
  // range and link stability beat power draw
  WiFi.setOutputPower(20.5);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
  WiFi.setAutoReconnect(true);

  // optional static IP from SETUP.INI: instant connect, stable address
  if(config.hasStaticIP())
    WiFi.config(config.staticIP(), config.gateway(), config.subnet(), config.dnsServer());

  WiFi.begin(config.ssid(), config.password());

  // Wait for connection
  unsigned long waited = 0;
  while(WiFi.status() != WL_CONNECTED) {
    SERIAL_ECHO(".");
    if(waited > WIFI_CONNECT_TIMEOUT) {
      SERIAL_ECHOLN("");
      wifiConnecting = false;
      return false;
    }
    delay(100);
    waited += 100;
  }

  SERIAL_ECHOLN("");
  SERIAL_ECHO("Connected to "); SERIAL_ECHOLN(config.ssid());
  SERIAL_ECHO("Hostname: "); SERIAL_ECHOLN(config.hostname());
  SERIAL_ECHO("IP address: "); SERIAL_ECHOLN(WiFi.localIP());
  SERIAL_ECHO("RSSI: "); SERIAL_ECHOLN(WiFi.RSSI());
  SERIAL_ECHO("Access the SD at the Run prompt : \\\\"); SERIAL_ECHO(WiFi.localIP()); SERIAL_ECHOLN("\\DavWWWRoot");
  SERIAL_ECHO("Or in a browser : http://"); SERIAL_ECHOLN(WiFi.localIP());

  wifiConnected = true;

  config.save();
  String sIp = IpAddress2String(WiFi.localIP());
  config.save_ip(sIp.c_str());

  SERIAL_ECHOLN("Going to start DAV server");
  startDAVServer();
  wifiConnecting = false;

  return true;
}

int Network::startDAVServer() {
  // The TCP server ALWAYS comes up, even if the card is missing or another
  // host (USB reader / printer) owns the SPI bus right now. Stock returned
  // early here, so a board that booted with a busy bus answered pings but
  // refused port 80 forever - looked bricked. SD is mounted on demand by
  // handle() instead.
  if(!sdcontrol.canWeTakeBus()) {
    dav.beginServer(SERVER_PORT);
    initFailed = true;
    SERIAL_ECHOLN("WebDAV server started (SD busy - will mount on demand)");
    return 0;
  }

  sdcontrol.takeBusControl();

  // start the SD DAV server
  if(!dav.init(SD_CS, SPI_FULL_SPEED, SERVER_PORT))   {
    SERIAL_ECHOLN("ERROR: Failed to initialize SD Card - will retry when a client connects");
    initFailed = true;
  }
  else {
    initFailed = false;
  }

  sdcontrol.relinquishBusControl();
  SERIAL_ECHOLN("FYSETC WebDAV server started");
  return 0;
}

bool Network::isConnected() {
  return wifiConnected;
}

bool Network::isConnecting() {
  return wifiConnecting;
}

void Network::handle() {
  uint32_t now = millis();

  // track the real link state so a dropped AP pauses the server and the
  // LED shows it (auto-reconnect brings it back by itself)
  if(now - lastStatusPoll >= 500) {
    lastStatusPoll = now;
    if(!wifiConnecting)
      wifiConnected = (WiFi.status() == WL_CONNECTED);
  }
  if(!wifiConnected) return;

  // cheap connection bookkeeping every loop - no SD access inside
  dav.maintainClient();

  // SD never came up, or died since (card swapped / glitched): re-mount in
  // the BACKGROUND only. sd.begin() on an empty slot blocks ~3 s, so doing it
  // with a client waiting stalled every request past its timeout. Back off
  // 3 s -> 30 s while the slot stays empty so an unused board stays snappy;
  // a card that appears is picked up within one interval, no reboot needed.
  if((initFailed || !dav.sdHealthy()) && !dav.requestPending()
     && now - lastSdRetry >= sdRetryDelay && sdcontrol.canWeTakeBus()) {
    lastSdRetry = now;
    sdcontrol.takeBusControl();
    // cheap presence check first - the expensive mount only runs when a card
    // is actually in the slot
    bool fixed = dav.cardPresent(SD_CS) && dav.initSD(SD_CS, SPI_FULL_SPEED);
    sdcontrol.relinquishBusControl();
    initFailed = !fixed;
    if(fixed) {
      sdRetryDelay = SD_RETRY_MIN_MS;
      SERIAL_ECHOLN("SD card mounted");
    }
    else {
      sdRetryDelay *= 2;
      if(sdRetryDelay > SD_RETRY_MAX_MS) sdRetryDelay = SD_RETRY_MAX_MS;
    }
    lastSdRetry = millis();   // a real mount attempt can take seconds
  }

  if(!dav.requestPending()) return;

  if(initFailed || !dav.sdHealthy()) {
    dav.rejectClient("SD card not ready");
    return;
  }

  // has other master been using the bus in last few seconds
  if(!sdcontrol.canWeTakeBus()) {
    dav.rejectClient("SD busy - another host is using the card");
    return;
  }

  sdcontrol.takeBusControl();
  dav.handleClient();
  sdcontrol.relinquishBusControl();
}

Network network;
