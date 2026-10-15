// Прогон прошивки robot_cube на ПК без железа (на робот это не идёт).
// Время виртуальное, манипулятор и меканум-шасси заменены моделями.
// Проверяет то, что требует ТЗ: стартовую паузу, «моторы выключены до LOAD», повтор LOAD через 3 с,
// реакцию на BUSY, таймаут DONE 30 с, неподвижность во время погрузки и куда робот в итоге приехал.
//
// Запуск: tools/sim/run.sh            — все сценарии
//         tools/sim/run.sh normal -v   — один сценарий с логом прошивки
#include "../../robot_cube/robot_cube.ino"
#include <vector>

namespace {

struct Scenario {
  const char* name;
  const char* what;
  int dropLoads;          // сколько первых LOAD теряется по дороге к манипулятору
  bool loseStarted;       // ответ STARTED на первый принятый LOAD теряется
  bool loseBusy;          // все ответы BUSY теряются
  bool sendDone;          // манипулятор присылает DONE
  bool silent;            // манипулятор не отвечает совсем
  bool lowerNewline;      // ответы строчными и с \r\n в конце
  int beginsNeeded;       // Wi-Fi поднимается только после N-го WiFi.begin()
  uint32_t connectDelay;  // через сколько мс после begin() поднимается Wi-Fi
  bool expectFinish;      // должен ли робот дойти до FINISHED
};

const Scenario SCENARIOS[] = {
  //  имя            что проверяем                                   drop  loseSt loseBusy done  silent lower begins delay  finish
  {"normal",       "всё штатно",                                     0, false, false, true,  false, false, 1, 2000,  true},
  {"lost_load",    "первый LOAD потерян -> повтор через 3 с",        1, false, false, true,  false, false, 1, 2000,  true},
  {"lost_started", "STARTED потерян -> повтор -> BUSY",              0, true,  false, true,  false, false, 1, 2000,  true},
  {"done_early",   "STARTED и все BUSY потеряны, пришёл только DONE", 0, true,  true,  true,  false, false, 1, 2000,  true},
  {"no_done",      "DONE не приходит -> таймаут 30 с",               0, false, false, false, false, false, 1, 2000,  true},
  {"newline",      "ответы строчными и с \\r\\n",                    0, false, false, true,  false, true,  1, 2000,  true},
  {"late_wifi",    "Wi-Fi поднялся только через 45 с",               0, false, false, true,  false, false, 1, 45000, true},
  {"wifi_stuck",   "Wi-Fi поднимается только после нового begin()",  0, false, false, true,  false, false, 2, 2000,  true},
  {"silent_arm",   "манипулятор молчит -> LOAD каждые 3 с",          0, false, false, true,  true,  false, 1, 2000,  false},
};

const Scenario* sc = nullptr;
int failures = 0;

void check(bool ok, const char* fmt, ...) {
  if (ok) return;
  failures++;
  va_list ap; va_start(ap, fmt);
  printf("  ОШИБКА: "); vprintf(fmt, ap); printf("\n");
  va_end(ap);
}

bool anyDuty() {
  for (const Motor& m : motors) if (sim::duty[m.in1] || sim::duty[m.in2]) return true;
  return false;
}

// ---------- шасси ----------
// Модель: колесо крутится со скоростью (duty - MIN_DUTY) / (255 - MIN_DUTY), кинематика меканума
// линейная, команда CRUISE вперёд даёт SPEED_FWD, вбок — SPEED_SIDE (ровно то, что заложено в motion.h).
double x = 0, y = 0, armX = 0, armY = 0, maxRot = 0, vxLast = 0, vyLast = 0, maxJump = 0;
uint32_t wifiUpAt = 0, tEnter[6] = {0};
State prevState = WAIT_START;
bool movedInWaitStart = false;

double wheel(int i) {
  const Motor& m = motors[i];
  uint32_t a = sim::duty[m.in1], b = sim::duty[m.in2];
  if (a && b) sim::fail("мотор %d: ШИМ одновременно на IN1 и IN2", i + 1);
  uint32_t d = a ? a : b;
  if (!d) return 0;
  if (d < (uint32_t)MIN_DUTY) sim::fail("мотор %d: скважность %u ниже MIN_DUTY", i + 1, d);
  double s = (d - MIN_DUTY) / double(255 - MIN_DUTY);
  return (a ? s : -s) * m.dir;
}

void tick() {
  double s[4];
  for (int i = 0; i < 4; i++) s[i] = wheel(i);
  bool moving = anyDuty();
  if (moving && state == WAIT_START) movedInWaitStart = true;
  if (moving && (state == REQ_LOAD || state == WAIT_DONE))
    sim::fail("моторы крутятся в состоянии %s", STATE_NAMES[state]);

  double vx = (s[0] + s[1] + s[2] + s[3]) / 4 * SPEED_FWD / CRUISE;
  double vy = (s[0] - s[1] - s[2] + s[3]) / 4 * SPEED_SIDE / CRUISE;
  double rot = fabs(s[0] - s[1] + s[2] - s[3]) / 4;
  if (rot > maxRot) maxRot = rot;
  double jump = hypot(vx - vxLast, vy - vyLast);   // скачок скорости корпуса за 1 мс, м/с
  if (jump > maxJump) maxJump = jump;
  vxLast = vx; vyLast = vy;
  x += vx * 0.001;
  y += vy * 0.001;

  if (!wifiUpAt && WiFi.status() == WL_CONNECTED) wifiUpAt = sim::now_ms;
  if (state != prevState) { tEnter[state] = tState; prevState = state; }
}

// ---------- манипулятор ----------
int armDrops = 0, armAccepted = 0;
uint32_t armBusyUntil = 0;
std::vector<uint32_t> loadTimes, doneTimes;

void armReply(const char* word, uint32_t at) {
  std::string w = word;
  if (sc->lowerNewline) { for (auto& c : w) c = (char)tolower((unsigned char)c); w += "\r\n"; }
  sim::inbox.emplace(at, w);
}

void armOnSend(const std::string& cmd) {
  uint32_t now = sim::now_ms;
  if (cmd != "LOAD") sim::fail("манипулятор получил неожиданную команду '%s'", cmd.c_str());
  check(!anyDuty(), "LOAD отправлен при работающих моторах (t=%u)", now);
  if (loadTimes.empty()) { armX = x; armY = y; }
  loadTimes.push_back(now);
  if (sc->silent) return;
  if (armDrops > 0) { armDrops--; return; }
  if (now < armBusyUntil) { if (!sc->loseBusy) armReply("BUSY", now + 40); return; }
  armBusyUntil = now + 15000;
  armAccepted++;
  if (!(sc->loseStarted && armAccepted == 1)) armReply("STARTED", now + 40);
  if (sc->sendDone)
    for (uint32_t d : {14000u, 14500u, 15000u}) { armReply("DONE", now + d); doneTimes.push_back(now + d); }
}

void sumRoute(const Seg* r, size_t n, double& f, double& rt) {
  for (size_t i = 0; i < n; i++) { f += r[i].fwd; rt += r[i].right; }
}

}  // namespace

int main(int argc, char** argv) {
  const char* want = argc > 1 ? argv[1] : "normal";
  bool verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
  for (const Scenario& s : SCENARIOS) if (strcmp(s.name, want) == 0) sc = &s;
  if (!sc) { fprintf(stderr, "нет сценария %s\n", want); return 2; }

  sim::quiet = !verbose;
  sim::beginsNeeded = sc->beginsNeeded;
  sim::connectDelayMs = sc->connectDelay;
  sim::onSend = armOnSend;
  sim::onTick = tick;
  armDrops = sc->dropLoads;

  printf("=== %s: %s\n", sc->name, sc->what);
  setup();
  while (millis() < 240000) {
    loop();
    if (state == FINISHED) break;
    if (sc->silent && state == REQ_LOAD && millis() - tState > 20000) break;
  }

  // где должен оказаться робот по route.h
  double armF = 0, armR = 0;
  sumRoute(ROUTE_TO_ARM, LEN(ROUTE_TO_ARM), armF, armR);
  double finF = armF, finR = armR;
  sumRoute(ROUTE_TO_UNLOAD, LEN(ROUTE_TO_UNLOAD), finF, finR);

  // 1. стартовая пауза
  check(!movedInWaitStart, "моторы крутились во время стартовой паузы");
  check(tEnter[TO_ARM] >= START_DELAY_MS, "поехал раньше %u мс", (unsigned)START_DELAY_MS);

  // 2. LOAD: первый — сразу как есть Wi-Fi и робот стоит, повторы — ровно через LOAD_RETRY_MS
  if (loadTimes.empty()) {
    check(false, "LOAD так и не отправлен (Wi-Fi: %s)", wifiUpAt ? "есть" : "нет");
  } else {
    uint32_t ready = tEnter[REQ_LOAD] > wifiUpAt ? tEnter[REQ_LOAD] : wifiUpAt;
    check(loadTimes[0] - ready <= 10, "первый LOAD через %u мс после готовности", loadTimes[0] - ready);
    for (size_t i = 1; i < loadTimes.size(); i++) {
      uint32_t d = loadTimes[i] - loadTimes[i - 1];
      check(d >= LOAD_RETRY_MS && d <= LOAD_RETRY_MS + 10, "интервал между LOAD %u мс", d);
    }
  }

  // 3. выход из ожидания: по первому DONE или ровно через DONE_TIMEOUT_MS
  if (sc->expectFinish) {
    check(state == FINISHED, "не дошёл до FINISHED (застрял в %s)", STATE_NAMES[state]);
    check(armAccepted == 1, "манипулятор запускал погрузку %d раз", armAccepted);
    // если до WAIT_DONE не дошли (все ответы на LOAD потерялись), уезжать надо по первому DONE
    bool waited = tEnter[WAIT_DONE] != 0;
    uint32_t tw = waited ? tEnter[WAIT_DONE] : tEnter[REQ_LOAD];
    uint32_t expect = waited ? tw + DONE_TIMEOUT_MS : UINT32_MAX;
    for (uint32_t d : doneTimes) if (d >= tw && d < expect) { expect = d; break; }
    uint32_t got = tEnter[TO_UNLOAD];
    check(got >= expect && got - expect <= 10, "уехал с погрузки в %u мс, ожидалось %u", got, expect);
  } else {
    check(state == REQ_LOAD, "должен ждать ответа в REQ_LOAD, а он в %s", STATE_NAMES[state]);
    check(loadTimes.size() >= 6, "за 20 с тишины отправлено только %zu LOAD", loadTimes.size());
  }

  // 4. отчёт
  printf("  состояния, с: TO_ARM %.2f | REQ_LOAD %.2f | WAIT_DONE %.2f | TO_UNLOAD %.2f | FINISHED %.2f\n",
         tEnter[TO_ARM] / 1e3, tEnter[REQ_LOAD] / 1e3, tEnter[WAIT_DONE] / 1e3, tEnter[TO_UNLOAD] / 1e3,
         tEnter[FINISHED] / 1e3);
  printf("  LOAD отправлен %zu раз:", loadTimes.size());
  for (uint32_t t : loadTimes) printf(" %.2f", t / 1e3);
  printf("\n");
  if (state == FINISHED || sc->silent) {
    printf("  поворот корпуса (должен быть 0): %.4f;  макс. скачок скорости %.1f см/с\n", maxRot, maxJump * 100);
  }
  if (!loadTimes.empty()) {
    printf("  у манипулятора: x=%.3f y=%.3f м, по route.h %.3f / %.3f, ошибка %+.3f / %+.3f м\n",
           armX, armY, armF, armR, armX - armF, armY - armR);
  }
  if (state == FINISHED) {
    printf("  конец маршрута: x=%.3f y=%.3f м, по route.h %.3f / %.3f, ошибка %+.3f / %+.3f м\n",
           x, y, finF, finR, x - finF, y - finR);
  }
  printf("  %s\n", failures ? "ПРОВАЛ" : "OK");
  return failures ? 1 : 0;
}
