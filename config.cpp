#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <EEPROM.h>
#include "pins.h"
#include "config.h"
#include "serial.h"
#include "sdControl.h"

int Config::loadSD() {
  SdFat sdfat;

  SERIAL_ECHOLN("Going to load config from INI file");

  if(!sdcontrol.canWeTakeBus()) {
    SERIAL_ECHOLN("Another host is controlling the bus");
    return -1;
  }
  sdcontrol.takeBusControl();

  if(!sdfat.begin(SD_CS, SPI_FULL_SPEED)) {
    SERIAL_ECHOLN("Initial SD failed");
    sdcontrol.relinquishBusControl();
    return -2;
  }

  File file = sdfat.open("SETUP.INI", FILE_READ);
  if (!file) {
    SERIAL_ECHOLN("Open INI file failed");
    sdcontrol.relinquishBusControl();
    return -3;
  }

  // Get SSID and PASSWORD (and optional NAME / IP / GATEWAY / SUBNET / DNS)
  int rst = 0,step = 0;
  String buffer,sKEY,sValue;
  IPAddress tmp;
  while (file.available()) { // check for EOF
    buffer = file.readStringUntil('\n');
    if(buffer.length() == 0) continue; // Empty line
    buffer.replace("\r", ""); // Delete all CR
    int iS = buffer.indexOf('='); // Get the seperator
    if(iS < 0) continue; // Bad line
    sKEY = buffer.substring(0,iS);
    sKEY.trim();
    sValue = buffer.substring(iS+1);
    sValue.trim();
    if(sKEY == "SSID") {
      SERIAL_ECHOLN("INI file : SSID found");
      if(sValue.length()>0) {
        memset(data.ssid,'\0',WIFI_SSID_LEN);
        sValue.toCharArray(data.ssid,WIFI_SSID_LEN);
        step++;
      }
      else {
        rst = -4;
        goto FAIL;
      }
    }
    else if(sKEY == "PASSWORD") {
      SERIAL_ECHOLN("INI file : PASSWORD found");
      if(sValue.length()>0) {
        memset(data.psw,'\0',WIFI_PASSWD_LEN);
        sValue.toCharArray(data.psw,WIFI_PASSWD_LEN);
        step++;
      }
      else {
        rst = -5;
        goto FAIL;
      }
    }
    else if(sKEY == "NAME" || sKEY == "HOSTNAME") {
      if(sValue.length()>0) {
        memset(_hostname, 0, sizeof(_hostname));
        sValue.toCharArray(_hostname, sizeof(_hostname));
        SERIAL_ECHO("INI file : NAME "); SERIAL_ECHOLN(_hostname);
      }
    }
    else if(sKEY == "IP") {
      if(tmp.fromString(sValue)) _ip = (uint32_t)tmp;
    }
    else if(sKEY == "GATEWAY") {
      if(tmp.fromString(sValue)) _gw = (uint32_t)tmp;
    }
    else if(sKEY == "SUBNET") {
      if(tmp.fromString(sValue)) _mask = (uint32_t)tmp;
    }
    else if(sKEY == "DNS") {
      if(tmp.fromString(sValue)) _dns = (uint32_t)tmp;
    }
    else if(sKEY == "BLOCKOUT") {
      long b = sValue.toInt();
      if(b >= BLOCKOUT_MIN_S && b <= BLOCKOUT_MAX_S) {
        data.blockout = (uint16_t) b;
        SERIAL_ECHO("INI file : BLOCKOUT "); SERIAL_ECHOLN(data.blockout);
      }
    }
    else continue; // Unknown key
  }
  if(step != 2) { // We miss ssid or password
    SERIAL_ECHOLN("Please check your SSID or PASSWORD in ini file");
    rst = -6;
    goto FAIL;
  }

  FAIL:
  file.close();
  sdcontrol.relinquishBusControl();
  return rst;
}

unsigned char Config::load() {
  // Always read EEPROM first - it may hold the web-set hostname and the
  // last good credentials, even when an INI file is present.
  EEPROM.begin(EEPROM_SIZE);
  uint8_t *p = (uint8_t*)(&data);
  for (unsigned int i = 0; i < sizeof(data); i++)
  {
    *(p + i) = EEPROM.read(i);
  }

  // validate appended hostname field (old images have 0xFF here)
  if(data.flag2 != CONFIG_FLAG2_MAGIC) {
    data.flag2 = 0;
    memset(data.host, 0, sizeof(data.host));
  }
  else
    data.host[sizeof(data.host) - 1] = 0;

  bool eepromValid = (data.flag == 1);

  // INI file (if present) overrides ssid/password and sets NAME/static-IP
  if(0 == loadSD())
  {
    return 1;
  }

  if(!eepromValid) {
    SERIAL_ECHOLN("We didn't connect the network before");
    return 0;
  }
  SERIAL_ECHOLN("Going to use the old config to connect the network");
  return 1;
}

char* Config::ssid() {
  return data.ssid;
}

void Config::ssid(char* ssid) {
  if(ssid == NULL) return;
  strncpy(data.ssid,ssid,WIFI_SSID_LEN);
}

char* Config::password() {
  return data.psw;
}

void Config::password(char* password) {
  if(password == NULL) return;
  strncpy(data.psw,password,WIFI_PASSWD_LEN);
}

const char* Config::hostname() {
  if(data.flag2 == CONFIG_FLAG2_MAGIC && data.host[0])
    return data.host;                       // set from the web UI
  return _hostname[0] ? _hostname : HOSTNAME_DEFAULT;
}

void Config::setHostname(const char* n) {
  if(n == NULL || !n[0]) return;
  memset(data.host, 0, sizeof(data.host));
  strncpy(data.host, n, sizeof(data.host) - 1);
  data.flag2 = CONFIG_FLAG2_MAGIC;
  save();
}

static const uint32_t VALID_BAUDS[] =
  { 9600, 19200, 38400, 57600, 74880, 115200, 230400, 250000, 460800, 921600 };

uint32_t Config::baud() {
  for (unsigned int i = 0; i < sizeof(VALID_BAUDS)/sizeof(VALID_BAUDS[0]); i++)
    if (data.sbaud == VALID_BAUDS[i]) return data.sbaud;
  return 115200;  // old/erased EEPROM reads 0xFFFFFFFF -> default
}

void Config::setBaud(uint32_t b) {
  data.sbaud = b;
  save();
}

uint16_t Config::blockoutSec() {
  if (data.blockout >= BLOCKOUT_MIN_S && data.blockout <= BLOCKOUT_MAX_S)
    return data.blockout;
  return BLOCKOUT_DEFAULT_S;   // erased EEPROM reads 0xFFFF
}

void Config::setBlockoutSec(uint16_t s) {
  if (s < BLOCKOUT_MIN_S || s > BLOCKOUT_MAX_S) return;
  data.blockout = s;
  save();
}

bool Config::hasStaticIP() {
  return _ip != 0 && _gw != 0 && _mask != 0;
}

void Config::save(const char*ssid,const char*password) {
  if(ssid ==NULL || password==NULL)
    return;

  data.flag = 1;
  strncpy(data.ssid, ssid, WIFI_SSID_LEN);
  strncpy(data.psw, password, WIFI_PASSWD_LEN);
  save();
}

void Config::save() {
  data.flag = 1;

  EEPROM.begin(EEPROM_SIZE);
  // only burn the flash sector when something actually changed
  CONFIG_TYPE current;
  uint8_t *c = (uint8_t*)(&current);
  for (unsigned int i = 0; i < sizeof(current); i++)
    *(c + i) = EEPROM.read(i);

  if(memcmp(&current, &data, sizeof(data)) == 0)
    return;

  uint8_t *p = (uint8_t*)(&data);
  for (unsigned int i = 0; i < sizeof(data); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

// Save the ip address to sdcard as ip.gcode (M117 shows it on a printer LCD)
int Config::save_ip(const char *ip) {
  SdFat sdfat;

  if(!sdcontrol.canWeTakeBus()) {
    return -1;
  }
  sdcontrol.takeBusControl();

  if(!sdfat.begin(SD_CS, SPI_FULL_SPEED)) {
    SERIAL_ECHOLN("Initial SD failed");
    sdcontrol.relinquishBusControl();
    return -2;
  }

  // Remove the old file
  sdfat.remove("ip.gcode");

  File file = sdfat.open("ip.gcode", FILE_WRITE);
  if (!file) {
    SERIAL_ECHOLN("Open ip file failed");
    sdcontrol.relinquishBusControl();
    return -3;
  }

  char buf[24] = "M117 ";
  strncat(buf,ip,15);
  strcat(buf,"\n");
  file.write(buf, strlen(buf));
  file.close();
  sdcontrol.relinquishBusControl();
  return 0;
}

Config config;
