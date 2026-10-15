// Проверка моторов без всей остальной программы (без маршрута, Wi-Fi и плавного разгона).
// Залить, открыть Serial Monitor на 115200, нажать EN. Робот на подставке, аккумулятор ВКЛЮЧЁН.
//
// Для каждого мотора по очереди:
//   шаг 1: digitalWrite, IN1 = HIGH, IN2 = LOW, 2 с  — полное напряжение, без ШИМ
//   шаг 2: digitalWrite, IN1 = LOW,  IN2 = HIGH, 2 с — в обратную сторону
//   шаг 3: ШИМ 100% на IN1, 2 с                       — так же, как крутит основная программа
//
// Не крутится даже на шаге 1 -> проблема в железе (питание, общий GND, перемычки ENA/ENB, провода).
// Шаг 1 крутится, а шаг 3 нет -> проблема в ШИМ.
#include <esp_system.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

struct M { const char* name; uint8_t in1, in2; };
M motors[4] = {
  { "1 передний левый  (L298N #2, IN1/IN2)", 26, 25 },
  { "2 передний правый (L298N #2, IN3/IN4)", 33, 32 },
  { "3 задний левый    (L298N #1, IN3/IN4)", 16,  4 },
  { "4 задний правый   (L298N #1, IN1/IN2)", 18, 17 },
};

void allLow() {
  for (auto& m : motors) { digitalWrite(m.in1, LOW); digitalWrite(m.in2, LOW); }
}

// ШИМ 100% на пине 2 секунды; работает на ядре ESP32 и 3.x, и 2.x
void pwmFull2s(uint8_t pin) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(pin, 1000, 8)) Serial.println("  ОШИБКА: ШИМ не подключился к пину");
  ledcWrite(pin, 255);
  delay(2000);
  ledcWrite(pin, 0);
  ledcDetach(pin);
#else
  ledcSetup(0, 1000, 8);
  ledcAttachPin(pin, 0);
  ledcWrite(0, 255);
  delay(2000);
  ledcWrite(0, 0);
  ledcDetachPin(pin);
#endif
  pinMode(pin, OUTPUT);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  esp_reset_reason_t r = esp_reset_reason();
  Serial.printf("\n=== Проверка моторов === (причина запуска: код %d%s)\n", (int)r,
                r == ESP_RST_BROWNOUT ? " — ПРОСАДКА ПИТАНИЯ, проверить аккумулятор" : "");
  for (auto& m : motors) { pinMode(m.in1, OUTPUT); pinMode(m.in2, OUTPUT); }
  allLow();
}

void loop() {
  for (auto& m : motors) {
    Serial.printf("\nМотор %s\n", m.name);

    Serial.printf("  шаг 1: IN1(%d)=HIGH IN2(%d)=LOW\n", m.in1, m.in2);
    digitalWrite(m.in1, HIGH); digitalWrite(m.in2, LOW);
    delay(2000); allLow(); delay(500);

    Serial.printf("  шаг 2: IN1(%d)=LOW IN2(%d)=HIGH\n", m.in1, m.in2);
    digitalWrite(m.in1, LOW); digitalWrite(m.in2, HIGH);
    delay(2000); allLow(); delay(500);

    Serial.printf("  шаг 3: ШИМ 100%% на IN1(%d)\n", m.in1);
    pwmFull2s(m.in1);
    allLow();
    delay(1000);
  }
  Serial.println("\n--- круг пройден, повтор через 3 с ---");
  delay(3000);
}
