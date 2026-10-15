// Проверка моторов без всей остальной программы (без маршрута, Wi-Fi и плавного разгона).
// Залить, открыть Serial Monitor 115200, робот на подставке, аккумулятор ВКЛЮЧЁН.
//
// Для каждого мотора по очереди:
//   1) digitalWrite: IN1 = HIGH, IN2 = LOW, 2 с  — полное напряжение, без ШИМ
//   2) digitalWrite: IN1 = LOW,  IN2 = HIGH, 2 с — в обратную сторону
//   3) ШИМ (ledcWrite) 100% на IN1, 2 с         — так же, как крутит основная программа
//
// Не крутится даже на шаге 1 -> проблема в железе (питание, земля, перемычки ENA/ENB, провода).
// Шаг 1 крутится, а шаг 3 нет -> проблема в ШИМ, сообщить.

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

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Проверка моторов ===");
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
    bool ok = ledcAttach(m.in1, 1000, 8);   // API ядра ESP32 3.x
    if (!ok) Serial.println("  ОШИБКА: ledcAttach не сработал");
    ledcWrite(m.in1, 255);
    delay(2000);
    ledcWrite(m.in1, 0);
    ledcDetach(m.in1);
    pinMode(m.in1, OUTPUT);
    allLow();
    delay(1000);
  }
  Serial.println("\n--- круг пройден, повтор через 3 с ---");
  delay(3000);
}
