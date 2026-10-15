// Проверка моторов без всей остальной программы (без маршрута и Wi-Fi). Только digitalWrite, без ШИМ.
// Залить, открыть Serial Monitor на 115200, нажать EN. Робот на подставке, аккумулятор ВКЛЮЧЁН.
//
// Для каждого мотора по очереди:
//   шаг 1: IN1 = HIGH, IN2 = LOW, 2 с — крутится в одну сторону
//   шаг 2: IN1 = LOW, IN2 = HIGH, 2 с — в другую сторону
//
// Не крутится -> проблема в железе (питание, общий GND, перемычки ENA/ENB, провода).
// Пины те же, что в robot_cube/motion.h — меняете там, меняйте и здесь.
#include <esp_system.h>

struct M { const char* name; uint8_t in1, in2; };
M motors[4] = {
  { "1 передний левый ", 26, 25 },
  { "2 передний правый", 33, 32 },
  { "3 задний левый   ", 16,  4 },
  { "4 задний правый  ", 18, 17 },
};

void allLow() {
  for (auto& m : motors) { digitalWrite(m.in1, LOW); digitalWrite(m.in2, LOW); }
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
    delay(2000); allLow(); delay(1000);
  }
  Serial.println("\n--- круг пройден, повтор через 3 с ---");
  delay(3000);
}
