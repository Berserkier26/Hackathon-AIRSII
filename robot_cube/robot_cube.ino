// Хакатон «Довезти кубик» — основная программа
// Плата: ESP32 Dev Module, ядро ESP32 3.x, Arduino IDE
//
// Файлы проекта (в Arduino IDE это вкладки):
//   robot_cube.ino — логика попытки и UDP (здесь работает человек на связи)
//   motion.h       — моторы, разгон, пересчёт метров во время (человек на движении)
//   route.h        — сам маршрут, крутим на поле
//
// Порядок настройки на поле:
//   1. WHEEL_TEST 1 -> каждое колесо крутится «вперёд». Неправильное -> dir = -1 в motion.h
//   2. CALIB_TEST 1 и 2 -> калибруем SPEED_FWD и SPEED_SIDE в motion.h
//   3. TUNE_STEPS 1, 2, 3... -> проверяем маршрут по кускам, подгоняем k в route.h
//   4. Все режимы в 0 -> полный прогон

#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>   // esp_reset_reason(): почему плата запустилась
#include "motion.h"
#include "route.h"

// ================== РЕЖИМЫ ОТЛАДКИ ==================
#define WHEEL_TEST 0   // 1 = проверка направления колёс
#define CALIB_TEST 0   // 1 = «2 м вперёд», 2 = «2 м вбок вправо», затем стоп
#define TUNE_STEPS 0   // N > 0 = проехать только первые N отрезков ROUTE_TO_ARM и стоп

// ================== СВЯЗЬ ==================
const char*     WIFI_SSID  = "ROBOT_ARM";
const char*     WIFI_PASS  = "12345678";
const IPAddress ARM_IP(192, 168, 4, 1);
const uint16_t  ARM_PORT   = 4210;
const uint16_t  LOCAL_PORT = 4210;   // ответ придёт на адрес и порт отправителя

const uint32_t START_DELAY_MS  = 3000;   // пауза после включения питания: успеть убрать руки
const uint32_t LOAD_RETRY_MS   = 3000;   // по ТЗ: нет STARTED за 3 с -> LOAD снова
const uint32_t DONE_TIMEOUT_MS = 30000;  // по ТЗ: ждём DONE не дольше 30 с
const uint32_t WIFI_KICK_MS    = 10000;  // стоим у манипулятора без Wi-Fi столько -> подключаемся заново

// ================== КОНЕЧНЫЙ АВТОМАТ: СОСТОЯНИЯ ==================
// Объявлено выше всех функций: Arduino сам вставляет прототипы функций перед первой из них,
// и прототип go(State) без этого оказался бы раньше enum State -> ошибка компиляции.
enum State { WAIT_START, TO_ARM, REQ_LOAD, WAIT_DONE, TO_UNLOAD, FINISHED };
const char* STATE_NAMES[] = { "WAIT_START", "TO_ARM", "REQ_LOAD", "WAIT_DONE", "TO_UNLOAD", "FINISHED" };

State state = WAIT_START;
uint32_t tState = 0, lastLoad = 0, lastTick = 0, lastWifiKick = 0;

WiFiUDP udp;
bool udpReady = false;

void sendCmd(const char* cmd) {
  udp.beginPacket(ARM_IP, ARM_PORT);
  udp.print(cmd);
  bool ok = udp.endPacket();
  Serial.printf("[UDP] -> %s%s\n", cmd, ok ? "" : " (не отправлено, повторим)");
}

String readReply() {
  int n = udp.parsePacket();
  if (n <= 0) return "";
  char buf[64];
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len < 0) len = 0;
  buf[len] = 0;
  String s(buf);
  s.trim();
  s.toUpperCase();
  Serial.printf("[UDP] <- %s\n", s.c_str());
  return s;
}

// ================== КОНЕЧНЫЙ АВТОМАТ ==================
void go(State s) {
  state = s;
  tState = millis();
  Serial.printf("[STATE] %s\n", STATE_NAMES[s]);
}

void startUnload() {
  startRoute(ROUTE_TO_UNLOAD, LEN(ROUTE_TO_UNLOAD));
  go(TO_UNLOAD);
}

void printBootInfo() {
  esp_reset_reason_t r = esp_reset_reason();
  const char* why = r == ESP_RST_POWERON ? "включение питания"
                  : r == ESP_RST_EXT || r == ESP_RST_SW ? "кнопка EN / перезапуск после заливки"
                  : r == ESP_RST_BROWNOUT ? "ПРОСАДКА ПИТАНИЯ (brownout)"
                  : r == ESP_RST_PANIC ? "сбой программы (panic)"
                  : "другая";
  Serial.printf("\n[BOOT] robot_cube запущен, причина: %s (код %d)\n", why, (int)r);
  if (r == ESP_RST_BROWNOUT)
    Serial.println("[BOOT] Плата перезагрузилась от просадки напряжения: зарядить аккумулятор, проверить XT60 и провода питания");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  printBootInfo();
  if (!motionBegin()) Serial.println("[ОШИБКА] не все моторы получили ШИМ — смотри строки выше");

#if WHEEL_TEST
  // Крутим по кругу бесконечно: тест не пропустить, если аккумулятор включили или монитор открыли позже.
  const char* WHEEL_NAMES[] = { "передний левый", "передний правый", "задний левый", "задний правый" };
  delay(2000);
  while (true) {
    Serial.println("\n=== WHEEL_TEST: каждое колесо должно крутиться ВПЕРЁД 1,5 с ===");
    Serial.println("Строки идут, а колёса стоят -> питание L298N, общий GND с ESP32, перемычки ENA/ENB");
    for (int i = 0; i < 4; i++) {
      Serial.printf("Мотор %d (%s), IN1=%d IN2=%d: вперёд\n", i + 1, WHEEL_NAMES[i], motors[i].in1, motors[i].in2);
      motorWrite(i, 0.8f);
      delay(1500);
      motorsOff();
      delay(700);
    }
    delay(2000);
  }
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // подключается в фоне, пока робот едет

  lastTick = millis();
  go(WAIT_START);
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastTick) / 1000.0f;
  lastTick = now;

  switch (state) {
    case WAIT_START:
      if (now - tState >= START_DELAY_MS) {
#if CALIB_TEST == 1
        startRoute(CALIB_FWD, LEN(CALIB_FWD));
#elif CALIB_TEST == 2
        startRoute(CALIB_SIDE, LEN(CALIB_SIDE));
#else
        size_t n = LEN(ROUTE_TO_ARM);
        if (TUNE_STEPS > 0 && (size_t)TUNE_STEPS < n) n = TUNE_STEPS;
        startRoute(ROUTE_TO_ARM, n);
#endif
        go(TO_ARM);
      }
      break;

    case TO_ARM:
      if (routeTick(dt)) {
        motorsOff();          // критерий «моторы выключены до отправки LOAD»
        if (CALIB_TEST || TUNE_STEPS) { go(FINISHED); break; }
        lastLoad = 0;
        lastWifiKick = now;
        go(REQ_LOAD);
      }
      break;

    case REQ_LOAD: {
      if (WiFi.status() != WL_CONNECTED) {
        // Автопереподключение ядра срабатывает не после любой ошибки (например, после неудачной
        // первой попытки). Чтобы не стоять у манипулятора вечно, начинаем подключение заново.
        if (now - lastWifiKick >= WIFI_KICK_MS) {
          lastWifiKick = now;
          Serial.println("[WIFI] нет связи с ROBOT_ARM, подключаемся заново");
          WiFi.disconnect();
          WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
        break;
      }
      if (!udpReady) {
        udpReady = udp.begin(LOCAL_PORT);
        Serial.printf("[WIFI] подключено, IP %s, UDP-порт %u %s\n", WiFi.localIP().toString().c_str(),
                      (unsigned)LOCAL_PORT, udpReady ? "открыт" : "не открылся");
        if (!udpReady) break;
      }
      if (lastLoad == 0 || now - lastLoad >= LOAD_RETRY_MS) {
        sendCmd("LOAD");
        lastLoad = now;
      }
      String r = readReply();
      // indexOf, а не ==: переживём ответ вида «STARTED 1» или «OK DONE» (слова протокола не пересекаются)
      // BUSY в ответ на повторный LOAD значит, что погрузка уже идёт (STARTED потерялся)
      if (r.indexOf("STARTED") >= 0 || r.indexOf("BUSY") >= 0) go(WAIT_DONE);
      // DONE раньше STARTED/BUSY: все ответы на LOAD потерялись, а погрузка уже закончилась.
      // Без этой ветки робот отправил бы LOAD снова и запустил вторую погрузку.
      else if (r.indexOf("DONE") >= 0) startUnload();
      break;
    }

    case WAIT_DONE: {
      String r = readReply();
      bool done = r.indexOf("DONE") >= 0;
      if (done || now - tState >= DONE_TIMEOUT_MS) {
        if (!done) Serial.println("[WARN] DONE не пришёл за 30 с, едем дальше");
        startUnload();
      }
      break;
    }

    case TO_UNLOAD:
      if (routeTick(dt)) {
        motorsOff();
        go(FINISHED);
      }
      break;

    case FINISHED:
      break;
  }
  delay(5);
}
