#!/bin/sh
# Сборка симулятора и прогон сценариев. Без аргументов — все сценарии, иначе: run.sh <сценарий> [-v]
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -I stubs sim.cpp -o /tmp/robot_cube_sim
if [ $# -gt 0 ]; then exec /tmp/robot_cube_sim "$@"; fi
rc=0
for s in normal lost_load lost_started done_early no_done newline late_wifi wifi_stuck silent_arm; do
  /tmp/robot_cube_sim "$s" || rc=1
done
exit $rc
