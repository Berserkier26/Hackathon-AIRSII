// motion.h — моторы и езда по маршруту БЕЗ ШИМ.
// Каждое колесо либо крутится вперёд, либо назад, либо стоит (только digitalWrite).
// Здесь работает человек, который отвечает за движение. Маршрут лежит отдельно в route.h.
#pragma once
#include <Arduino.h>

// ================== КАЛИБРОВКА СКОРОСТЕЙ ==================
// Сколько метров в секунду робот проезжает в каждом виде движения.
// Как замерить (CALIB_TEST в robot_cube.ino), d — реально проеханное расстояние по рулетке:
//   CALIB_TEST 1: «2 м вперёд»                  -> SPEED_FWD  = SPEED_FWD  * d / 2
//   CALIB_TEST 2: «2 м вбок вправо»             -> SPEED_SIDE = SPEED_SIDE * d / 2
//   CALIB_TEST 3: «2 м по диагонали вперёд-вправо», d — по прямой от старта до финиша
//                                               -> SPEED_DIAG = SPEED_DIAG * d / 2
// Мерить на том же полу, с заряженным аккумулятором и с теми же PULSE_ON_MS / PULSE_OFF_MS.
const float SPEED_FWD  = 0.50f;   // м/с вперёд и назад
const float SPEED_SIDE = 0.40f;   // м/с вбок (у меканума обычно меньше, чем вперёд)
const float SPEED_DIAG = 0.30f;   // м/с по диагонали (крутится только одна пара колёс)

// ================== СКОРОСТЬ БЕЗ ШИМ ==================
// Мотор включается только целиком. Чтобы робот ехал медленнее, моторы работают импульсами:
// PULSE_ON_MS включены, PULSE_OFF_MS выключены, и так по кругу.
//   PULSE_OFF_MS = 0  -> моторы включены постоянно, полная скорость
//   больше OFF        -> медленнее (20/20 — примерно вдвое медленнее, 20/40 — ещё медленнее)
// Импульсы — это по сути очень медленный программный ШИМ. Если судьи и такое запрещают — PULSE_OFF_MS = 0.
// После смены этих чисел скорости SPEED_* надо откалибровать заново.
const uint16_t PULSE_ON_MS  = 20;
const uint16_t PULSE_OFF_MS = 20;

const uint16_t PAUSE_MS = 400;    // полная остановка между отрезками: робот успевает встать, меньше заносов

#define LEN(a) (sizeof(a) / sizeof(a[0]))

// ================== МОТОРЫ ==================
// L298N №1 (задний):   IN1=18 IN2=17 IN3=16 IN4=4
// L298N №2 (передний): IN1=26 IN2=25 IN3=33 IN4=32
// Пины менять здесь (и так же в tools/motor_test/motor_test.ino). Порядок строк не менять.
// Если колесо в WHEEL_TEST крутится назад — dir = -1.
struct Motor { uint8_t in1, in2; int8_t dir; };
Motor motors[4] = {
  //  IN1 IN2 dir
  {  26, 25, +1 },   // 0 передний левый
  {  33, 32, +1 },   // 1 передний правый
  {  16,  4, +1 },   // 2 задний левый
  {  18, 17, +1 },   // 3 задний правый
};

// s: +1 — вперёд, -1 — назад, 0 — стоп
void motorSet(int i, int s) {
  const Motor& m = motors[i];
  s *= m.dir;
  digitalWrite(m.in1, s > 0 ? HIGH : LOW);
  digitalWrite(m.in2, s < 0 ? HIGH : LOW);
}

void motorsOff() {
  for (int i = 0; i < 4; i++) motorSet(i, 0);
}

void motionBegin() {
  for (int i = 0; i < 4; i++) { pinMode(motors[i].in1, OUTPUT); pinMode(motors[i].in2, OUTPUT); }
  motorsOff();
}

// ================== ВИДЫ ДВИЖЕНИЯ ==================
// Робот не поворачивается: курс всё время один и тот же, меканум едет в любую сторону.
enum MoveType { GO_FWD, GO_BACK, GO_RIGHT, GO_LEFT, GO_FWD_RIGHT, GO_FWD_LEFT, GO_BACK_RIGHT, GO_BACK_LEFT };

// Какое колесо куда крутится: { передний левый, передний правый, задний левый, задний правый }
// Если при GO_RIGHT робот едет влево — поменять местами строки GO_RIGHT и GO_LEFT,
// а также GO_FWD_RIGHT <-> GO_FWD_LEFT и GO_BACK_RIGHT <-> GO_BACK_LEFT.
const int8_t WHEELS[8][4] = {
  { +1, +1, +1, +1 },   // GO_FWD        вперёд
  { -1, -1, -1, -1 },   // GO_BACK       назад
  { +1, -1, -1, +1 },   // GO_RIGHT      вбок вправо
  { -1, +1, +1, -1 },   // GO_LEFT       вбок влево
  { +1,  0,  0, +1 },   // GO_FWD_RIGHT  диагональ вперёд-вправо
  {  0, +1, +1,  0 },   // GO_FWD_LEFT   диагональ вперёд-влево
  {  0, -1, -1,  0 },   // GO_BACK_RIGHT диагональ назад-вправо
  { -1,  0,  0, -1 },   // GO_BACK_LEFT  диагональ назад-влево
};
const char* MOVE_NAMES[8] = { "вперёд", "назад", "вбок вправо", "вбок влево",
                              "вперёд-вправо", "вперёд-влево", "назад-вправо", "назад-влево" };

float moveSpeed(MoveType t) {
  if (t == GO_FWD || t == GO_BACK) return SPEED_FWD;
  if (t == GO_RIGHT || t == GO_LEFT) return SPEED_SIDE;
  return SPEED_DIAG;
}

void wheelsFor(MoveType t) {
  for (int i = 0; i < 4; i++) motorSet(i, WHEELS[t][i]);
}

// ================== МАРШРУТ ==================
// Отрезок: вид движения, сколько метров и k — поправка для ручной подгонки (1.0 = без поправки,
// 1.05 = ехать на 5% дольше). Время отрезка = метры / скорость этого вида движения * k.
struct Move { MoveType type; float meters; float k; };

const Move* route = nullptr;
int routeLen = 0, routeIdx = 0;
uint32_t stepStart = 0, stepMs = 0;
bool pausing = false;

void beginMove() {
  const Move& m = route[routeIdx];
  stepMs = (uint32_t)(m.meters / moveSpeed(m.type) * m.k * 1000.0f);
  stepStart = millis();
  pausing = false;
  Serial.printf("[ROUTE] отрезок %d: %s %.2f м, %lu мс\n",
                routeIdx, MOVE_NAMES[m.type], m.meters, (unsigned long)stepMs);
}

void startRoute(const Move* r, size_t n) {
  route = r; routeLen = (int)n; routeIdx = 0;
  if (routeLen > 0) beginMove();
}

// Вызывать каждый проход loop(). true — маршрут пройден и робот стоит.
bool routeTick() {
  if (routeIdx >= routeLen) { motorsOff(); return true; }
  uint32_t t = millis() - stepStart;
  if (!pausing) {
    if (t >= stepMs) {                       // отрезок проехан -> стоп и пауза
      motorsOff();
      pausing = true;
      stepStart = millis();
      return false;
    }
    // импульсы: начало каждого отрезка — с включённых моторов
    bool on = PULSE_OFF_MS == 0 || (t % (PULSE_ON_MS + PULSE_OFF_MS)) < PULSE_ON_MS;
    if (on) wheelsFor(route[routeIdx].type);
    else    motorsOff();
    return false;
  }
  if (t >= PAUSE_MS) {                       // пауза закончилась -> следующий отрезок
    routeIdx++;
    if (routeIdx >= routeLen) return true;
    beginMove();
  }
  return false;
}
