// Заглушка WiFi.h для прогона прошивки на ПК (tools/sim).
// Сеть «поднимается» через connectDelayMs после N-го вызова begin() (N = beginsNeeded).
#pragma once
#include "Arduino.h"

#define WIFI_STA 1
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

class IPAddress {
  uint8_t b_[4] = {0, 0, 0, 0};
 public:
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { b_[0] = a; b_[1] = b; b_[2] = c; b_[3] = d; }
  String toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
    return String(buf);
  }
};

namespace sim {
inline int beginsNeeded = 1;          // сколько раз нужно вызвать begin(), чтобы сеть поднялась
inline uint32_t connectDelayMs = 2000;
inline int beginCalls = 0;
inline uint32_t lastBeginAt = 0;
inline bool wifiDown = false;         // принудительно «нет сети» (сценарий: точка не видна)
}  // namespace sim

struct SimWiFi {
  void mode(int) {}
  void setAutoReconnect(bool) {}
  void begin(const char* ssid, const char* pass) {
    if (strcmp(ssid, "ROBOT_ARM") != 0 || strcmp(pass, "12345678") != 0)
      sim::fail("WiFi.begin: неверные SSID/пароль");
    sim::beginCalls++;
    sim::lastBeginAt = sim::now_ms;
  }
  bool disconnect(bool = false, bool = false) { return true; }
  int status() {
    if (sim::wifiDown || sim::beginCalls < sim::beginsNeeded) return WL_DISCONNECTED;
    return sim::now_ms - sim::lastBeginAt >= sim::connectDelayMs ? WL_CONNECTED : WL_DISCONNECTED;
  }
  IPAddress localIP() { return IPAddress(192, 168, 4, 2); }
  bool softAP(const char*, const char*) { return true; }   // для tools/mock_arm
  IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
};
inline SimWiFi WiFi;
