// Заглушка Arduino.h для прогона прошивки на ПК (tools/sim). На робот не идёт.
// Время виртуальное: delay() двигает часы и на каждой миллисекунде вызывает модель мира.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <math.h>
#include <string>
#include <functional>

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

namespace sim {
inline uint32_t now_ms = 0;
inline uint32_t duty[40] = {0};
inline bool attached[40] = {false};
inline bool quiet = false;
inline std::function<void()> onTick;   // вызывается на каждой виртуальной миллисекунде

inline void fail(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  fprintf(stderr, "[SIM FAIL t=%u] ", now_ms); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}
}  // namespace sim

inline uint32_t millis() { return sim::now_ms; }
inline void delay(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) { sim::now_ms++; if (sim::onTick) sim::onTick(); }
}

// API ШИМ ядра ESP32 3.x
inline bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t res) {
  if (pin >= 40) sim::fail("ledcAttach: пин %d", pin);
  if (res != 8) sim::fail("ledcAttach: симулятор рассчитан на 8 бит, получено %d", res);
  (void)freq;
  sim::attached[pin] = true;
  return true;
}
inline bool ledcWrite(uint8_t pin, uint32_t duty) {
  if (pin >= 40 || !sim::attached[pin]) sim::fail("ledcWrite на неинициализированный пин %d", pin);
  if (duty > 255) sim::fail("ledcWrite: скважность %u > 255 на пине %d", duty, pin);
  sim::duty[pin] = duty;
  return true;
}

class String {
  std::string s_;
 public:
  String() {}
  String(const char* c) : s_(c ? c : "") {}
  const char* c_str() const { return s_.c_str(); }
  unsigned length() const { return (unsigned)s_.size(); }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n");
    size_t b = s_.find_last_not_of(" \t\r\n");
    s_ = (a == std::string::npos) ? "" : s_.substr(a, b - a + 1);
  }
  void toUpperCase() { for (auto& ch : s_) ch = (char)toupper((unsigned char)ch); }
  int indexOf(const char* sub) const {
    size_t p = s_.find(sub);
    return p == std::string::npos ? -1 : (int)p;
  }
  bool startsWith(const char* p) const { return s_.rfind(p, 0) == 0; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator!=(const char* o) const { return s_ != o; }
};

struct SimSerial {
  void begin(unsigned long) {}
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (sim::quiet) return 0;
    va_list ap; va_start(ap, fmt);
    ::printf("%7.3f  ", sim::now_ms / 1000.0);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
  }
  void println(const char* s) { if (!sim::quiet) ::printf("%7.3f  %s\n", sim::now_ms / 1000.0, s); }
  void print(const char* s) { if (!sim::quiet) ::printf("%s", s); }
  template <class T> void println(const T& v) { println(v.toString().c_str()); }   // IPAddress
};
inline SimSerial Serial;
