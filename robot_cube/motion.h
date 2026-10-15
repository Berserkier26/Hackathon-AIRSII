// motion.h — моторы, кинематика меканум-колёс, плавный разгон, езда по отрезкам в метрах.
// Здесь работает человек, который отвечает за движение. Маршрут лежит отдельно в route.h.
#pragma once
#include <Arduino.h>

// ================== КАЛИБРОВКА ==================
// Как замерить (режим CALIB_TEST в robot_cube.ino):
//   CALIB_TEST 1: робот «думает», что едет 2 м вперёд. Мерим реальное расстояние d.
//                 SPEED_FWD = SPEED_FWD * d / 2.0
//   CALIB_TEST 2: то же вбок -> SPEED_SIDE = SPEED_SIDE * d / 2.0
// Мерить на том же полу и с заряженным аккумулятором.
const float CRUISE     = 0.6f;    // команда скорости на маршруте (0..1)
const float SPEED_FWD  = 0.50f;   // м/с вперёд при команде CRUISE
const float SPEED_SIDE = 0.40f;   // м/с вбок при команде CRUISE (у меканума обычно меньше)

// Драйверы — два L298N, перемычки ENA/ENB стоят (мосты всегда включены), ШИМ подаётся на IN.
// L298N медленно переключается и съедает ~2 В, поэтому частота пониже и порог трогания повыше.
const int   PWM_FREQ = 1000;
const int   PWM_RES  = 8;      // скважность 0..255
const int   MIN_DUTY = 90;     // ниже этого мотор не трогается (подобрать на поле)
const float ACCEL    = 1.2f;   // плавность: изменение скорости в секунду

#define LEN(a) (sizeof(a) / sizeof(a[0]))

// ================== МОТОРЫ ==================
struct Motor { uint8_t in1, in2; int8_t dir; float trim; };
// L298N №1 (задний): IN1=18 IN2=17 IN3=16 IN4=4
// L298N №2 (передний): IN1=26 IN2=25 IN3=33 IN4=32
// Если колесо в WHEEL_TEST крутится назад — dir = -1 (или поменять провода мотора местами).
Motor motors[4] = {
  //  IN1 IN2 dir  trim
  {  26, 25, +1, 1.00f },   // 0 передний левый   — L298N №2, выход A (IN1/IN2)
  {  33, 32, +1, 1.00f },   // 1 передний правый  — L298N №2, выход B (IN3/IN4)
  {  16,  4, +1, 1.00f },   // 2 задний левый     — L298N №1, выход B (IN3/IN4)
  {  18, 17, +1, 1.00f },   // 3 задний правый    — L298N №1, выход A (IN1/IN2)
};

// ================== ШИМ: ЛЮБОЕ ЯДРО ESP32 ==================
// Ядро 3.x: ledcAttach(пин) и ledcWrite(пин). Ядро 2.x: ledcSetup(канал) + ledcAttachPin и ledcWrite(канал).
// Версия ядра определяется при сборке, поэтому код собирается и крутит моторы на обоих.
// У пина IN мотора i канал 2*i (IN1) или 2*i+1 (IN2) — нужен только для ядра 2.x.
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
bool pwmAttach(uint8_t pin, uint8_t ch) { (void)ch; return ledcAttach(pin, PWM_FREQ, PWM_RES); }
void pwmWrite(uint8_t pin, uint8_t ch, uint32_t duty) { (void)ch; ledcWrite(pin, duty); }
#else
bool pwmAttach(uint8_t pin, uint8_t ch) { ledcSetup(ch, PWM_FREQ, PWM_RES); ledcAttachPin(pin, ch); return true; }
void pwmWrite(uint8_t pin, uint8_t ch, uint32_t duty) { (void)pin; ledcWrite(ch, duty); }
#endif

void motorWrite(int i, float s) {
  Motor& m = motors[i];
  s = constrain(s * m.dir * m.trim, -1.0f, 1.0f);
  int duty = 0;
  // lroundf, а не (int): отбрасывание дробной части занижало скважность медленных колёс на диагонали
  if (fabsf(s) > 0.02f) duty = MIN_DUTY + (int)lroundf(fabsf(s) * (255 - MIN_DUTY));
  if (s >= 0) { pwmWrite(m.in1, 2 * i, duty); pwmWrite(m.in2, 2 * i + 1, 0); }
  else        { pwmWrite(m.in1, 2 * i, 0);    pwmWrite(m.in2, 2 * i + 1, duty); }
}

void motorsOff() {
  for (int i = 0; i < 4; i++) { pwmWrite(motors[i].in1, 2 * i, 0); pwmWrite(motors[i].in2, 2 * i + 1, 0); }
}

// true — все 8 пинов получили ШИМ. Если false, в мониторе будет видно, какой пин не подключился.
bool motionBegin() {
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    if (!pwmAttach(motors[i].in1, 2 * i))     { Serial.printf("[ОШИБКА] ШИМ не подключился к пину %d\n", motors[i].in1); ok = false; }
    if (!pwmAttach(motors[i].in2, 2 * i + 1)) { Serial.printf("[ОШИБКА] ШИМ не подключился к пину %d\n", motors[i].in2); ok = false; }
  }
  motorsOff();
  return ok;
}

// vx: вперёд(+), vy: вправо(+), w: поворот по часовой(+)
// Если робот при vy > 0 едет влево, поменяйте знак vy во всех четырёх строках.
void drive(float vx, float vy, float w) {
  float s[4] = {
    vx + vy + w,   // передний левый
    vx - vy - w,   // передний правый
    vx - vy + w,   // задний левый
    vx + vy - w,   // задний правый
  };
  float mx = 1.0f;
  for (int i = 0; i < 4; i++) mx = fmaxf(mx, fabsf(s[i]));
  for (int i = 0; i < 4; i++) motorWrite(i, s[i] / mx);
}

// ================== ПЛАВНЫЙ РАЗГОН ==================
float cur[3] = {0, 0, 0}, tgt[3] = {0, 0, 0};

void setTarget(float vx, float vy, float w) { tgt[0] = vx; tgt[1] = vy; tgt[2] = w; }

void updateRamp(float dt) {
  float step = ACCEL * dt;
  for (int k = 0; k < 3; k++) cur[k] += constrain(tgt[k] - cur[k], -step, step);
  drive(cur[0], cur[1], cur[2]);
}

bool isStopped() {
  return fabsf(cur[0]) < 0.01f && fabsf(cur[1]) < 0.01f && fabsf(cur[2]) < 0.01f;
}

// ================== ОТРЕЗКИ В МЕТРАХ ==================
// Отрезок задаётся смещением в системе робота: fwd — вперёд(+)/назад(-), right — вправо(+)/влево(-).
// k — поправка времени для ручной подгонки конкретного отрезка (1.0 = без поправки).
// Робот не поворачивается: курс всё время один и тот же, меканум едет в любую сторону.
struct Seg { float fwd, right, k; };

// Пересчёт метров в команду и время.
// Время = часть вперёд / скорость вперёд + часть вбок / скорость вбок.
// Команды подобраны так, что самое нагруженное колесо крутится ровно на CRUISE,
// поэтому прямо, вбок и по диагонали колёса работают в одном и том же режиме.
void segToCmd(const Seg& s, float& vx, float& vy, uint32_t& ms) {
  float t = fabsf(s.fwd) / SPEED_FWD + fabsf(s.right) / SPEED_SIDE;   // секунды
  vx = 0; vy = 0; ms = 0;
  if (t <= 0) return;
  vx = s.fwd   * CRUISE / (SPEED_FWD  * t);
  vy = s.right * CRUISE / (SPEED_SIDE * t);
  ms = (uint32_t)(t * s.k * 1000.0f);
}

const Seg* route = nullptr;
int routeLen = 0;
int routeIdx = -1;                  // -1 — пауза перед первым отрезком, routeLen — маршрут пройден
uint32_t stepStart = 0, stepMs = 0;
float stepV[2] = {0, 0};            // команда текущего отрезка: vx, vy

// Команда отрезка i. До начала и после конца маршрута — стоим.
void segCmd(int i, float v[2], uint32_t& ms) {
  v[0] = 0; v[1] = 0; ms = 0;
  if (i >= 0 && i < routeLen) segToCmd(route[i], v[0], v[1], ms);
}

// Разгон центрирован на границе отрезков: каждая составляющая скорости начинает меняться
// за половину своего времени разгона ДО границы и заканчивает через столько же ПОСЛЕ.
// Недобор пути до границы и перебор после неё равны, поэтому робот проходит ровно заданные
// метры при любом сочетании отрезков. Если начинать разгон прямо на границе, ошибки гасятся
// только у отрезков вдоль одной оси; на зигзаге с выездом на прямую набегает ~5 см вперёд.
// Отрезок должен быть длиннее своего разгона и торможения (при CRUISE 0.6 и ACCEL 1.2 — от ~0,5 с).
uint32_t leadMs(float from, float to) {
  return (uint32_t)(fabsf(to - from) / (2.0f * ACCEL) * 1000.0f);
}

void startRoute(const Seg* r, size_t n) {
  route = r; routeLen = (int)n; routeIdx = -1;
  float v0[2]; uint32_t ms0;
  segCmd(0, v0, ms0);
  stepV[0] = 0; stepV[1] = 0;
  uint32_t a = leadMs(0, v0[0]), b = leadMs(0, v0[1]);
  stepMs = a > b ? a : b;           // разгон в первый отрезок тоже центрирован на его начале
  stepStart = millis();
}

// Вызывать каждый проход loop(). true — маршрут пройден и робот плавно остановился.
bool routeTick(float dt) {
  if (routeIdx < routeLen && millis() - stepStart >= stepMs) {
    stepStart += stepMs;            // граница по расписанию, задержки loop() не накапливаются
    routeIdx++;
    segCmd(routeIdx, stepV, stepMs);
    if (routeIdx < routeLen)
      Serial.printf("[ROUTE] отрезок %d: vx=%.2f vy=%.2f, %lu мс\n",
                    routeIdx, stepV[0], stepV[1], (unsigned long)stepMs);
  }
  // переход к следующему отрезку начинаем заранее, для vx и vy — каждый со своим запасом
  float next[2]; uint32_t nextMs;
  segCmd(routeIdx + 1, next, nextMs);
  uint32_t e = millis() - stepStart;
  float v[2];
  for (int k = 0; k < 2; k++) v[k] = (e + leadMs(stepV[k], next[k]) >= stepMs) ? next[k] : stepV[k];
  setTarget(v[0], v[1], 0);
  updateRamp(dt);
  return routeIdx >= routeLen && isStopped();
}
