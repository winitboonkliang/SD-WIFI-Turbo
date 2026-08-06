#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <Arduino.h>

#define SERVER_PORT		80

#define WIFI_CONNECT_TIMEOUT 30000UL

// background SD re-mount pacing (an empty slot makes sd.begin() block ~3 s)
#define SD_RETRY_MIN_MS  1500UL
#define SD_RETRY_MAX_MS  6000UL

class Network {
public:
  Network() { initFailed = false; wifiConnecting = true; wifiConnected = false;
              lastStatusPoll = 0; lastSdRetry = 0; sdRetryDelay = SD_RETRY_MIN_MS; }
  bool start();
  int startDAVServer();
  bool isConnected();
  bool isConnecting();
  void handle();

private:
  bool wifiConnected;
  bool wifiConnecting;
  bool initFailed;
  uint32_t lastStatusPoll;
  uint32_t lastSdRetry;
  uint32_t sdRetryDelay;
};

extern Network network;

#endif
