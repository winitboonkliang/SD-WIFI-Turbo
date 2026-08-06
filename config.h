#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdio.h>
#include <string.h>
#include <IPAddress.h>

#define WIFI_SSID_LEN 32
#define WIFI_PASSWD_LEN 64
#define HOSTNAME_LEN 24
#define HOSTNAME_DEFAULT "FYSETC"

#define EEPROM_SIZE 512

// NOTE: layout must stay compatible with boards already in the field.
// flag2/host are appended - old EEPROM images have 0xFF there, which reads
// as "not set" (flag2 != magic), so upgrades are safe.
#define CONFIG_FLAG2_MAGIC 0x5A

typedef struct config_type
{
  unsigned char flag; // Was saved before?
  char ssid[32];
  char psw[64];
  unsigned char flag2;  // CONFIG_FLAG2_MAGIC when host[] below is valid
  char host[HOSTNAME_LEN];
  uint32_t sbaud;       // serial baud; validated against a whitelist on read
  uint16_t blockout;    // seconds to stay off the SPI bus after another
                        // master touches the card; 0xFFFF (erased) -> default
}CONFIG_TYPE;

#define BLOCKOUT_DEFAULT_S  10
#define BLOCKOUT_MIN_S      1
#define BLOCKOUT_MAX_S      300

class Config	{
public:
  int loadSD();
	unsigned char load();
  char* ssid();
  void ssid(char* ssid);
  char* password();
  void password(char* password);
  void save(const char*ssid,const char*password);
  void save();
  int save_ip(const char *ip);

  // hostname precedence: web-set (EEPROM) > SETUP.INI NAME > "FYSETC"
  const char* hostname();
  void setHostname(const char* n);   // persists to EEPROM (web rename)

  uint32_t baud();                   // whitelisted, defaults to 115200
  void setBaud(uint32_t b);          // persists to EEPROM (web settings)

  uint16_t blockoutSec();            // range-checked, defaults to 10 s
  void setBlockoutSec(uint16_t s);   // persists to EEPROM (web settings)
  bool hasStaticIP();
  IPAddress staticIP()  { return IPAddress(_ip);   }
  IPAddress gateway()   { return IPAddress(_gw);   }
  IPAddress subnet()    { return IPAddress(_mask); }
  IPAddress dnsServer() { return IPAddress(_dns);  }

protected:
  CONFIG_TYPE data;
  char _hostname[HOSTNAME_LEN] = {0};
  uint32_t _ip = 0, _gw = 0, _mask = 0, _dns = 0;
};

extern Config config;

#endif
