// Имитатор манипулятора ROBOT_ARM на запасном ESP32.
// Поднимает точку доступа ROBOT_ARM (IP по умолчанию 192.168.4.1) и отвечает по протоколу из ТЗ.
// Позволяет отлаживать UDP-часть, пока реальный манипулятор занят.

#include <WiFi.h>
#include <WiFiUdp.h>

#define DROP_FIRST_LOAD 1   // 1 = «потерять» первый LOAD, чтобы проверить повтор через 3 с

WiFiUDP udp;
bool busy = false, dropped = false;
uint32_t tLoad = 0, lastDone = 0;
IPAddress client;
uint16_t clientPort = 0;
int doneSent = 0;

void reply(IPAddress ip, uint16_t port, const char* s) {
  udp.beginPacket(ip, port);
  udp.print(s);
  udp.endPacket();
  Serial.printf("-> %s\n", s);
}

void setup() {
  Serial.begin(115200);
  WiFi.softAP("ROBOT_ARM", "12345678");
  udp.begin(4210);
  Serial.print("Манипулятор-имитатор: ");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  if (udp.parsePacket() > 0) {
    char b[64];
    int l = udp.read(b, sizeof(b) - 1);
    b[l > 0 ? l : 0] = 0;
    String c(b);
    c.trim();
    Serial.printf("<- %s\n", c.c_str());
    IPAddress ip = udp.remoteIP();
    uint16_t p = udp.remotePort();

    if (c == "PING") {
      reply(ip, p, "PONG");
    } else if (c == "STATUS") {
      reply(ip, p, busy ? "BUSY" : "IDLE");
    } else if (c == "LOAD") {
      if (DROP_FIRST_LOAD && !dropped) {
        dropped = true;
        Serial.println("(имитация потери пакета)");
      } else if (busy) {
        reply(ip, p, "BUSY");
      } else {
        busy = true; tLoad = millis(); doneSent = 0;
        client = ip; clientPort = p;
        reply(ip, p, "STARTED");
      }
    }
  }

  // погрузка ~14 с, затем DONE до 3 раз
  if (busy && millis() - tLoad >= 14000 && millis() - lastDone >= 500) {
    reply(client, clientPort, "DONE");
    lastDone = millis();
    if (++doneSent >= 3) busy = false;
  }
}
