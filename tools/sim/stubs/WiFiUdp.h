// Заглушка WiFiUdp.h для прогона прошивки на ПК (tools/sim).
// Отправленные пакеты уходят в модель манипулятора (sim::onSend), ответы она кладёт в sim::inbox.
#pragma once
#include "Arduino.h"
#include "WiFi.h"
#include <map>
#include <vector>

namespace sim {
struct Sent { uint32_t at; std::string data; };
inline std::vector<Sent> sent;
inline std::multimap<uint32_t, std::string> inbox;   // время доставки -> содержимое
inline std::function<void(const std::string&)> onSend;
inline bool udpBound = false;
}  // namespace sim

class WiFiUDP {
  std::string out_, cur_;
 public:
  uint8_t begin(uint16_t port) {
    if (port == 0) sim::fail("udp.begin(0)");
    sim::udpBound = true;
    return 1;
  }
  int beginPacket(IPAddress, uint16_t) { out_.clear(); return 1; }
  size_t print(const char* s) { out_ += s; return strlen(s); }
  int endPacket() {
    if (!sim::udpBound) sim::fail("endPacket до udp.begin()");
    if (WiFi.status() != WL_CONNECTED) sim::fail("отправка UDP без Wi-Fi");
    sim::sent.push_back({sim::now_ms, out_});
    if (sim::onSend) sim::onSend(out_);
    return 1;
  }
  int parsePacket() {
    if (!sim::udpBound) return 0;
    auto it = sim::inbox.begin();
    if (it == sim::inbox.end() || it->first > sim::now_ms) return 0;
    cur_ = it->second;
    sim::inbox.erase(it);
    return (int)cur_.size();
  }
  IPAddress remoteIP() { return IPAddress(192, 168, 4, 2); }   // для tools/mock_arm
  uint16_t remotePort() { return 4210; }
  int read(char* buf, size_t len) {
    size_t n = cur_.size() < len ? cur_.size() : len;
    memcpy(buf, cur_.data(), n);
    cur_.erase(0, n);
    return (int)n;
  }
};
